/*
 * scr_usb.c - the USB tab.
 *
 * A placeholder for now: the tab exists in the row, and its screen says
 * the feature is coming. Planned: browsing a USB drive (a PSP's memory
 * stick, a Walkman in drive mode, a USB stick) and playing its music with
 * the existing music player.
 *
 * Navigation matches the other tabs: Left goes to SEARCH, Right wraps
 * round to HOME, Circle goes back to the users like HOME does.
 */

#include "screens.h"
#include "app.h"
#include "jf.h"
#include "ui.h"
#include "config.h"

#include <libpad.h>

/* Nothing to set up yet. */
static void enter(void)
{
}

/*
 * Left: the Search tab - its results if a query is set, else the search
 * keyboard (as HOME used to do). Right: round to HOME. Circle: the users.
 */
static void input(u32 p)
{
	if (p & PAD_LEFT) {
		jf_tab         = JF_TAB_SEARCH;
		jf_start_index = 0;

		if (jf_query[0])
			app_run_job(job_load_items, "Loading...");
		else
			app_goto(SCR_SEARCH);
		return;
	}

	if (p & PAD_RIGHT) {
		app_run_job(job_load_home, "Loading...");
		return;
	}

	if (p & PAD_CIRCLE)
		app_run_job(job_load_users, "Loading users...");
}

/* Header and tabs as on HOME, and the work-in-progress note. */
static void draw(void)
{
	ui_waves();

	ui_text(24, 16, 1.0f, C_WHITE, "Jellyfin");
	ui_text(24 + ui_text_w(1.0f, "Jellyfin") + 6, 16, 1.0f, C_PURPLE, "PS2");
	ui_text(SCR_W - 24 - ui_text_w(1.0f, jf_user_name), 16, 1.0f, C_DIM,
	        jf_user_name);

	scr_draw_tabs(SCR_TAB_USB, 1);

	ui_text_c(SCR_W / 2, 200, 1.0f, C_WHITE, "Work in progress");
	ui_text_c(SCR_W / 2, 230, 1.0f, C_DIM,
	          "Music from USB devices: coming later.");

	static const BtnGlyph b[] = { BTN_DPAD, BTN_CIRCLE };
	static const char *l[] = { "Move", "Users" };
	ui_hint_row(b, l, 2);
}

const Screen scr_usb = { enter, input, draw };
