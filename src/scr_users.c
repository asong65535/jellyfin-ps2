#include "screens.h"
#include "ui.h"
#include "jf.h"
#include "settings.h"

#include <libpad.h>
#include <stdio.h>
#include <string.h>

/*
 * "Who is watching?" - shown every time, after connecting.
 *
 * One tile per user on the server's login page, plus users who signed in
 * here through "Other user" (hidden from that page), plus "Other user"
 * itself. A tile says whether this PS2 holds a sign-in for that user: those
 * start at once, others are asked for a password once, if they have one.
 */

#define MAX_TILES  (JF_MAX_USERS + SETTINGS_MAX_USERS + 1)
#define VISIBLE    4

typedef struct {
	char name[64];
	int  has_pw;
	int  saved;      /* a sign-in for this user is in settings.txt */
	int  other;      /* the "Other user" tile */
} Tile;

static Tile g_tiles[MAX_TILES];
static int  g_count;
static int  g_sel;
static int  g_first;
static char g_pick[64];     /* the user a job is working on */
static int  g_pick_pw;

static void build(void)
{
	g_count = 0;

	for (int i = 0; i < jf_user_count && g_count < MAX_TILES - 1; i++) {
		Tile *t = &g_tiles[g_count++];

		memset(t, 0, sizeof(*t));
		snprintf(t->name, sizeof(t->name), "%s", jf_users[i].name);
		t->has_pw = jf_users[i].has_pw;
		t->saved  = settings_user(t->name) != NULL;
	}

	/* Signed in here, but not on the public list: hidden accounts. */
	for (int i = 0; i < settings.user_count && g_count < MAX_TILES - 1; i++) {
		int listed = 0;

		for (int k = 0; k < g_count; k++)
			if (strcmp(g_tiles[k].name, settings.users[i].name) == 0)
				listed = 1;

		if (listed)
			continue;

		Tile *t = &g_tiles[g_count++];

		memset(t, 0, sizeof(*t));
		snprintf(t->name, sizeof(t->name), "%s", settings.users[i].name);
		t->has_pw = 1;
		t->saved  = 1;
	}

	Tile *o = &g_tiles[g_count++];

	memset(o, 0, sizeof(*o));
	snprintf(o->name, sizeof(o->name), "Other user");
	o->other = 1;
}

/* After connecting: nobody is signed in until a tile is chosen. */
void job_load_users(void)
{
	jf_sign_out_local();

	if (jf_load_users() < 0) {
		app_fail(jf_err_title(), jf_err_body());
		return;
	}

	app_goto(SCR_USERS);
}

static void open_library(void)
{
	jf_start_index = 0;
	app_run_job(job_load_home, "Loading...");
}

/* A kept sign-in: check it, and ask for the password if it was ended. */
static void job_resume(void)
{
	int rc = jf_resume_user(g_pick);

	if (rc == 0) {
		open_library();
		return;
	}

	if (rc == -2) {
		login_open(g_pick, 0, "That sign-in has ended. Sign in again.");
		return;
	}

	app_fail(jf_err_title(), jf_err_body());
}

/* No password on the account: sign straight in. */
static void job_no_password(void)
{
	int rc = jf_sign_in(g_pick, "");

	if (rc == 0) {
		open_library();
		return;
	}

	if (rc == -2) {                 /* it has one after all */
		login_open(g_pick, 0, NULL);
		return;
	}

	app_fail(jf_err_title(), jf_err_body());
}

static void job_sign_out(void)
{
	jf_sign_out(g_pick);
	app_run_job(job_load_users, "Loading users...");
}

static void enter(void)
{
	build();

	if (g_sel >= g_count)
		g_sel = 0;
	if (g_first > g_sel)
		g_first = g_sel;
}

static void input(u32 p)
{
	if (p & PAD_LEFT)  { if (g_sel > 0) g_sel--; }
	if (p & PAD_RIGHT) { if (g_sel < g_count - 1) g_sel++; }

	if (g_sel < g_first)            g_first = g_sel;
	if (g_sel >= g_first + VISIBLE) g_first = g_sel - VISIBLE + 1;

	if (p & PAD_CIRCLE) {
		app_goto(SCR_CONNECT);
		return;
	}

	Tile *t = &g_tiles[g_sel];

	if ((p & PAD_TRIANGLE) && t->saved) {
		snprintf(g_pick, sizeof(g_pick), "%s", t->name);
		app_run_job(job_sign_out, "Signing out...");
		return;
	}

	if (p & PAD_CROSS) {
		snprintf(g_pick, sizeof(g_pick), "%s", t->name);
		g_pick_pw = t->has_pw;

		if (t->other)
			login_open("", 1, NULL);
		else if (t->saved)
			app_run_job(job_resume, "Signing in...");
		else if (!t->has_pw)
			app_run_job(job_no_password, "Signing in...");
		else
			login_open(t->name, 0, NULL);
	}
}

/* A filled disc, from horizontal strips. */
static void disc(float cx, float cy, int r, u64 col)
{
	for (int dy = -r; dy < r; dy += 2) {
		int y = dy + 1;
		int h = 0;

		while ((h + 1) * (h + 1) + y * y <= r * r)
			h++;

		ui_box(cx - h, cy + dy, 2 * h, 2, col);
	}
}

static void lock(float x, float y, u64 col)
{
	ui_box(x + 2, y,      8, 2, col);      /* the shackle */
	ui_box(x + 1, y + 1,  2, 5, col);
	ui_box(x + 9, y + 1,  2, 5, col);
	ui_box(x,     y + 6, 12, 9, col);      /* the body */
}

static u64 tile_colour(int i)
{
	static const u64 c[] = {
		GS_SETREG_RGBAQ(0x6B, 0x4F, 0xC8, 0x80, 0x00),
		GS_SETREG_RGBAQ(0x2F, 0x8F, 0xB8, 0x80, 0x00),
		GS_SETREG_RGBAQ(0xC0, 0x5A, 0x7A, 0x80, 0x00),
		GS_SETREG_RGBAQ(0x3E, 0x9E, 0x6E, 0x80, 0x00),
		GS_SETREG_RGBAQ(0xC0, 0x8A, 0x3A, 0x80, 0x00),
	};

	return c[i % 5];
}

static void draw(void)
{
	ui_waves();
	ui_header("Who is watching?", jf_host());

	const float tw = 124.0f, th = 150.0f, gap = 16.0f, y0 = 96.0f;
	int shown = g_count - g_first < VISIBLE ? g_count - g_first : VISIBLE;
	float total = shown * tw + (shown - 1) * gap;
	float x0 = (SCR_W - total) / 2;

	for (int k = 0; k < shown; k++) {
		int   i  = g_first + k;
		Tile *t  = &g_tiles[i];
		float x  = x0 + k * (tw + gap);
		float cx = x + tw / 2, cy = y0 + 52;

		if (i == g_sel) {
			ui_round(x, y0, tw, th, 8, C_ROWSEL);
			ui_box(x + 10, y0 + th - 4, tw - 20, 3, C_PURPLE);
		}

		if (t->other) {
			disc(cx, cy, 38, C_DIM);
			disc(cx, cy, 36, i == g_sel ? C_ROWSEL : C_BG);
			ui_box(cx - 2, cy - 14, 4, 28, C_DIM);
			ui_box(cx - 14, cy - 2, 28, 4, C_DIM);
		} else {
			char initial[2] = { t->name[0] ? t->name[0] : '?', '\0' };

			disc(cx, cy, 38, tile_colour(i));
			ui_text_c(cx, cy - 22, 2.0f, C_WHITE, initial);
		}

		/* Names longer than the tile are cut, not wrapped. */
		char shown_name[64];

		snprintf(shown_name, sizeof(shown_name), "%s", t->name);
		while (shown_name[0] && ui_text_w(1.0f, shown_name) > tw - 6)
			shown_name[strlen(shown_name) - 1] = '\0';

		ui_text_c(cx, y0 + 98, 1.0f, i == g_sel ? C_WHITE : C_LILAC,
		          shown_name);

		if (t->other) {
			ui_text_c(cx, y0 + 120, 1.0f, C_DIM, "Type name");
		} else if (t->saved) {
			ui_text_c(cx, y0 + 120, 1.0f, C_OK, "Signed in");
		} else if (t->has_pw) {
			float w = ui_text_w(1.0f, "Password") + 16;

			lock(cx - w / 2, y0 + 123, C_DIM);
			ui_text(cx - w / 2 + 16, y0 + 120, 1.0f, C_DIM, "Password");
		}
	}

	/* More tiles off to either side. */
	if (g_first > 0)
		ui_text(16, y0 + 40, 1.0f, C_DIM, "<");
	if (g_first + VISIBLE < g_count)
		ui_text(SCR_W - 26, y0 + 40, 1.0f, C_DIM, ">");

	ui_text_c(SCR_W / 2, 268, 1.0f, C_DIM,
	          "Signed-in users start at once. Others sign in once.");

	if (g_tiles[g_sel].saved) {
		static const BtnGlyph b[] = { BTN_CROSS, BTN_TRIANGLE, BTN_CIRCLE };
		static const char *l[] = { "Select", "Sign out", "Back" };
		ui_hint_row(b, l, 3);
	} else {
		static const BtnGlyph b[] = { BTN_CROSS, BTN_CIRCLE };
		static const char *l[] = { "Select", "Back" };
		ui_hint_row(b, l, 2);
	}
}

const Screen scr_users = { enter, input, draw };
