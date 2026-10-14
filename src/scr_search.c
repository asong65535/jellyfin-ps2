#include "screens.h"
#include "ui.h"
#include "osk.h"
#include "jf.h"

#include <libpad.h>
#include <stdio.h>
#include <string.h>

/*
 * Search entry. The same furniture as the URL screen, reusing the on-screen
 * keyboard bound to jf_query instead. START runs the search and returns to
 * the browse grid.
 */

static void enter(void)
{
	osk_bind(jf_query, sizeof(jf_query));
}

static void run_search(void)
{
	jf_tab         = JF_TAB_SEARCH;
	jf_start_index = 0;
	app_run_job(job_load_items, "Searching...");
}

static void input(u32 p)
{
	if (osk_input(p) || (p & PAD_START)) {
		if (jf_query[0]) run_search();
		else             app_goto(SCR_BROWSE);
		return;
	}

	if (p & PAD_CIRCLE)
		app_goto(SCR_BROWSE);
}

static void draw(void)
{
	ui_waves();

	ui_text(24, 16, 1.0f, C_WHITE, "Jellyfin");
	ui_text(24 + ui_text_w(1.0f, "Jellyfin") + 6, 16, 1.0f, C_PURPLE, "PS2");

	/* Names its scope, so an empty result for an album reads as expected
	 * rather than as a fault. */
	ui_text_c(SCR_W / 2, 60, 1.0f, C_WHITE, "Search movies");
	ui_box(70, 88, SCR_W - 140, 1, C_RULE);

	const float fx = 90.0f, fw = SCR_W - 180.0f;
	ui_round(fx, 120, fw, 30, 4, C_FIELD);
	ui_box  (fx, 150, fw,  3,    C_FIELDLINE);

	const char *q = jf_query;
	float avail = fw - 28.0f;
	while (*q && ui_text_w(1.0f, q) > avail)
		q++;

	if (*jf_query)
		ui_text(fx + 14, 124, 1.0f, C_LILAC, q);
	else
		ui_text(fx + 14, 124, 1.0f, C_RULE, "title");

	ui_box(fx + 14 + ui_text_w(1.0f, q) + 1, 125, 2, 20, C_WHITE);

	osk_draw();

	{
		static const BtnGlyph b[] = { BTN_CROSS, BTN_SQUARE,
		                              BTN_START, BTN_CIRCLE };
		static const char *l[] = { "Select", "Delete", "Search", "Back" };
		ui_hint_row(b, l, 4);
	}
}

const Screen scr_search = { enter, input, draw };