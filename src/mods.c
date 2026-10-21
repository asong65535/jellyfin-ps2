/*
 * IOP modules. Resets the IOP, patches it to accept modules from EE
 * memory, and loads the drivers embedded in the ELF: USB storage,
 * keyboard, network and sound.
 * HDD settings use a temporary IOP before the normal runtime modules load.
 */

#include "mods.h"
#include "config.h"
#include "hdd.h"

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <sbv_patches.h>
#include <libkbd.h>
#include <stdio.h>
#include <string.h>

/*
 * Module set and load order taken from
 * $PS2SDK/samples/network/tcpip-basic/ps2ip.c
 *
 * Three network modules only. There is no ps2ip.irx or ps2ips.irx: lwIP
 * runs on the EE side and is linked in with -lps2ip.
 */

#define IRX_DECL(n) extern unsigned char n[] __attribute__((aligned(16))); extern unsigned int size_##n;

IRX_DECL(usbd_irx)
IRX_DECL(ps2kbd_irx)
IRX_DECL(bdm_irx)
IRX_DECL(bdmfs_fatfs_irx)
IRX_DECL(usbmass_bd_irx)
IRX_DECL(dev9_irx)
IRX_DECL(netman_irx)
IRX_DECL(smap_irx)
IRX_DECL(libsd_irx)
IRX_DECL(audsrv_irx)
IRX_DECL(iomanX_irx)
IRX_DECL(fileXio_irx)
IRX_DECL(ps2atad_irx)
IRX_DECL(ps2hdd_irx)
IRX_DECL(ps2fs_irx)

static int  g_kbd = 0;
static int  g_hdd = 0;
static char g_failed[160] = "";

static ModsProgress g_progress;

/* Report a loading step through the progress callback, if any. */
static void say(const char *msg)
{
	if (g_progress)
		g_progress(msg);
}

/*
 * Add a module's name to the list of those that failed to load, shown on
 * the connect screen.
 */
static void note_failure(const char *name)
{
	if (strlen(g_failed) + strlen(name) + 2 >= sizeof(g_failed))
		return;
	if (g_failed[0])
		strcat(g_failed, " ");
	strcat(g_failed, name);
}

/*
 * Load one IOP module from a buffer embedded in the ELF (bin2c), noting a
 * failure. Returns the module id, or negative.
 */
static int load(const char *name, unsigned char *buf, unsigned int size)
{
	char msg[64];
	snprintf(msg, sizeof(msg), "loading %s", name);
	say(msg);

	int ret = 0;
	int id  = SifExecModuleBuffer(buf, size, 0, NULL, &ret);
	snprintf(msg, sizeof(msg), "%s returned: load=%d start=%d", name, id, ret);
	say(msg);

	/* MODULE_RESIDENT_END=0, MODULE_REMOVABLE_END=2; 1 is not resident. */
	if (id < 0 || (ret != 0 && ret != 2)) {
		/* Startup diagnostics must not depend on a working file-I/O backend. */
		snprintf(msg, sizeof(msg), "%s(load=%d,start=%d)", name, id, ret);
		note_failure(msg);
		return -1;
	}

	return id;
}

/* Reset and patch the IOP before loading either settings or runtime drivers. */
static void init_iop(ModsProgress progress)
{
	g_progress = progress;
	g_kbd = g_hdd = 0;

#if IOP_RESET
	/*
	 * A fresh IOP. Required so sbv_patch_enable_lmb can take effect, which
	 * in turn is what allows SifExecModuleBuffer to load modules out of EE
	 * memory. Without this, module loading silently wedges.
	 *
	 * Note: this also tears down ps2link's host: filesystem, so if you
	 * launch over ps2client, settings.txt must live on mass:.
	 */
	say("resetting IOP");
	sceSifInitRpc(0);
	while (!SifIopReset("", 0)) {}
	while (!SifIopSync()) {}
	hdd_iop_reset();
#endif

	say("sif services");
	sceSifInitRpc(0);
	say("binding loadfile");
	SifLoadFileInit();
	say("binding IOP heap");
	SifInitIopHeap();
	say("patching module loader");
	sbv_patch_enable_lmb();
}

void mods_load_hdd(ModsProgress progress)
{
	g_failed[0] = '\0';
	g_hdd = 0;
	if (!hdd_requested() || hdd_notice()[0])
		return;

	/* Settings need HDD drivers only while reading. Start with a minimal IOP
	 * so the IRX staging buffers do not compete with USB/audio allocations. */
	init_iop(progress);
	g_hdd = load("iomanX", iomanX_irx, size_iomanX_irx) >= 0 &&
	        load("fileXio", fileXio_irx, size_fileXio_irx) >= 0 &&
	        load("dev9", dev9_irx, size_dev9_irx) >= 0 &&
	        load("atad", ps2atad_irx, size_ps2atad_irx) >= 0 &&
	        load("hdd", ps2hdd_irx, size_ps2hdd_irx) >= 0 &&
	        load("pfs", ps2fs_irx, size_ps2fs_irx) >= 0;
	g_progress = NULL;
}

void mods_load(ModsProgress progress)
{
	/* Runtime modules: USB storage/keyboard, network and sound. */
	/* Keep HDD bootstrap failures visible after replacing its IOP. */
	if (!hdd_requested())
		g_failed[0] = '\0';
	init_iop(progress);

	/* Pad, from the BIOS. padInit and padPortOpen hang without these. */
	say("rom0:SIO2MAN");
	if (SifLoadModule("rom0:SIO2MAN", 0, NULL) < 0)
		note_failure("sio2man");

	say("rom0:PADMAN");
	if (SifLoadModule("rom0:PADMAN", 0, NULL) < 0)
		note_failure("padman");

	/* USB: the keyboard and mass storage both sit on top of usbd. */
	load("usbd",    usbd_irx,        size_usbd_irx);
	load("bdm",     bdm_irx,         size_bdm_irx);
	load("fatfs",   bdmfs_fatfs_irx, size_bdmfs_fatfs_irx);
	load("usbmass", usbmass_bd_irx,  size_usbmass_bd_irx);

	/* Sound. audsrv sits on top of libsd, so the order matters. */
	load("libsd",  libsd_irx,  size_libsd_irx);
	load("audsrv", audsrv_irx, size_audsrv_irx);

	if (load("ps2kbd", ps2kbd_irx, size_ps2kbd_irx) >= 0) {
		say("binding keyboard");
		if (PS2KbdInit() >= 0) {
			PS2KbdSetReadmode(PS2KBD_READMODE_NORMAL);
			g_kbd = 1;
		}
	}

#if SKIP_NETWORK
	say("network skipped (SKIP_NETWORK=1)");
	note_failure("network-skipped");
#else
	/* The HDD bootstrap IOP is gone; this DEV9 instance serves Ethernet. */
	load("dev9",   dev9_irx,   size_dev9_irx);
	load("netman", netman_irx, size_netman_irx);
	load("smap",   smap_irx,   size_smap_irx);
#endif

	say("settling");
	for (int i = 0; i < 120; i++)
		nopdelay();

	g_progress = NULL;
}

/* Whether a USB keyboard driver was loaded. */
int         mods_have_keyboard(void) { return g_kbd; }
int         mods_have_hdd(void)      { return g_hdd; }

/* Space-separated names of modules that failed to load, or ''. */
const char *mods_failures(void)      { return g_failed; }
