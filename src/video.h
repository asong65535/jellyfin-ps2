#ifndef VIDEO_H
#define VIDEO_H

#include <gsKit.h>

/*
 * MPEG-2 playback through the IPU.
 *
 * The decoder is libmpeg, which wraps the IPU and hands back RGBA32 pictures
 * as a grid of 16x16 macroblocks. Those go into a gsKit texture, which the
 * UI then draws like any other, so the rest of the app is unaffected and
 * overlays remain possible.
 *
 * VRAM is the constraint: a 640x448 texture is 1.25 MB against 0.97 MB free
 * with artwork resident, so video_begin releases the poster slots first and
 * video_end gives them back.
 *
 * A NOTE ON SIZE
 *
 * Stock libmpeg cannot decode a picture with more than 1023 macroblocks:
 * the continuation pass in its colour-conversion handler programs the two
 * DMA channels with each other's transfer lengths, and hangs. 640x448 is
 * 1120 and needs that pass. The patched library mounted by
 * docker-compose.yml fixes it. Built without that, keep pictures at or under
 * 1023 macroblocks - 576x432 is 972 and is exactly 4:3.
 */

/* Release the poster slots and prepare to decode. Returns 0 on failure. */
int  video_begin(GSGLOBAL *gs);

/*
 * Collect a decoded picture, and ask for the next when one is due.
 *
 * Decoding happens on its own thread, so this never blocks. Call it every
 * frame and pass due only when the next picture should be started: the
 * collection must not be paced, or finished pictures wait to be picked up
 * and the frame rate halves.
 *
 *   2  still working; the previous frame remains on screen
 *   1  a new frame has landed on the texture
 *   0  the stream ended
 *  -1  something failed; video_error says what
 */
int  video_step(int due);

/*
 * True when a single picture has taken absurdly long.
 *
 * MPEG_Picture has several unbounded spins inside it, and a decoder left in
 * a bad state sits in one forever. On the UI thread that froze the console
 * outright; on its own thread it becomes this, and the screen can say so.
 */
int  video_stalled(void);

/* Draw the most recent frame, scaled to fit the box. */
void video_draw(GSGLOBAL *gs, float x, float y, float w, float h);

/* Stop, and give the poster slots their VRAM back. */
void video_end(GSGLOBAL *gs);

/*
 * Stop the decoder but keep the texture, and the last picture in it, for
 * a pause or a seek: the picture stays on screen while the next stream
 * loads, and video_begin reuses the same VRAM. video_end (or
 * video_release) gives it back when playback really ends.
 */
void video_end_keep(GSGLOBAL *gs);

/* Give back a texture kept by video_end_keep, if any. */
void video_release(GSGLOBAL *gs);

/* A picture is in the texture and can be drawn. */
int  video_has_picture(void);

/* 1 while the decoder thread's stack has never overflowed. */
int  video_stack_ok(void);

/*
 * Give the VRAM back without touching the decoder.
 *
 * For when it is wedged: MPEG_Destroy would spin waiting for something that
 * will never happen, so the library is abandoned and only our own resources
 * are recovered. Without this every stalled playback leaked its 1.25 MB
 * texture and the third one ran out of VRAM.
 */
void video_abandon(GSGLOBAL *gs);

const char *video_error(void);

/* Where the texture landed, as "vram NNNN KB". Watching this between films
 * is how a leak shows itself before it becomes a refusal. */
const char *video_info(void);

/*
 * The worker's state, for telling where a stop is stuck: whether the decoder
 * is still inside a picture, or the UI thread is waiting on libmpeg.
 */
const char *video_debug(void);

/*
 * The picture's size, once the sequence header has been read. Zero before
 * that. Used to letterbox rather than stretch.
 */
int  video_width(void);
int  video_height(void);

/*
 * The real picture size, when the coded one is larger.
 *
 * MPEG-2 codes in whole macroblocks, so a 640x346 picture arrives as 640x352
 * with six rows of whatever the encoder left there. Pass zero to use the
 * coded size as it comes.
 */
void video_set_crop(int w, int h);

/* Milliseconds per frame from the sequence header, for pacing. */
int  video_ms_per_frame(void);

/* Feed the decoder. Set before video_begin: returns bytes read, or 0 at the
 * end of the stream. */
void video_set_source(int (*read_cb)(void *buf, int len));

/*
 * PLAYBACK PACED BY THE SOUND (VIDEO_AUDIO_ENABLE)
 *
 * The decoder works one picture ahead: as soon as a picture has been shown
 * or dropped, the next one is decoded and then held until the caller says
 * what to do with it. Each held picture carries its presentation time, so
 * the caller can compare it with the audio clock and show it when due, or
 * drop it when hopelessly late. Nothing here knows about the clock.
 */

/*
 * Where the timestamps come from.
 *
 * Called after every 2 KB block is gathered for the IPU. Returns the newest
 * PTS (90 kHz) whose PES started inside the bytes just read, or -1 if none
 * did. libmpeg stamps each picture with the latest value it was given.
 * NULL (the file source) leaves pictures without timestamps, and they are
 * then spaced one frame apart.
 */
void video_set_pts_source(long long (*pts_cb)(void));

/* The frame duration at 90 kHz, for pictures that carry no usable PTS. */
void video_set_frame90(int ticks);

/*
 * Is a decoded picture waiting? 1 and its PTS (90 kHz) if so.
 */
int  video_peek(long long *pts90);

/*
 * Act on the waiting picture, and keep the decoder busy.
 *
 *   act  1  show it: upload to the texture, then decode the next
 *       -1  drop it unseen, then decode the next
 *        0  keep holding it
 *
 * Returns as video_step does - 1 a frame landed, 2 nothing new, 0 the
 * stream ended, -1 an error - plus 3 when a picture was dropped.
 */
int  video_step_sync(int act);

/*
 * After the end-of-stream watchdog rescued a stuck decoder: what it found,
 * as text, once. "" otherwise.
 */
const char *video_take_stuck_report(void);

/* Decode no further pictures: the stream ends after the current one. */
void video_hold(void);

/* The freeze safety net set the decoder aside: replace it at video_end. */
void video_mark_stuck(void);

#endif