/*
 * Jellyfin PS2
 *
 *   settings.txt -> URL entry -> connect -> who is watching -> sign in -> library
 *
 * main.c does three things: bring up hardware, read input, run frames.
 *
 * Order matters here. Video comes up before anything that can hang, so a
 * failure shows a message instead of a black screen.
 */

#include <kernel.h>
#include <sifrpc.h>
#include <libpad.h>
#include <libkbd.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <stdio.h>

#include "app.h"
#include "config.h"
#include "settings.h"
#include "freeze.h"
#include "mods.h"
#include "net.h"
#include "ui.h"
#include "osk.h"
#include "jf.h"
#include "audio.h"


static char pad_buf[256] __attribute__((aligned(64)));

static void pad_setup(void)
{
	padInit(0);
	padPortOpen(0, 0, pad_buf);
}

/* buttons is a level, not an event, so mask to rising edges. */
static u32 pad_pressed(void)
{
#if SKIP_PAD
	return 0;
#else
	static u32 prev = 0;
	struct padButtonStatus b;

	if (padGetState(0, 0) != PAD_STATE_STABLE) return 0;
	if (padRead(0, 0, &b) == 0) return 0;

	u32 now = 0xFFFF ^ b.btns;
	u32 hit = now & ~prev;
	prev = now;
	return hit;
#endif
}

/* A USB keyboard types into whatever the OSK is currently bound to. */
void osk_append(char c);
void osk_backspace(void);

static int keyboard_poll(void)
{
	if (!mods_have_keyboard()) return 0;

	char c;
	int enter = 0;

	while (PS2KbdRead(&c) > 0) {
		if (c == 0x08 || c == 0x7F)      osk_backspace();
		else if (c == '\n' || c == '\r') enter = 1;
		else if (c >= 0x20 && c < 0x7F)  osk_append(c);
	}
	return enter;
}

/*
 * Hand the console back.
 *
 * The IOP is reset first so whatever runs next does not inherit our modules,
 * then rom0:OSDSYS starts the system browser. On a modded console that lands
 * in the boot menu, which is where you came from.
 */
static void quit_to_browser(void)
{
	/*
	 * The splash's layout with "See you later" under the logo. It stays
	 * up through the shutdown below, until the browser takes the screen.
	 */
	ui_goodbye();

	audio_stop();
	jf_reports_wait(2000);       /* a last stop report or resume point */
	net_stop();

	SifIopReset("", 0);
	while (!SifIopSync()) {}
	SifInitRpc(0);

	LoadExecPS2("rom0:OSDSYS", 0, NULL);

	/* Only reached if that failed. */
	SleepThread();
}

int main(int argc, char **argv)
{

	SifInitRpc(0);

	/* Before any thread sleeps: timed sleeps run on the vertical blank. */
	net_snooze_init();

	/* Video before anything that can block, so a failure has somewhere
	 * to report itself. */
	ui_init_video();
	ui_init_font();

	/* Logo and wordmark, always. With the boot log on, the step messages
	 * replace it a moment later. */
	ui_splash();

#if DEBUG_BOOT_LOG
	/*
	 * Every step names itself before it runs, so a freeze tells you which
	 * call blocked. Ugly, and exactly what you want when it breaks.
	 */
	settings_load(argc, argv);
	jf_init();
	ui_boot("read settings.txt");

	mods_load(ui_boot);

#if !SKIP_PAD
	ui_boot("padInit");
	pad_setup();
#endif

#if DEBUG_NET_LOG
	net_set_progress(ui_boot);
#endif
#else
	/*
	 * settings.txt is read before mods_load because the IOP reset in
	 * there takes uLaunchELF's USB modules with it. Ours load straight
	 * after, so mass: is writable again by the time anything is saved.
	 */
	settings_load(argc, argv);
	jf_init();

	mods_load(NULL);

#if !SKIP_PAD
	pad_setup();
#endif
#endif

	/* Sound. Needs libsd and audsrv, which mods_load has just installed. */
#if DEBUG_BOOT_LOG
	ui_boot("audio");
#endif
	audio_init();

	/*
	 * Drop the UI below the decoder.
	 *
	 * On the EE a lower number wins, and the main thread starts at 0. Since
	 * gsKit_sync_flip busy-waits on the GS rather than sleeping, the UI
	 * never yields, and without this the decoder is starved and playback
	 * crawls. Measured both ways: with it, playback runs; without it, it
	 * does not.
	 *
	 * Rendering is vsync-bound and loses nothing by giving way.
	 */
#if UI_THREAD_PRIO
	ChangeThreadPriority(GetThreadId(), UI_THREAD_PRIO);
	freeze_register(GetThreadId(), FZ_UI);
	freeze_init();
#endif

	osk_init();
	app_init();

	for (;;) {
		u32 pressed = pad_pressed();

		/* Enter on a real keyboard behaves exactly like START. */
		if (keyboard_poll())
			pressed |= PAD_START;

		/* SELECT quits from anywhere. Handled here rather than per screen
		 * so there is never a state you cannot get out of. */
		if (pressed & PAD_SELECT)
			quit_to_browser();

		/* A thread the freeze safety net had to set aside. */
		{
			char code = freeze_take();

			if (code)
				app_freeze(code);
		}

		app_frame(pressed);
	}

	return 0;
}