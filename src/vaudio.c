/*
 * The sound of a video: a thread that decodes the MP2 audio with libmad,
 * feeds audsrv, and publishes the playback clock that pictures are timed
 * against.
 */

#include "vaudio.h"
#include "freeze.h"
#include "screens.h"
#include "config.h"
#include "net.h"          /* net_snooze */

#include <audsrv.h>
#include <mad.h>

#include <kernel.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * Above the video decoder, but not above everything.
 *
 * On the EE a lower number is a higher priority, and this thread has been
 * wrong in both directions: below the decoder it could not keep up with
 * realtime and its ring overflowed, and above the music decoder it kept
 * preempting the very thread that fills that ring. The value is in config.h
 * alongside the others, which is the only way to see the ordering at all.
 */
#define VAUDIO_PRIO  VIDEO_AUDIO_PRIO
#define VA_INBUF     (32 * 1024) /* bitstream libmad works over */
#define VA_PCM       (8 * 1024)

/*
 * A cushion before the first frame: about a second of MP2 at 192 kbit/s.
 *
 * With the reader on its own thread and rings measured in seconds this is
 * normally there long before the first picture, so it costs nothing; it is
 * kept for a slow start.
 */
#define VA_PREBUFFER (24 * 1024)

/*
 * When audsrv holds less than this and there is nothing to decode, pad with
 * silence.
 *
 * Starved, the SPU2 repeats its last block, which is the buzz. But padding
 * whenever the INPUT ran low - as before - put gaps into sound that was
 * still arriving. Only the OUTPUT running low means a gap is coming anyway;
 * then silence is the better gap. About 40 ms at 48 kHz stereo.
 */
#define VA_LOW_WATER (4 * AUDIO_CHUNK)

static int  g_thread = -1;
/*
 * 64 KB, as the music player's decoder has: the same libmad runs here, and
 * the music player's own note is that getting this wrong costs silent
 * memory corruption rather than a crash. Was 16 KB.
 */
static char g_stack[64 * 1024] __attribute__((aligned(16)));

/*
 * Stack guard: the lowest 1 KB is filled with a pattern when the thread is
 * made. A stack grows down, so an overflow breaks the pattern before it
 * reaches anything else - and the music screen reports it.
 */
#define STACK_GUARD 1024

/* Fill the bottom of the sound thread's stack with a known pattern. */
static void stack_guard_fill(char *stk)
{
	for (int i = 0; i < STACK_GUARD; i++)
		stk[i] = (char)0xA5;
}

/* Whether that pattern is intact (1) or the stack overran into it (0). */
static int stack_guard_ok(const char *stk)
{
	for (int i = 0; i < STACK_GUARD; i++)
		if (stk[i] != (char)0xA5)
			return 0;

	return 1;
}

/* The sound thread's stack guard, for the DEBUG_UI readout. */
int vaudio_stack_ok(void) { return stack_guard_ok(g_stack); }

static volatile int g_run;       /* keep decoding */
static volatile int g_paused;    /* hold: feed silence, clock stands still */
static volatile int g_idle;      /* the worker has stopped and released audsrv */
static volatile int g_frames;
static volatile int g_underruns; /* silence chunks padded in */
static volatile int g_stage;     /* see vaudio_debug */
static volatile int g_errs;      /* frames libmad refused */
static volatile int g_loops;     /* worker passes, to see it is alive */

static char g_err[96];

static struct mad_stream g_stream;
static struct mad_frame  g_frame;
static struct mad_synth  g_synth;

static unsigned char g_in[VA_INBUF] __attribute__((aligned(64)));
static int           g_have;     /* bytes in g_in */

static short g_pcm[VA_PCM / 2] __attribute__((aligned(64)));
static short g_zero[AUDIO_CHUNK / 2] __attribute__((aligned(64)));

static int g_hz, g_ch;           /* the format audsrv was set to */
static int g_started;            /* past the initial fill */

/*
 * The clock, as the worker last measured it.
 *
 * Only the worker calls audsrv - the RPC client is not thread-safe, and a
 * second thread asking audsrv_queued while this one played is what threw
 * another PS2 player back to the browser. So the worker measures after
 * every chunk and publishes, and the UI only reads what was published.
 *
 * g_seq is odd while a write is in progress. The worker outranks the UI and
 * can preempt it mid-read, so the reader retries until it sees the same
 * even value before and after.
 */
static volatile int       g_seq;
static volatile int       g_clock_ok;
static volatile long long g_played;    /* real sample frames played */
static volatile int       g_queued;    /* real sample frames still queued */
static volatile clock_t   g_when;      /* when that was measured */
static volatile int       g_clock_hz;
static volatile long long g_skip;      /* samples skipped to meet the picture */

/* Worker-only accounting. */
static long long g_real;      /* real sample frames handed to audsrv */
static int       g_sil_run;   /* silence frames handed over since the last real */
static long long g_decoded;   /* sample frames decoded since the stream began */
static int       g_aligned;   /* reached the first picture's time */
static long long g_skip_w;    /* samples skipped, published by publish() */

void vaudio_pause(int on) { g_paused = on ? 1 : 0; }

/* Audio frames decoded this stream. */
int         vaudio_frames(void)    { return g_frames; }

/* Times the sound ran dry and silence was played instead. */
int         vaudio_underruns(void) { return g_underruns; }

/*
 * The readout line about the sound thread (DEBUG_UI): stage, rate,
 * channels, errors, clock state, loop count.
 */
const char *vaudio_debug(void)
{
	static char buf[64];

	snprintf(buf, sizeof(buf), "st%d hz%d ch%d err%d clk%d lp%d",
	         g_stage, g_hz, g_ch, g_errs, g_clock_ok, g_loops);
	return buf;
}


/*
 * libmad works in 28-bit fixed point. Round, clip and drop to signed 16-bit,
 * which is what the SPU2 wants. Same as audio.c; kept local so neither has
 * to expose it.
 */
static inline short to_s16(mad_fixed_t v)
{
	v += (1L << (MAD_F_FRACBITS - 16));

	if (v >= MAD_F_ONE)  v = MAD_F_ONE - 1;
	if (v < -MAD_F_ONE)  v = -MAD_F_ONE;

	return (short)(v >> (MAD_F_FRACBITS + 1 - 16));
}

/*
 * Measure and publish. Worker only.
 *
 * audsrv_queued is bytes not yet played. Whatever silence was padded since
 * the last real sound sits at the back of that queue, so it is taken off
 * first: the clock stands still through a gap rather than running on
 * without sound, and the picture waits with it.
 */
static void publish(void)
{
	int bpf = 2 * g_ch;                 /* bytes per sample frame */

	if (bpf <= 0)
		return;

	int q = audsrv_queued();

	if (q < 0)
		q = 0;

	q /= bpf;

	int real_q = q - g_sil_run;

	if (real_q < 0)
		real_q = 0;

	g_seq++;
	g_played   = g_real - real_q;
	g_queued   = real_q;
	g_skip     = g_skip_w;
	g_when     = clock();
	g_clock_hz = g_hz;
	g_clock_ok = 1;
	g_seq++;
}

/*
 * The playback clock: the timestamp of the sound being heard right now, at
 * 90 kHz, from samples handed to audsrv minus those still queued,
 * extrapolated from when it was published. 0 if not available.
 */
int vaudio_clock90(long long *pts90)
{
	long long p0 = scr_video_audio_pts0();

	if (p0 < 0)
		return 0;

	long long played, skip;
	int       queued, hz, ok, s;
	clock_t   when;

	do {
		s      = g_seq;
		ok     = g_clock_ok;
		played = g_played;
		queued = g_queued;
		when   = g_when;
		hz     = g_clock_hz;
		skip   = g_skip;
	} while ((s & 1) || s != g_seq);

	if (!ok || hz <= 0 || played < 0)
		return 0;

	/*
	 * Between measurements the SPU2 keeps playing, so move on by the
	 * time since - but never past what was still queued, or a decoder
	 * that has stopped would let the clock run on regardless.
	 */
	unsigned long dt = (unsigned long)(clock() - when);

	if (dt > (unsigned long)CLOCKS_PER_SEC)
		return 0;                      /* no report for a second: stale */

	long long since = (long long)dt * hz / CLOCKS_PER_SEC;

	if (since > queued)
		since = queued;

	/* The sound began where the picture did: skip samples in. */
	*pts90 = p0 + (skip + played + since) * 90000 / hz;
	return 1;
}


/*
 * Top up the bitstream buffer from the demuxer.
 *
 * libmad wants a contiguous run and tells us, through next_frame, how much
 * it did not consume. That remainder moves to the front and the rest is
 * filled from the ring.
 *
 * Returns the bytes available.
 */
static int refill(void)
{
	int keep = 0;

	if (g_stream.next_frame) {
		keep = g_have - (int)(g_stream.next_frame - g_in);

		if (keep > 0 && keep < g_have)
			memmove(g_in, g_stream.next_frame, keep);
		else if (keep < 0)
			keep = 0;
	}

	g_have = keep;

	/*
	 * Forget the pointer once it has been honoured.
	 *
	 * next_frame points into g_in, and the memmove above has just moved
	 * what it refers to. If this returns without handing the buffer back
	 * to libmad - which happens whenever there is not yet a whole frame -
	 * the next call would measure the remainder from a stale pointer and
	 * throw away good bytes.
	 */
	g_stream.next_frame = NULL;

	int room = VA_INBUF - g_have;

	/*
	 * A full buffer that libmad will not decode means the stream has lost
	 * sync. Drop the oldest half and let it find the next header rather
	 * than filling up and stopping for good. Only garbage reaches this:
	 * thirty-odd frames' worth that would not decode.
	 */
	if (room < 1024) {
		memmove(g_in, g_in + VA_INBUF / 2, VA_INBUF / 2);
		g_have = VA_INBUF / 2;
		room   = VA_INBUF - g_have;
	}

	/* The guard libmad reads past the end is not data: leave it free. */
	room -= MAD_BUFFER_GUARD;

	if (room > 0)
		g_have += scr_video_audio_read(g_in + g_have, room);

	return g_have;
}

/*
 * While paused: keep the SPU2 fed with silence so it does not buzz on its
 * last block, but only as much as it needs. Silence is counted apart from
 * real sound, so the clock - and the picture with it - stands still.
 */
static void paused_idle(void)
{
	if (g_hz > 0 && audsrv_queued() < VA_LOW_WATER) {
		audsrv_wait_audio(AUDIO_CHUNK);
		audsrv_play_audio((char *)g_zero, AUDIO_CHUNK);

		g_sil_run += AUDIO_CHUNK / (2 * g_ch);
		publish();
		return;
	}

	net_snooze(16);
}

/*
 * Hand PCM to audsrv, in the chunk size it expects, publishing the clock
 * after each chunk.
 *
 * audsrv_wait_audio blocks until there is space, which is what paces this
 * thread - the sound card is the clock. Bounded only by the stop flag.
 */
static void feed(const void *data, int bytes)
{
	const char *p = (const char *)data;
	int off = 0;
	int bpf = 2 * g_ch;

	while (off < bytes && g_run) {
		/* A pause lands between chunks, within about 10 ms. */
		while (g_paused && g_run)
			paused_idle();

		if (!g_run)
			break;

		int n = bytes - off;

		if (n > AUDIO_CHUNK)
			n = AUDIO_CHUNK;

		g_stage = 4;
		audsrv_wait_audio(n);
		audsrv_play_audio((char *)p + off, n);
		g_stage = 3;

		off += n;

		g_real    += n / bpf;
		g_sil_run  = 0;
		publish();
	}
}

/*
 * Nothing to decode. Pad with silence only if audsrv is about to run dry;
 * otherwise wait for the reader.
 */
static void starved(void)
{
	if (g_hz > 0 && audsrv_queued() < VA_LOW_WATER) {
		g_stage = 5;
		audsrv_wait_audio(AUDIO_CHUNK);
		audsrv_play_audio((char *)g_zero, AUDIO_CHUNK);

		g_sil_run += AUDIO_CHUNK / (2 * g_ch);
		g_underruns++;
		publish();
		return;                        /* paced by wait_audio */
	}

	g_stage = 6;
	net_snooze(16);
}

/*
 * Mute and release audsrv, from this thread, once per stop - leaving it
 * exactly as the music player leaves it at the end of a track: 44.1 kHz
 * stereo, stopped, at full volume.
 *
 * The music player restarts audsrv on every track with set_format, and
 * that has always worked - from its own state. Left at 48 kHz and volume
 * 0 by the film's sound, the music's first set_format had to change the
 * format of a stopped audsrv, and its worker hung there: music sat on
 * Buffering after any film until the console was reset.
 */
static void release(void)
{
	if (g_hz > 0) {
		struct audsrv_fmt_t fmt;

		audsrv_set_volume(0);          /* silent while it changes */

		fmt.freq     = 44100;
		fmt.bits     = 16;
		fmt.channels = 2;
		audsrv_set_format(&fmt);

		audsrv_stop_audio();
		audsrv_set_volume(MAX_VOLUME);
	}

	g_hz = 0;
	g_ch = 0;
	g_clock_ok = 0;
	g_stage = 0;
	g_idle = 1;
}

/*
 * The video's sound thread: waits for the first picture, decodes MP2
 * frames with libmad from the audio ring, feeds audsrv, and publishes the
 * clock after each chunk; plays silence while paused or starved. The only
 * caller of audsrv during video.
 */
static void worker(void *arg)
{
	(void)arg;

	for (;;) {
		g_loops++;

		if (!g_run) {
			if (!g_idle)
				release();

			net_snooze(20);
			continue;
		}

		/*
		 * Start with the picture, and with a cushion.
		 *
		 * Before the first picture is up there is nothing to keep in
		 * step with, and starting early would only make the picture
		 * chase the sound. The cushion is for a slow start.
		 */
		if (!g_started) {
			int enough = scr_video_audio_avail() >= VA_PREBUFFER ||
			             scr_video_source_done();

			if (!scr_video_first_shown() || !enough) {
				g_stage = scr_video_first_shown() ? 2 : 1;
				net_snooze(16);
				continue;
			}

			g_started = 1;
		}

		if (g_paused) {
			paused_idle();
			continue;
		}

		if (refill() <= 0) {
			starved();
			continue;
		}

		g_stage = 3;
		mad_stream_buffer(&g_stream, g_in, g_have);

		int decoded = 0;
		int bad     = 0;

		for (;;) {
			if (!g_run)
				break;

			if (mad_frame_decode(&g_frame, &g_stream) != 0) {
				/*
				 * Skip a bad frame, but not forever: libmad
				 * resyncs within a few, and an unbounded
				 * loop here, above the UI, freezes the
				 * console if it ever stops advancing.
				 */
				if (g_stream.error != MAD_ERROR_BUFLEN)
					g_errs++;

				if (MAD_RECOVERABLE(g_stream.error) && ++bad < 100)
					continue;

				break;                 /* wants more data */
			}

			bad = 0;

			mad_synth_frame(&g_synth, &g_frame);
			g_frames++;
			decoded++;

			/*
			 * The format is read from the stream. The request
			 * asks for 48 kHz stereo, but this survives a change.
			 */
			if (g_hz != (int)g_frame.header.samplerate) {
				struct audsrv_fmt_t fmt;

				fmt.freq     = g_frame.header.samplerate;
				fmt.bits     = 16;
				fmt.channels = (g_synth.pcm.channels > 1) ? 2 : 1;

				if (audsrv_set_format(&fmt) != 0) {
					snprintf(g_err, sizeof(g_err), "%d Hz: %s",
					         fmt.freq, audsrv_get_error_string());
					g_run = 0;
					break;
				}

				/* Back up from whatever the last stop left
				 * it at, which is zero. */
				audsrv_set_volume(MAX_VOLUME);

				g_hz = fmt.freq;
				g_ch = fmt.channels;
			}

			/*
			 * Start where the picture starts.
			 *
			 * The two streams rarely begin at the same instant:
			 * the sound's first frame can be some way before the
			 * first picture. Playing from there made the picture
			 * wait, or drop a burst to catch up - the stutter after
			 * every start. Samples before the first picture's PTS
			 * are skipped instead, so both begin together.
			 */
			unsigned int from = 0;
			unsigned int len  = g_synth.pcm.length;

			if (!g_aligned) {
				long long p0 = scr_video_audio_pts0();
				long long v0 = scr_video_first_vpts();
				long long at = p0 + g_decoded * 90000 / g_hz;
				long long to = at + (long long)len * 90000 / g_hz;

				if (p0 >= 0 && v0 >= 0 && to <= v0) {
					/* Wholly before the picture. */
					g_decoded += len;
					g_skip_w  += len;
					continue;
				}

				if (p0 >= 0 && v0 > at) {
					from      = (unsigned int)((v0 - at) * g_hz / 90000);
					g_skip_w += from;
				}

				/*
				 * The other way round: the sound's first frame
				 * comes AFTER the first picture. Playing it at
				 * once started the clock at the sound's time, so
				 * the clock ran that far ahead of every picture -
				 * and pictures more than two frames behind are
				 * dropped as late. On some files (still pictures,
				 * PAL episodes) the gap was just over that, and a
				 * steady share of pictures was dropped for the
				 * whole film; a seek, which starts both streams
				 * together, cured it. So play silence for the gap
				 * first and count it, as the sound would have
				 * been: the clock then starts at the picture.
				 * Capped at two seconds, for a broken stream.
				 */
				if (p0 >= 0 && v0 >= 0 && at > v0 && g_decoded == 0) {
					long long gap = (at - v0) * g_hz / 90000;

					if (gap > 2LL * g_hz)
						gap = 2LL * g_hz;

					g_skip_w -= gap;

					long long bytes = gap * 2 * g_ch;

					while (bytes > 0 && g_run) {
						int n = bytes > (long long)sizeof(g_zero)
						        ? (int)sizeof(g_zero) : (int)bytes;

						feed(g_zero, n);
						bytes -= n;
					}
				}

				g_aligned = 1;
			}

			g_decoded += len;

			/* Interleave into signed 16-bit and hand it over. */
			int ch   = g_synth.pcm.channels;
			int have = 0;

			for (unsigned int i = from; i < len; i++) {
				g_pcm[have++] = to_s16(g_synth.pcm.samples[0][i]);

				if (ch > 1 && g_ch > 1)
					g_pcm[have++] = to_s16(g_synth.pcm.samples[1][i]);

				if ((have + 2) * (int)sizeof(short) > VA_PCM) {
					feed(g_pcm, have * (int)sizeof(short));
					have = 0;
				}
			}

			if (have > 0)
				feed(g_pcm, have * (int)sizeof(short));
		}

		/*
		 * Nothing decoded: only part of a frame so far, or garbage
		 * libmad could not resync within. Garbage is thrown away so
		 * the next pass starts clean; either way, wait for more.
		 */
		if (decoded == 0) {
			if (bad >= 100) {
				g_have = 0;
				g_stream.next_frame = NULL;
			}

			starved();
		}
	}
}

/* Prepare a new stream's sound and start (or wake) the thread. */
int vaudio_start(void)
{
	g_err[0]   = '\0';
	g_frames   = 0;
	g_underruns = 0;
	g_errs     = 0;
	g_paused   = 0;
	g_decoded  = 0;
	g_aligned  = 0;
	g_skip_w   = 0;
	g_skip     = 0;
	g_stage    = 1;
	g_have     = 0;
	g_started  = 0;
	g_real     = 0;
	g_sil_run  = 0;
	g_clock_ok = 0;
	g_played   = 0;
	g_queued   = 0;

	memset(g_zero, 0, sizeof(g_zero));

	mad_stream_init(&g_stream);
	mad_frame_init(&g_frame);
	mad_synth_init(&g_synth);

	g_idle = 0;
	g_run  = 1;

	if (g_thread < 0) {
		extern void *_gp;
		ee_thread_t th;

		memset(&th, 0, sizeof(th));

		th.func             = worker;
		stack_guard_fill(g_stack);
		th.stack            = g_stack;
		th.stack_size       = sizeof(g_stack);
		th.gp_reg           = &_gp;
		th.initial_priority = VAUDIO_PRIO;

		g_thread = CreateThread(&th);
		freeze_register(g_thread, FZ_VSOUND);

		if (g_thread < 0) {
			snprintf(g_err, sizeof(g_err), "Could not start audio");
			g_run  = 0;
			g_idle = 1;
			return -1;
		}

		StartThread(g_thread, NULL);
	}

	return 0;
}

/*
 * Ask the sound thread to stop and wait (briefly) until it has released
 * audsrv itself.
 */
void vaudio_stop(void)
{
	/*
	 * Ask, and let the worker mute and stop audsrv itself.
	 *
	 * It checks g_run between chunks, and a chunk is about 10 ms, so it
	 * is out of audsrv_wait_audio and through release() almost at once.
	 * Muting from this thread instead - as before - called audsrv while
	 * the worker could be inside it.
	 */
	g_run = 0;

	for (int i = 0; i < 60 && !g_idle; i++)
		net_snooze(16);

	if (!g_idle) {
		/*
		 * The worker did not answer in a second. Better a risky call
		 * than a buzz that never ends; say so on screen.
		 */
		snprintf(g_err, sizeof(g_err), "Audio thread did not stop");
		audsrv_set_volume(0);
		audsrv_stop_audio();
		g_hz = 0;
		g_ch = 0;
		g_clock_ok = 0;
	}

	mad_synth_finish(&g_synth);
	mad_frame_finish(&g_frame);
	mad_stream_finish(&g_stream);
}


/* Samples skipped (or, negative, silence added) to start with the first
 * picture: for the DEBUG_UI readout. */
long long vaudio_skip(void) { return g_skip; }
