#ifndef AUDIO_H_INCLUDED
#define AUDIO_H_INCLUDED

/*
 * Streaming audio.
 *
 * The file is streamed untranscoded and decoded here with libmad, then handed
 * to audsrv, the SDK's SPU2 driver. audsrv_wait_audio blocks until the buffer
 * has room, which paces the decode and, behind it, the download, so there is
 * no ring buffer and no clock to keep.
 *
 * Asking Jellyfin for PCM would have avoided needing a decoder, but its PCM
 * path is broken: the ffmpeg command it builds carries a bare "-ar" with no
 * value, and every wav request returns 500. Serving the original file is
 * better anyway: no transcode for the server, a fifth of the bandwidth, and
 * a real Content-Length.
 *
 * Two worker threads. One reads the socket into a ring buffer, the other
 * decodes from the ring and feeds the SPU2, so a slow read can never stall
 * playback. The UI thread only sets flags, so nothing on screen ever blocks.
 *
 * Video would want the same arrangement.
 */

typedef enum {
	AUDIO_STOPPED,
	AUDIO_BUFFERING,
	AUDIO_PLAYING,
	AUDIO_PAUSED,
	AUDIO_ERROR
} AudioState;

/* Loads the IOP modules and starts the worker. Once, at boot. */
int  audio_init(void);

/*
 * Begin a track. Returns immediately; watch audio_state() for progress.
 * Any previous track is stopped first.
 */
void audio_play(const char *host, int port, const char *path,
                const char *token);

void audio_pause(void);
void audio_resume(void);

/* Stops and tears down the connection. Safe to call when already stopped. */
void audio_stop(void);

AudioState  audio_state(void);

/* Seconds of audio handed to the SPU2 so far. */
int         audio_position(void);

/*
 * Diagnostics: PCM bytes per second actually delivered to the SPU2, and the
 * rate needed. Delivered below needed means the hardware is running dry,
 * which sounds like noise over the music and makes the progress bar crawl.
 */
/* PCM bytes delivered so far. The caller times it against vsync, which is a
 * known 60 Hz; cpu_ticks counts CPU cycles and is not a clock. */
int  audio_bytes(void);

/*
 * Frames decoded, and frames libmad refused. A refused frame is roughly 26 ms
 * of silence, which is audible. Errors climbing while the delivered rate
 * matches the required one means the data is damaged rather than late.
 */
int  audio_frames(void);

/*
 * Where the worker is, for the music screen while it says Buffering:
 *   step 1 connecting   2 reading   3 decoding   4 inside audsrv_set_format
 *   5 playing           6 inside audsrv_wait_audio
 *   7 closing the socket   8 inside audsrv_stop_audio   9 track stopped
 * plus bytes received and frames decoded. A step that never moves says
 * which call is stuck.
 */
const char *audio_debug(void);
int  audio_errors(void);

/* MP3 bytes discarded by the carry-forward. Should always be 0. */
int  audio_dropped(void);

/*
 * Errors split by kind. LOSTSYNC is routine, reported on every buffer refill
 * while libmad hunts for a frame header, and costs nothing. Anything else is
 * a frame that decoded to silence.
 */
int  audio_err_sync(void);
int  audio_err_real(void);
int  audio_last_err(void);
int  audio_rate_needed(void);

/* Sample rate and channel count taken from the first decoded frame. */
void audio_format(int *hz, int *channels);

/* Why the last attempt failed, for the error line. */
const char *audio_error(void);

/*
 * True between a track finishing by itself and the next one starting. Lets
 * the player tell "stopped because it ended" from "stopped because you
 * pressed stop".
 */
int  audio_ended(void);

#endif  /* AUDIO_H_INCLUDED */