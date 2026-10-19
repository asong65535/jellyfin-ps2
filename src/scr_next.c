/*
 * The 5-second countdown to the next episode, shown on black after an
 * episode has truly ended.
 */

#include "screens.h"
#include "ui.h"
#include "jf.h"

#include <libpad.h>
#include <stdio.h>
#include <time.h>

/*
 * The countdown to the next episode.
 *
 * Only after an episode has truly ended - its sound played out and its
 * picture faded to black - and only when there is a next one. X starts it
 * at once, Circle stays on the episode just watched.
 */

#define NEXT_SECONDS 5

static clock_t g_start;

/* Whole seconds left in the countdown. */
static int left(void)
{
	clock_t gone = (clock() - g_start) / CLOCKS_PER_SEC;
	int s = NEXT_SECONDS - (int)gone;

	return s < 0 ? 0 : s;
}

/* Start the countdown. */
static void enter(void) { g_start = clock(); }

/*
 * X plays the next episode at once, Circle returns to the episode just
 * watched; at zero it starts by itself.
 */
static void input(u32 p)
{
	if (p & PAD_CIRCLE) {
		app_goto(SCR_DETAIL);          /* the episode just watched */
		return;
	}

	if ((p & PAD_CROSS) || left() == 0)
		app_run_job(job_play_next, "Loading next episode...");
}

/*
 * The countdown on black: 'Next episode', its number and title, and a bar
 * running down.
 */
static void draw(void)
{
	/* Black, as the fade left it, with the countdown on top. */
	ui_box(SCR_LEFT, SCR_TOP, SCR_FULL_W, SCR_FB_H,
	       GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));

	ui_text_c(SCR_W / 2, 120, 1.0f, C_DIM, "Next episode");

	char ep[32] = "";

	if (jf_next.season > 0 || jf_next.episode > 0)
		snprintf(ep, sizeof(ep), "S%02dE%02d", jf_next.season, jf_next.episode);

	if (ep[0])
		ui_text_c(SCR_W / 2, 150, 1.0f, C_LILAC, ep);

	/* Large if it fits on one line, else regular size. */
	if (ui_text_w(2.0f, jf_next.name) <= SCR_W - 120)
		ui_text_c(SCR_W / 2, 186, 2.0f, C_WHITE, jf_next.name);
	else
		ui_text_c(SCR_W / 2, 200, 1.0f, C_WHITE, jf_next.name);

	char t[32];
	int  s = left();

	snprintf(t, sizeof(t), "Starting in %d", s);
	ui_text_c(SCR_W / 2, 296, 1.0f, C_WHITE, t);

	/* A bar running down with the seconds. */
	float w = 240.0f;
	float f = (float)(clock() - g_start) / (float)(NEXT_SECONDS * CLOCKS_PER_SEC);

	if (f > 1.0f) f = 1.0f;
	ui_box((SCR_W - w) / 2, 326, w, 3, C_RULE);
	ui_box((SCR_W - w) / 2, 326, w * (1.0f - f), 3, C_PURPLE);

	static const BtnGlyph b[] = { BTN_CROSS, BTN_CIRCLE };
	static const char *l[] = { "Play now", "Cancel" };
	ui_hint_row(b, l, 2);
}

const Screen scr_next = { enter, input, draw };
