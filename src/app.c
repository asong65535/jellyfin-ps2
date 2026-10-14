#include "app.h"
#include "audio.h"
#include "freeze.h"
#include "screens.h"
#include "ui.h"
#include "jf.h"

#include <libpad.h>
#include <stdio.h>
#include <string.h>

static ScreenId    g_screen = SCR_CONNECT;
static JobFn       g_job    = NULL;
static char g_msg[80] = "";

static char g_res_title[64] = "";
static char g_res_body[320] = "";
static int  g_res_ok        = 0;

/* ------------------------------------------------------------------ */
/* Busy and result are owned by the framework, not by a feature file.  */
/* ------------------------------------------------------------------ */

/*
 * Deliberately the same furniture as the connect page, minus the field and
 * the keyboard, so a page transition does not look like a different app.
 */
static void busy_draw(void)
{
	ui_waves();
	ui_brand();
	ui_text_c(SCR_W / 2, 176, 2.0f, C_WHITE, g_msg);
}

static void result_input(u32 p)
{
	if (p & PAD_CROSS)
		app_goto(SCR_CONNECT);

	if (p & PAD_START) {
		if (g_res_ok) app_run_job(job_load_users, "Loading users...");
		else          app_run_job(job_connect,    "Connecting...");
	}
}

static void result_draw(void)
{
	ui_waves();
	ui_brand();

	ui_text_c(SCR_W / 2, 140, 2.0f, g_res_ok ? C_OK : C_ERR, g_res_title);
	/* max_w is in pixels, not characters. It was 62, which wrapped after
	 * about four letters. */
	ui_wrap(50, 196, 24, 1.0f, C_WHITE, g_res_body, SCR_W - 100, 8);

	if (g_res_ok) {
		static const BtnGlyph b[] = { BTN_START, BTN_CROSS };
		static const char *l[] = { "Browse library", "Back" };
		ui_hint_row(b, l, 2);
	} else {
		static const BtnGlyph b[] = { BTN_CROSS, BTN_START };
		static const char *l[] = { "Back", "Retry" };
		ui_hint_row(b, l, 2);
	}
}

static const Screen scr_busy   = { NULL, NULL,         busy_draw   };
static const Screen scr_result = { NULL, result_input, result_draw };

/* ------------------------------------------------------------------ */

static const Screen *g_screens[SCR_COUNT] = {
	[SCR_CONNECT] = &scr_connect,
	[SCR_BUSY]    = &scr_busy,
	[SCR_RESULT]  = &scr_result,
	[SCR_USERS]   = &scr_users,
	[SCR_LOGIN]   = &scr_login,
	[SCR_HOME]    = &scr_home,
	[SCR_NEXT]    = &scr_next,
	[SCR_BROWSE]  = &scr_browse,
	[SCR_DETAIL]  = &scr_detail,
	[SCR_TRACKS]  = &scr_tracks,
	[SCR_PLAYER]  = &scr_player,
	[SCR_SEARCH]  = &scr_search,
	[SCR_VIDEO]   = &scr_video,
};

void app_goto(ScreenId id)
{
	freeze_arm(0);                 /* the video screen re-arms itself */
	g_screen = id;
	if (g_screens[id]->enter)
		g_screens[id]->enter();
}

void app_progress(const char *msg)
{
	snprintf(g_msg, sizeof(g_msg), "%s", msg);

	ui_begin();
	busy_draw();
	ui_end();
}

void app_run_job(JobFn fn, const char *msg)
{
	freeze_arm(0);                 /* jobs block the screen on purpose */
	g_job    = fn;
	snprintf(g_msg, sizeof(g_msg), "%s", msg);
	g_screen = SCR_BUSY;      /* no enter hook, so no recursion risk */
}

static void set_result(int ok, const char *title, const char *body)
{
	g_res_ok = ok;
	snprintf(g_res_title, sizeof(g_res_title), "%s", title);
	snprintf(g_res_body,  sizeof(g_res_body),  "%s", body);
	g_screen = SCR_RESULT;
}

void app_ok  (const char *t, const char *b) { set_result(1, t, b); }
void app_fail(const char *t, const char *b) { set_result(0, t, b); }

int app_result_ok(void) { return g_res_ok; }

/*
 * Always the connect screen first: nothing is usable until the server is
 * reached and someone has signed in on the user page.
 */
void app_init(void)
{
	app_goto(SCR_CONNECT);
}

void app_frame(u32 pressed)
{
	const Screen *s = g_screens[g_screen];

	if (s->input)
		s->input(pressed);

	ui_begin();
	g_screens[g_screen]->draw();     /* re-read: input may have moved us */
	ui_end();

	/*
	 * Blocking work runs only after the frame is on screen, so the user
	 * sees "Connecting..." rather than a frozen previous page.
	 */
	if (g_screen == SCR_BUSY && g_job) {
		JobFn fn = g_job;
		g_job = NULL;
		fn();
	}
}
/*
 * A thread the freeze safety net set aside (freeze.h). During video, stop
 * it and go back to the film's page with the code; during music, stop the
 * song. Elsewhere it was a background thread, and there is nothing to
 * stop: the app carries on.
 */
void app_freeze(char code)
{
	char msg[64];

	snprintf(msg, sizeof(msg), "Playback stopped unexpectedly (%c)", code);

	if (g_screen == SCR_VIDEO || g_screen == SCR_NEXT) {
		scr_video_abort(code);
		scr_detail_set_notice(msg);
		app_goto(SCR_DETAIL);
		return;
	}

	if (g_screen == SCR_PLAYER && (code == 'M' || code == 'P')) {
		audio_stop();
		app_fail("Music stopped unexpectedly", msg);
	}
}
