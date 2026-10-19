/*
 * A video's sound and the playback clock; see vaudio.c.
 */

#ifndef VAUDIO_H
#define VAUDIO_H

/*
 * Sound for the video player, and the clock the picture follows.
 *
 * Separate from audio.c, which serves the music player: the two have
 * different sources, different lifetimes, and only one can be running at a
 * time. What they share is libmad and audsrv, and the awkward lessons about
 * both - see the comments here and there.
 *
 * The source is the audio ring that scr_video's reader fills. Layer II
 * rather than layer III, which libmad decodes just the same.
 *
 * THE SOUND IS THE CLOCK. audsrv drains at the SPU2's own rate, so the
 * samples it has actually played say where the film is. The picture is shown
 * when that clock reaches its timestamp; the sound is never held, trimmed or
 * hurried to suit the picture.
 */

/* Start decoding. The source is scr_video's audio ring. */
int  vaudio_start(void);

/*
 * Stop, and wait for the decoder to leave libmad and audsrv.
 *
 * The worker mutes and stops audsrv itself: audsrv's RPC client is not safe
 * to call from two threads at once.
 */
void vaudio_stop(void);

/*
 * Where the sound is, at 90 kHz on the stream's own timeline: the first
 * audio PTS plus the samples audsrv has really played, moved on by the time
 * since that was measured (never past what is still queued).
 *
 * Returns 1 and writes *pts90 when the clock is running, 0 when it is not
 * (not started yet, or the decoder has stopped reporting).
 */
int  vaudio_clock90(long long *pts90);

/*
 * Pause in place: the worker feeds silence, which does not advance the
 * clock, so the picture holds too. Resuming continues from the very next
 * sample - no reload. Only the worker touches audsrv.
 */
void vaudio_pause(int on);

/* Diagnostics: frames decoded, silence chunks padded in, and errors. */
int         vaudio_frames(void);
int         vaudio_underruns(void);

/*
 * Where the worker is, for the on-screen readout:
 *   0 idle (stopped)      1 waiting for the first picture
 *   2 waiting for buffer  3 decoding
 *   4 inside audsrv wait/play   5 padding silence   6 sleeping, starved
 * plus the format, decode errors and whether the clock is published.
 */
const char *vaudio_debug(void);

/* 1 while the thread's stack has never overflowed. */
int vaudio_stack_ok(void);

/* Samples skipped to meet the first picture, negative for silence added
 * before a late-starting sound. For the DEBUG_UI readout. */
long long vaudio_skip(void);

#endif
