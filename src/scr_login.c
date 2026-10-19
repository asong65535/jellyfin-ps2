/*
 * Signing in: a name (for Other user) and a password on the on-screen
 * keyboard. The password is sent once and never kept.
 */

#include "screens.h"
#include "ui.h"
#include "osk.h"
#include "jf.h"

#include <libpad.h>
#include <stdio.h>
#include <string.h>

/*
 * Signing in: a name (for "Other user"), then a password, on the same
 * keyboard as the server address. The password is shown as blocks, sent
 * once, and never kept: what is kept is the server's access token.
 */

static char g_name[64];
static char g_pw[64];
static int  g_step;            /* 0 the name, 1 the password */
static int  g_ask_name;        /* came from "Other user" */
static char g_note[96];        /* a wrong password, an ended sign-in */
static int  g_keep;            /* re-entering after a failed attempt */

static void bind(void)
{
	if (g_step == 0)
		osk_bind(g_name, sizeof(g_name));
	else
		osk_bind(g_pw, sizeof(g_pw));
}

/*
 * Open the sign-in screen for name (or ask for a name first), with an
 * optional red note.
 */
void login_open(const char *name, int ask_name, const char *note)
{
	snprintf(g_name, sizeof(g_name), "%s", name ? name : "");
	snprintf(g_note, sizeof(g_note), "%s", note ? note : "");
	memset(g_pw, 0, sizeof(g_pw));

	g_ask_name = ask_name;
	g_step     = ask_name ? 0 : 1;
	g_keep     = 1;
	app_goto(SCR_LOGIN);
}

/*
 * Job: sign in; on success go to HOME, on a wrong password come back here
 * with a note. The password is wiped either way.
 */
static void job_login(void)
{
	int rc = jf_sign_in(g_name, g_pw);

	memset(g_pw, 0, sizeof(g_pw));       /* not kept, in any form */

	if (rc == 0) {
		jf_start_index = 0;
		app_run_job(job_load_home, "Loading...");
		return;
	}

	if (rc == -2) {
		snprintf(g_note, sizeof(g_note), "Wrong name or password.");
		g_step = 1;
		g_keep = 1;
		app_goto(SCR_LOGIN);
		return;
	}

	app_fail(jf_err_title(), jf_err_body());
}

/*
 * Start clean unless coming back from a failed attempt, and bind the
 * keyboard.
 */
static void enter(void)
{
	if (!g_keep) {                /* reached some other way: start clean */
		g_note[0] = '\0';
		memset(g_pw, 0, sizeof(g_pw));
	}

	g_keep = 0;
	bind();
}

/*
 * Enter/START: from the name go to the password, from the password sign
 * in.
 */
static void submit(void)
{
	if (g_step == 0) {
		if (!g_name[0])
			return;

		g_step    = 1;
		g_note[0] = '\0';
		bind();
		return;
	}

	app_run_job(job_login, "Signing in...");
}

/* The keyboard; Circle goes back a step or to the users. */
static void input(u32 p)
{
	if (osk_input(p) || (p & PAD_START)) {
		submit();
		return;
	}

	if (p & PAD_CIRCLE) {
		memset(g_pw, 0, sizeof(g_pw));

		if (g_step == 1 && g_ask_name) {   /* back to the name */
			g_step = 0;
			bind();
		} else {
			app_goto(SCR_USERS);
		}
	}
}

/*
 * The sign-in screen: label, the name or password field (blocks, not
 * letters), the keyboard.
 */
static void draw(void)
{
	ui_waves();
	ui_header("Sign in", NULL);

	if (g_note[0])
		ui_text_c(SCR_W / 2, 80, 1.0f, C_ERR, g_note);

	char label[96];

	if (g_step == 0)
		snprintf(label, sizeof(label), "Name:");
	else
		snprintf(label, sizeof(label), "Password for %s:", g_name);

	ui_text_c(SCR_W / 2, 116, 1.0f, C_WHITE, label);

	const float fx = 90.0f, fw = SCR_W - 180.0f;

	ui_round(fx, 148, fw, 30, 4, C_FIELD);
	ui_box  (fx, 178, fw,  3,    C_FIELDLINE);

	float caret;

	if (g_step == 0) {
		const char *u = g_name;

		while (*u && ui_text_w(1.0f, u) > fw - 28.0f)
			u++;

		ui_text(fx + 14, 152, 1.0f, C_LILAC, u);
		caret = fx + 14 + ui_text_w(1.0f, u) + 1;
	} else {
		/* A block per character: the length shows, the password does not. */
		int n = strlen(g_pw);
		int max = (int)((fw - 28.0f) / 14.0f);

		if (n > max)
			n = max;

		for (int i = 0; i < n; i++)
			ui_box(fx + 14 + i * 14, 158, 9, 9, C_LILAC);

		caret = fx + 14 + n * 14 + 1;
	}

	ui_box(caret, 153, 2, 20, C_WHITE);

	osk_draw();

	{
		static const BtnGlyph b[] = { BTN_CROSS, BTN_SQUARE,
		                              BTN_START, BTN_CIRCLE };
		static const char *l0[] = { "Select", "Delete", "Next", "Back" };
		static const char *l1[] = { "Select", "Delete", "Sign in", "Back" };
		ui_hint_row(b, g_step == 0 ? l0 : l1, 4);
	}
}

const Screen scr_login = { enter, input, draw };
