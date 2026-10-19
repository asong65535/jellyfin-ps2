/*
 * Music playback. Two threads: a network reader filling a ring buffer with
 * the MP3 stream, and a worker decoding it with libmad and feeding audsrv
 * (the IOP sound driver). The screen thread only sets commands (play,
 * pause, stop); the worker alone calls audsrv.
 */

#include "audio.h"
#include "freeze.h"
#include "config.h"
#include "net.h"

#include <kernel.h>
#include <audsrv.h>
#include <string.h>
#include <stdio.h>

#include <mad.h>

/*
 * One worker thread, driven by two volatile flags.
 *
 * The UI thread never touches the socket, the decoder or audsrv; it sets
 * g_cmd and reads g_state. That keeps the frame loop free of anything that
 * can block, which matters because audsrv_wait_audio blocks until the SPU2
 * has room. That wait is also what paces the download, so there is no ring
 * buffer and no clock to maintain.
 *
 * MP3 rather than PCM, decoded here with libmad.
 *
 * The original plan was to have Jellyfin transcode to PCM so the console
 * needed no decoder at all. That turned out to be impossible: its PCM path
 * emits a bare "-ar" with no value, ffmpeg swallows the following argument
 * and exits with code 234. Every wav request returns 500.
 *
 * Serving the file untouched works and is better anyway: no transcode for
 * the server to do, a fifth of the bandwidth, a real Content-Length for the
 * progress bar, and no dependency on ffmpeg behaving. libmad was written for
 * hardware slower than a 294 MHz EE, so the decode is affordable.
 */

#define CMD_IDLE   0
#define CMD_PLAY   1
#define CMD_STOP   2

/* Network read size. Small enough to stay responsive to a stop. */
#define CHUNK   AUDIO_CHUNK

/*
 * Decoder input buffer. libmad needs a frame to be contiguous, so undecoded
 * bytes are carried forward and the next read appended after them.
 */
#define MAD_BUF AUDIO_IN_BUF

/* Decoded samples waiting to go to audsrv, in bytes. */
#define PCM_BUF AUDIO_PCM_BUF

static volatile int g_cmd;
static volatile int g_state = AUDIO_STOPPED;
static volatile int g_step;      /* see audio_debug */

/*
 * Whether audio_init got as far as starting the worker, and if not why.
 * Without a worker nothing ever plays, and audio_play used to wipe the
 * error and say Buffering for ever - a sound start-up failure was
 * invisible.
 */
static int  g_init_ok;
static char g_init_err[96];

/*
 * Which play request is current. audio_play bumps it; a worker still
 * busy with an older track sees the change and leaves, instead of
 * carrying on under the new track's freshly reset counters.
 */
static volatile int g_gen;
static volatile int g_rx;        /* bytes received this track */
static volatile int g_paused;
static volatile int g_bytes;          /* PCM handed to the SPU2 */
static volatile int g_ended;          /* the track reached its end by itself */

static char g_host[64];
static int  g_port;
static char g_path[512];
static char g_token[192];
static char g_err[96];

/*
 * Decode counters.
 *
 * Frames libmad accepted, and frames it refused. A refused frame is a gap of
 * about 26 ms, which is audible as a click. If errors climb while the
 * delivered rate sits at the required figure, the pipeline is keeping up and
 * the data itself is damaged.
 */
static volatile int g_frames;
static volatile int g_errors;

/*
 * Bytes thrown away by the carry-forward.
 *
 * When libmad leaves next_frame NULL, or pointing past the end, there is no
 * way to know how much was consumed and the whole buffer is dropped. That is
 * up to AUDIO_IN_BUF of MP3, well over a second of audio, so if this is ever
 * non-zero it is a serious source of glitches and not a rounding error.
 */
static volatile int g_dropped;


/*
 * Error breakdown.
 *
 * LOSTSYNC is expected and mostly harmless: libmad reports it whenever it is
 * handed a fresh buffer and has to hunt for a frame header, which happens on
 * every refill. Counting it alongside real failures made the error total look
 * alarming when most of it is routine.
 *
 * BADDATA and the rest are genuine: a frame that decoded to nothing, which is
 * roughly 26 ms of silence.
 */
static volatile int g_err_sync;      /* MAD_ERROR_LOSTSYNC */
static volatile int g_err_real;      /* everything else recoverable */
static volatile int g_last_err;      /* the most recent code, for diagnosis */


static volatile int g_hz;             /* from the first decoded frame */
static volatile int g_ch;


static int  g_thread = -1;

/*
 * Wakes the idle worker when a track is asked for.
 *
 * It used to idle on a 100 ms alarm, so starting a track depended on that
 * alarm firing. After a video the worker was found asleep with a track
 * waiting (step 0, loop count frozen): its wake-up never came. A semaphore
 * signalled by audio_play does not depend on the alarm at all.
 */
static int  g_wake = -1;
/*
 * Generous, because the cost of being wrong here is silent memory corruption
 * rather than a clean crash. libmad's own recursion is modest now that its
 * large structures are static, but the margin is cheap.
 */
static u8   g_stack[64 * 1024] __attribute__((aligned(16)));

static u8   g_in[MAD_BUF]  __attribute__((aligned(64)));

/*
 * Ring buffer between the reader thread and the decoder.
 *
 * One producer, one consumer, and the EE runs one thread at a time, so two
 * volatile indices are enough: no semaphore, no lock. The writer only ever
 * advances head, the reader only ever advances tail, and an aligned int store
 * is atomic here so neither can observe a half-written index.
 */
static volatile int g_head;      /* next byte to write */
static volatile int g_tail;      /* next byte to read  */
static volatile int g_eof;       /* reader reached the end of the stream */
static volatile int g_sock = -1;

#if AUDIO_READER_THREAD
static u8 g_ring[AUDIO_RING] __attribute__((aligned(64)));

static u8  g_read_stack[32 * 1024] __attribute__((aligned(16)));
static int g_read_thread = -1;

/*
 * Bytes waiting in the music ring (written by the reader thread, read by
 * the worker).
 */
static int ring_used(void)
{
	int h = g_head, t = g_tail;
	return (h >= t) ? (h - t) : (AUDIO_RING - t + h);
}

/*
 * Room left in the music ring; one byte is kept free so full and empty
 * differ.
 */
static int ring_free(void)
{
	return AUDIO_RING - 1 - ring_used();
}

/* Copies out up to max bytes, handling the wrap. Returns the count. */
static int ring_read(u8 *dst, int max)
{
	int used = ring_used();
	int n    = (used < max) ? used : max;

	if (n <= 0)
		return 0;

	int t     = g_tail;
	int first = AUDIO_RING - t;

	if (first > n)
		first = n;

	memcpy(dst, g_ring + t, first);

	if (n > first)
		memcpy(dst + first, g_ring, n - first);

	g_tail = (t + n) % AUDIO_RING;
	return n;
}

/*
 * The reader. Owns the socket and does nothing else, so a slow read stalls
 * only itself and never the SPU2 feed.
 */
static void reader(void *arg)
{
	(void)arg;

	for (;;) {
		if (g_sock < 0 || g_eof || g_cmd != CMD_PLAY) {
			net_snooze(AUDIO_IDLE_MS);
			continue;
		}

		if (ring_free() < CHUNK) {
			net_snooze(20);          /* decoder is behind; let it catch up */
			continue;
		}

		int h     = g_head;
		int space = AUDIO_RING - h;      /* contiguous room before the wrap */

		if (space > CHUNK)
			space = CHUNK;

		int n = net_stream_read(g_sock, g_ring + h, space);

		if (n <= 0) {
			g_eof = 1;               /* server closed: track finished */
			continue;
		}

		g_head = (h + n) % AUDIO_RING;
	}
}
#endif /* AUDIO_READER_THREAD */
static u8   g_pcm[PCM_BUF] __attribute__((aligned(64)));

/* A block of zeros, fed to the SPU2 while paused. Never written to. */
static u8   g_silence[AUDIO_CHUNK] __attribute__((aligned(64)));

/* ------------------------------------------------------------------ */

/*
 * libmad works in 28-bit fixed point. Round, clip and drop to signed 16-bit,
 * which is what the SPU2 wants.
 */
static inline short to_s16(mad_fixed_t v)
{
	v += (1L << (MAD_F_FRACBITS - 16));          /* round */

	if (v >= MAD_F_ONE)  v = MAD_F_ONE - 1;      /* clip */
	if (v < -MAD_F_ONE)  v = -MAD_F_ONE;

	return (short)(v >> (MAD_F_FRACBITS + 1 - 16));
}

/*
 * Length of an ID3v2 tag at the head of the stream, or 0.
 *
 * libmad does not know about ID3 and will happily hunt for a frame sync
 * inside one. Tags routinely carry embedded cover art, and JPEG data contains
 * plenty of byte pairs that look like a sync, so the decoder produces
 * confident nonsense: noise rather than silence.
 *
 * Header is "ID3", two version bytes, one flags byte, then four syncsafe
 * length bytes holding seven bits each.
 */
static int id3_len(const unsigned char *b, int n)
{
	if (n < 10 || b[0] != 'I' || b[1] != 'D' || b[2] != '3')
		return 0;

	int size = ((b[6] & 0x7F) << 21) | ((b[7] & 0x7F) << 14) |
	           ((b[8] & 0x7F) <<  7) |  (b[9] & 0x7F);

	return size + 10;
}

/*
 * Hand PCM to audsrv, one chunk at a time.
 *
 * audsrv_play_audio does NOT block. That has to be paired with
 * audsrv_wait_audio, which waits until there is room for the number of bytes
 * about to be written; both SDK samples do exactly this. Calling play_audio
 * on its own pushes at a full buffer and the surplus is simply dropped, which
 * sounds like noise over the music and makes the byte counter crawl.
 *
 * The wait is also what paces the whole pipeline: it is the only thing
 * throttling the decoder and, behind it, the download.
 */
static int g_gen_feed;            /* the generation feed() works for */

static void feed(const void *data, int bytes)
{
	const char *p = (const char *)data;
	int off = 0;

	/*
	 * Finish the buffer even if a pause arrives mid-way. Abandoning it
	 * discards samples that were already decoded, which is a click on
	 * resume; the few milliseconds involved are not worth it.
	 */
	while (off < bytes && g_cmd == CMD_PLAY && g_gen_feed == g_gen) {
		int n = bytes - off;

		if (n > AUDIO_CHUNK)
			n = AUDIO_CHUNK;        /* the size both samples use */

		g_step = 6;
		audsrv_wait_audio(n);
		audsrv_play_audio((char *)p + off, n);
		g_step = 5;

		off     += n;
		g_bytes += n;
	}
}

/*
 * libmad's working structures, deliberately static rather than local.
 *
 * mad_frame is around 10 KB and mad_synth around 13 KB, so together they
 * exceed the decoder thread's stack on their own. Overflowing into whatever
 * lies below produced exactly the symptoms seen on hardware: sporadic
 * MAD_ERROR_LOSTSYNC and a cascade of MAD_ERROR_BADDATAPTR, on a stream
 * proven byte-identical to the source.
 *
 * Only one thread ever touches these, so static costs nothing.
 */
static struct mad_stream g_stream;
static struct mad_frame  g_frame;
static struct mad_synth  g_synth;

/*
 * The music worker's main job, one track: wait for the reader to buffer,
 * decode MP3 frames with libmad, convert to 16-bit PCM, and feed audsrv
 * until the track ends or a stop is asked. Runs on the music worker
 * thread; the only code that calls audsrv for music.
 */
static void stream_one(void)
{

	int  in_len   = 0;          /* undecoded bytes at the front of g_in */
	int  eof      = 0;
	int  started  = 0;          /* audsrv format set from the first frame */
	int  skip     = -1;         /* ID3 bytes still to discard, -1 = unknown */

	g_bytes = 0;
	g_state = AUDIO_BUFFERING;

	g_head = g_tail = 0;
	g_eof  = 0;

	g_step = 1;
	g_rx   = 0;

	const int gen = g_gen;

	g_gen_feed = gen;

	int sock = net_stream_open(g_host, g_port, g_path,
	                           g_token[0] ? g_token : NULL);

	g_step = 2;

	if (sock < 0) {
		snprintf(g_err, sizeof(g_err), "connect failed (%d)", sock);
		g_state = AUDIO_ERROR;
		return;
	}

	/* From here the reader owns it and starts filling the ring. */
	g_sock = sock;

	mad_stream_init(&g_stream);
	mad_frame_init(&g_frame);
	mad_synth_init(&g_synth);

	/*
	 * Wait for the reader to build a cushion before decoding anything.
	 * Starting on an empty ring means the first frames play while the
	 * socket is still being read, which is when the SPU2 is most likely to
	 * run dry.
	 */
#if AUDIO_READER_THREAD
	while (ring_used() < AUDIO_PREBUFFER && !g_eof && g_cmd == CMD_PLAY)
		net_snooze(20);
#endif

	/*
	 * The madplay loop, and deliberately so.
	 *
	 * The previous version refilled and called mad_stream_buffer on every
	 * pass. Each of those resets makes libmad hunt for sync again, and a
	 * resync discards the bit reservoir: MP3 frames borrow bits from the
	 * bytes before them, so the frames that follow then fail with
	 * MAD_ERROR_BADDATAPTR. On hardware that was 118 syncs cascading into
	 * 298 reservoir failures, roughly two frames in five.
	 *
	 * Here the buffer is handed over once and refilled only when libmad
	 * says MAD_ERROR_BUFLEN, which is the one moment it is safe to move
	 * the data. Recoverable errors just continue: libmad resyncs by itself
	 * and the reservoir survives.
	 */
	int have_buffer = 0;


	while (g_cmd == CMD_PLAY && gen == g_gen) {
		if (g_paused) {
			g_state = AUDIO_PAUSED;

			/*
			 * Feed silence rather than stopping.
			 *
			 * The SPU2 must keep receiving something: starve it and it
			 * repeats its last block as a held tone. But calling
			 * audsrv_stop_audio is worse, because audsrv_wait_audio then
			 * blocks forever waiting for a buffer that will never drain,
			 * the worker never sees the stop command, and the app hangs.
			 *
			 * Zeros keep the hardware running normally, pace the loop
			 * through wait_audio exactly as playback does, and leave
			 * nothing to undo on resume. g_bytes is untouched, so the
			 * position does not creep while paused.
			 */
			audsrv_wait_audio(AUDIO_CHUNK);
			audsrv_play_audio((char *)g_silence, AUDIO_CHUNK);
			continue;
		}

		if (g_state == AUDIO_PAUSED)
			g_state = AUDIO_PLAYING;   /* carries on where it left off */

		if (!have_buffer) {
			/* Top up, keeping whatever libmad has not consumed. */
			while (!eof && in_len <= MAD_BUF - CHUNK) {
#if AUDIO_READER_THREAD
				int n = ring_read(g_in + in_len, MAD_BUF - in_len);

				if (n > 0)
					in_len += n;
				else if (g_eof)
					eof = 1;
				else
					break;            /* reader behind; use what we have */
#else
				int n = net_stream_read(sock, g_in + in_len, CHUNK);

				if (n > 0) {
					in_len += n;
					g_rx   += n;
				} else {
					eof = 1;
				}
#endif
			}

			if (in_len == 0)
				break;

			/*
			 * Discard any ID3v2 tag before the decoder sees it. libmad
			 * would otherwise hunt for a frame sync inside it, and a tag
			 * carrying cover art contains plenty of byte pairs that look
			 * like one.
			 */
			if (skip < 0 && in_len >= 10)
				skip = id3_len(g_in, in_len);

			if (skip > 0) {
				int drop = (skip < in_len) ? skip : in_len;

				memmove(g_in, g_in + drop, in_len - drop);
				in_len -= drop;
				skip   -= drop;

				if (in_len == 0)
					continue;
			}

			mad_stream_buffer(&g_stream, g_in, in_len);
			have_buffer = 1;
			g_step      = 3;
		}

		if (mad_frame_decode(&g_frame, &g_stream) != 0) {
			if (g_stream.error == MAD_ERROR_BUFLEN) {
				/*
				 * Out of data mid-frame. This is the only point at which
				 * the buffer may be moved: carry the unconsumed tail to
				 * the front and refill behind it.
				 */
				if (eof)
					break;                  /* nothing more is coming */

				int left = 0;

				if (g_stream.next_frame) {
					left = in_len - (int)(g_stream.next_frame - g_in);

					if (left > 0 && left < in_len)
						memmove(g_in, g_stream.next_frame, left);
				}

				if (left < 0 || left > in_len) {
					g_dropped += in_len;    /* should never happen */
					left = 0;
				}

				in_len      = left;
				have_buffer = 0;
				continue;
			}

			if (MAD_RECOVERABLE(g_stream.error)) {
				g_errors++;

				/*
				 * Nothing has decoded at all after a great many tries,
				 * so this is not MP3. Say so rather than playing
				 * silence: libmad handles no other format, and a track
				 * the server refused to transcode arrives as bytes it
				 * can never sync to.
				 */
				if (g_frames == 0 && g_errors > 2000) {
					snprintf(g_err, sizeof(g_err),
					         "not MP3, or transcode failed");
					g_state = AUDIO_ERROR;
					goto done;
				}

				if (g_stream.error == MAD_ERROR_LOSTSYNC) {
					g_err_sync++;
				} else {
					g_err_real++;
					g_last_err = (int)g_stream.error;
				}

				continue;                   /* libmad resyncs by itself */
			}

			break;                          /* not recoverable */
		}

		mad_synth_frame(&g_synth, &g_frame);
		g_frames++;

		/*
		 * The format is not known until a header has been read, so audsrv
		 * is configured here rather than up front. A 32 kHz file would
		 * otherwise play at the wrong pitch.
		 */
		if (!started) {
			struct audsrv_fmt_t fmt;

			fmt.freq     = g_frame.header.samplerate;
			fmt.bits     = 16;
			fmt.channels = (g_synth.pcm.channels > 1) ? 2 : 1;

			g_step = 4;

			if (audsrv_set_format(&fmt) != 0) {
				snprintf(g_err, sizeof(g_err), "%d Hz: %s",
				         g_frame.header.samplerate,
				         audsrv_get_error_string());
				g_state = AUDIO_ERROR;
				goto done;
			}

			audsrv_set_volume(MAX_VOLUME);

			g_hz = g_frame.header.samplerate;
			g_ch = fmt.channels;

			started = 1;
			g_state = AUDIO_PLAYING;
			g_step  = 5;
		}

		/* Interleave into signed 16-bit and hand it over. */
		{
			int    ch   = g_synth.pcm.channels;
			short *out  = (short *)g_pcm;
			int    have = 0;

			for (unsigned int i = 0; i < g_synth.pcm.length; i++) {
				out[have++] = to_s16(g_synth.pcm.samples[0][i]);

				if (ch > 1)
					out[have++] = to_s16(g_synth.pcm.samples[1][i]);

				if ((have + 2) * (int)sizeof(short) > PCM_BUF) {
					feed(g_pcm, have * (int)sizeof(short));
					have = 0;
				}
			}

			if (have > 0)
				feed(g_pcm, have * (int)sizeof(short));
		}
	}

done:
	mad_synth_finish(&g_synth);
	mad_frame_finish(&g_frame);
	mad_stream_finish(&g_stream);

	/* Take it back before closing, so the reader cannot touch a closed
	 * descriptor. */
	g_sock = -1;
	g_step = 7;                  /* closing the socket */
	net_stream_close(sock);

	/*
	 * Superseded by a newer play request: leave audsrv, the state and the
	 * command to the new track, and go straight back for it.
	 */
	if (gen != g_gen) {
		g_step = 9;
		return;
	}

	g_step = 8;                  /* inside audsrv_stop_audio */
	audsrv_stop_audio();
	g_step = 9;                  /* out: the track is fully stopped */

	if (g_state != AUDIO_ERROR)
		g_state = AUDIO_STOPPED;

	/*
	 * Clear the play command on the way out.
	 *
	 * Without this the worker sees CMD_PLAY still set, calls stream_one
	 * again, and the track restarts from the beginning: the whole song on
	 * a loop rather than stopping at the end.
	 *
	 * Only when it finished by itself. A stop or a new track has already
	 * set the command to something else, and overwriting that would race.
	 */
	if (g_cmd == CMD_PLAY) {
		g_cmd   = CMD_IDLE;
		g_ended = 1;
	}
}

static volatile int g_loops;     /* worker passes: it is alive if this moves */

static void worker(void *arg)
{
	(void)arg;

	for (;;) {
		g_loops++;

		if (g_cmd == CMD_PLAY)
			stream_one();
		else if (g_wake >= 0)
			WaitSema(g_wake);        /* until audio_play signals */
		else
			net_snooze(AUDIO_IDLE_MS);

		if (g_cmd == CMD_STOP)
			g_cmd = CMD_IDLE;
	}
}

/* ------------------------------------------------------------------ */

int audio_init(void)
{
	extern void *_gp;

	if (g_init_ok)
		return 0;                    /* already running */

	if (audsrv_init() != 0) {
		snprintf(g_err, sizeof(g_err), "audsrv: %s",
		         audsrv_get_error_string());
		snprintf(g_init_err, sizeof(g_init_err), "%s", g_err);
		g_state = AUDIO_ERROR;
		return -1;
	}

	{
		ee_sema_t sem;

		memset(&sem, 0, sizeof(sem));
		sem.init_count = 0;
		sem.max_count  = 1;          /* extra signals collapse into one */
		g_wake = CreateSema(&sem);
	}

	/*
	 * No format is set here: it comes from the first decoded frame, since
	 * the file decides the sample rate rather than us.
	 */

	ee_thread_t th;
	memset(&th, 0, sizeof(th));

	th.func             = worker;
	th.stack            = g_stack;
	th.stack_size       = sizeof(g_stack);
	th.gp_reg           = &_gp;
	th.initial_priority = AUDIO_DECODE_PRIO;

	g_thread = CreateThread(&th);
	freeze_register(g_thread, FZ_MUSIC);

	if (g_thread < 0) {
		snprintf(g_err, sizeof(g_err), "CreateThread failed");
		snprintf(g_init_err, sizeof(g_init_err), "%s", g_err);
		g_state = AUDIO_ERROR;
		return -1;
	}

	StartThread(g_thread, NULL);
	g_init_ok = 1;

	/*
	 * The reader, deliberately BELOW the decoder.
	 *
	 * Feeding the SPU2 is the real-time job. The reader only needs to keep
	 * up on average and has the whole ring to do it in, so it should fill
	 * the gaps while the decoder is blocked in audsrv_wait_audio rather
	 * than competing with it. Above the decoder it reads flat out whenever
	 * the ring has room and the hardware runs dry.
	 */
#if AUDIO_READER_THREAD
	memset(&th, 0, sizeof(th));

	th.func             = reader;
	th.stack            = g_read_stack;
	th.stack_size       = sizeof(g_read_stack);
	th.gp_reg           = &_gp;
	th.initial_priority = AUDIO_READER_PRIO;

	g_read_thread = CreateThread(&th);
	freeze_register(g_read_thread, FZ_MREADER);

	if (g_read_thread >= 0)
		StartThread(g_read_thread, NULL);
#endif

	return 0;
}

/*
 * Start a track from host:port/path: (re)start sound if needed, then hand
 * the request to the worker (signalled through its semaphore) and the
 * reader. Returns at once; the state goes Buffering then Playing.
 */
void audio_play(const char *host, int port, const char *path,
                const char *token)
{
	/*
	 * Sound did not start at boot: try again now, and if it still will
	 * not, say why instead of Buffering for ever.
	 */
	if (!g_init_ok && audio_init() != 0) {
		snprintf(g_err, sizeof(g_err), "Sound did not start (%.70s)",
		         g_init_err);
		g_state = AUDIO_ERROR;
		return;
	}

	audio_stop();

	snprintf(g_host,  sizeof(g_host),  "%s", host  ? host  : "");
	snprintf(g_path,  sizeof(g_path),  "%s", path  ? path  : "");
	snprintf(g_token, sizeof(g_token), "%s", token ? token : "");
	g_port = port;

	g_err[0] = '\0';
	g_paused     = 0;
	g_bytes      = 0;
	g_head       = 0;
	g_tail       = 0;
	g_eof        = 0;
	g_ended      = 0;
	g_frames     = 0;
	g_errors     = 0;
	g_dropped    = 0;
	g_err_sync   = 0;
	g_err_real   = 0;
	g_last_err   = 0;
	g_hz         = 0;
	g_ch         = 2;
	g_state      = AUDIO_BUFFERING;
	g_gen++;                     /* any older track's worker leaves */
	g_step       = 0;            /* 0 now means: not picked up yet */
	g_cmd        = CMD_PLAY;

	if (g_wake >= 0)
		SignalSema(g_wake);
}

/*
 * Pause the track: the worker stops feeding audsrv, nothing else changes.
 */
void audio_pause(void)  { g_paused = 1; }

/* Resume a paused track. */
void audio_resume(void) { g_paused = 0; }

/*
 * Ask the worker to stop the current track. Only sets the command: audsrv
 * is never called from the screen thread, since its client is not thread-
 * safe.
 */
void audio_stop(void)
{
	if (g_cmd != CMD_PLAY) {
		g_state  = AUDIO_STOPPED;
		g_paused = 0;
		return;
	}

	g_cmd    = CMD_STOP;
	g_paused = 0;

	/*
	 * No audsrv call here. This used to call audsrv_stop_audio to hurry
	 * the worker out of audsrv_wait_audio - from the UI thread, while the
	 * worker was inside audsrv on its own. audsrv's client is not safe
	 * for two threads at once: the worker's own audsrv_stop_audio on its
	 * way out then never returned, and every track after the first sat
	 * on Buffering. The worker checks g_cmd between chunks - about 12 ms
	 * of sound - and stops audsrv itself on the way out.
	 */

	/* Wait for it to leave the loop, so the socket is closed before the
	 * next track opens one. */
	for (int i = 0; i < 100 && g_state != AUDIO_STOPPED &&
	                           g_state != AUDIO_ERROR; i++)
		net_snooze(20);

	g_state = (g_state == AUDIO_ERROR) ? AUDIO_ERROR : AUDIO_STOPPED;
}

/* Stopped, Buffering, Playing, Paused or Error, for the player screen. */
AudioState audio_state(void) { return (AudioState)g_state; }

/*
 * One line on what the music worker is doing, for development builds
 * (DEBUG_UI): init state, loop count, command, step, bytes received,
 * frames, rate and decoder errors.
 */
const char *audio_debug(void)
{
	static char buf[128];

	snprintf(buf, sizeof(buf),
	         "init %d lp %d cmd %d step %d rx %dK fr %d hz %d "
	         "err %d/%d last %d drop %d",
	         g_init_ok, g_loops, g_cmd, g_step, g_rx >> 10, g_frames, g_hz,
	         g_err_sync, g_err_real, g_last_err, g_dropped);
	return buf;
}

/*
 * Seconds played in the current track, from PCM bytes handed to the sound
 * chip.
 */
int audio_position(void)
{
	/*
	 * Derived from bytes handed to the SPU2, using the rate the file
	 * actually decoded at rather than an assumed one.
	 */
	int per_sec = audio_rate_needed();

	return per_sec > 0 ? g_bytes / per_sec : 0;
}



/* True between a track ending by itself and the next one starting. */
int audio_ended(void)
{
	/*
	 * True once between a track finishing and the next one starting. The
	 * player uses it to know the difference between "stopped because it
	 * ended" and "stopped because you pressed stop".
	 */
	return g_ended;
}


/* PCM bytes per second the current track needs (rate x channels x 2). */
int audio_rate_needed(void)
{
	if (g_hz <= 0)
		return AUDIO_RATE * 2 * 2;

	return g_hz * g_ch * 2;
}

/* The last music error text, or ''. */
const char *audio_error(void) { return g_err; }