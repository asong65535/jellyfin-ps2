/*
 * The connect screen: the server address on the on-screen keyboard, the
 * PS2's own address, and settings.txt notices. Connecting leads to the
 * user page.
 */

#include "screens.h"
#include "config.h"
#include "ui.h"
#include "osk.h"
#include "jf.h"
#include "mods.h"
#include "net.h"
#include "settings.h"

#include <libpad.h>
#include <stdio.h>
#include <string.h>

/*
 * URL entry. Laid out to match the Jellyfin web client: centred wordmark,
 * a rule, a filled field with a bright underline, then the keyboard.
 */

static void enter(void)
{
	osk_bind(jf_url, sizeof(jf_url));
}

/*
 * Job: check the server address (jf_connect), save it, then load the user
 * page.
 */
void job_connect(void)
{
	if (jf_connect() < 0) {
		app_fail(jf_err_title(), jf_err_body());
		return;
	}

	/* Worth remembering now that we know it works. */
	jf_save_url();

	/* Chain straight on, repainting with a new message in between. */
	app_run_job(job_load_users, "Loading users...");
}

/*
 * The keyboard edits the address; START or Enter connects; Circle clears
 * the field.
 */
/*
 * Colour ramps over the screen, for judging 16-bit banding: development
 * and 16-bit test builds only (config.h), toggled with L2.
 */
#if DEBUG_UI || TEST_16BIT_FB
static int g_ramps;
#endif

static void input(u32 p)
{
#if DEBUG_UI || TEST_16BIT_FB
	if (p & PAD_L2) {
		g_ramps = !g_ramps;
		return;
	}

	if (g_ramps)
		return;                       /* only L2 while the ramps show */
#endif

	if (osk_input(p) || (p & PAD_START)) {
		app_run_job(job_connect, "Connecting...");
		return;
	}

	if (p & PAD_CIRCLE)
		jf_url[0] = '\0';
}

/*
 * The connect screen: the address field, the keyboard, the PS2's own
 * address and any settings.txt notice.
 */
static void draw(void)
{
#if DEBUG_UI || TEST_16BIT_FB
	if (g_ramps) {
		ui_color_ramps();
		return;
	}
#endif

	ui_waves();
	ui_brand();

	ui_text_c(SCR_W / 2, 116, 1.0f, C_WHITE, "Server URL:");

	/* Filled field with a brighter rule beneath, as in the web client. */
	const float fx = 90.0f, fw = SCR_W - 180.0f;
	ui_round(fx, 148, fw, 30, 4, C_FIELD);
	ui_box  (fx, 178, fw,  3,    C_FIELDLINE);

	/* Scroll so the tail stays visible once the URL outgrows the field. */
	const char *u = jf_url;
	float avail = fw - 28.0f;
	while (*u && ui_text_w(1.0f, u) > avail)
		u++;

	if (*jf_url)
		ui_text(fx + 14, 152, 1.0f, C_LILAC, u);
	else
		ui_text(fx + 14, 152, 1.0f, C_RULE, "http://");

	/* Caret sits after whatever is visible. */
	ui_box(fx + 14 + ui_text_w(1.0f, u) + 1, 153, 2, 20, C_WHITE);

	osk_draw();

	/*
	 * The console's address and how it got it - or, before the first
	 * connect starts the network, what settings.txt asks for.
	 */
	{
		char ipline[64];

		if (net_how()[0])
			snprintf(ipline, sizeof(ipline), "PS2  %s  (%s)",
			         net_local_ip(), net_how());
		else if (settings.network == NET_STATIC)
			snprintf(ipline, sizeof(ipline), "PS2  %s  (static)",
			         settings.ip[0] ? settings.ip : "no ip=");
		else if (settings.network == NET_DHCP || !settings.ip[0])
			snprintf(ipline, sizeof(ipline), "PS2  from router");
		else
			snprintf(ipline, sizeof(ipline), "PS2  router, else %s",
			         settings.ip);

		/* And the display mode in use, since 480p is a choice. */
		{
			int n = strlen(ipline);

			snprintf(ipline + n, sizeof(ipline) - n, "  %s", disp->name);
		}

		ui_text(SCR_W - 40 - ui_text_w(1.0f, ipline), 360, 1.0f,
		        C_RULE, ipline);
	}

	/* A line settings.txt could not use, or a failed save. */
	if (settings_notice()[0])
		ui_text_c(SCR_W / 2, 384, 1.0f, C_ERR, settings_notice());
	else if (!settings_found())
		ui_text_c(SCR_W / 2, 384, 1.0f, C_RULE,
		          "No settings.txt beside the ELF: defaults");

	if (mods_failures()[0]) {
		char m[160];
		snprintf(m, sizeof(m), "modules failed: %s", mods_failures());
		ui_text(40, 360, 1.0f, C_ERR, m);
	}

	{
		static const BtnGlyph b[] = { BTN_CROSS, BTN_SQUARE,
		                              BTN_START, BTN_SELECT };
		static const char *l[] = { "Select", "Delete", "Connect", "Quit" };
		ui_hint_row(b, l, 4);
	}
}

const Screen scr_connect = { enter, input, draw };
