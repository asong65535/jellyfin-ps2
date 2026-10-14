#ifndef FREEZE_H
#define FREEZE_H

/*
 * The freeze safety net.
 *
 * A rare hang was seen on hardware while paused (4:3 films, not
 * reproducible on demand), with every button dead and only a power cycle
 * out. This turns any such hang into a recoverable error that names the
 * part responsible.
 *
 * The screen thread ticks freeze_tick() once per frame. A vertical-blank
 * hook - an interrupt, so it runs however busy the CPU is - watches the
 * ticks. If none come for two seconds, it finds the thread holding the
 * CPU and drops it below the screen thread, which can then run again: the
 * main loop sees freeze_code(), stops playback and says so, with a letter:
 *
 *   D video decoder   S video sound   N video network reader
 *   R background reports   M music decoder   P music network reader
 *
 * If it is the screen thread itself that is stuck (U), or nothing is
 * running because it waits for something that never comes (W), nothing can
 * draw a message: the screen turns a solid colour instead (yellow for U,
 * white for W), so there is still a clue.
 */
enum {
	FZ_UI = 0, FZ_DECODER, FZ_VSOUND, FZ_VREADER, FZ_REPORTS,
	FZ_MUSIC, FZ_MREADER, FZ_COUNT
};

void freeze_init(void);                     /* once, at start-up */
void freeze_register(int tid, int which);   /* after each CreateThread */
void freeze_tick(void);                     /* once per frame */

/*
 * Watch only while armed. Many screens block the screen thread on purpose
 * - loading pages and artwork, signing in, saving settings.txt - and a
 * pause of a couple of seconds there is not a freeze. So the net is armed
 * only during video playback proper, where the screen thread never waits,
 * and disarmed before anything there that does (stopping, seeking).
 */
void freeze_arm(int on);

/* The letter of a thread set aside since the last call, or 0. Once. */
char freeze_take(void);

#endif
