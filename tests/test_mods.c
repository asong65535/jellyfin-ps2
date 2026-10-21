/* Exercise the real module initialization with a mocked IOP boundary. */
#include "mods.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MODULES(X) \
	X(usbd) X(ps2kbd) X(bdm) X(bdmfs_fatfs) X(usbmass_bd) \
	X(dev9) X(netman) X(smap) X(libsd) X(audsrv) \
	X(iomanX) X(fileXio) X(ps2atad) X(ps2hdd) X(ps2fs)
#define BUFFER(n) unsigned char n##_irx[16] __attribute__((aligned(16))); \
	unsigned int size_##n##_irx = 16;
MODULES(BUFFER)
#undef BUFFER

static int requested;
static int failed_load, failed_start;
static char loaded[32][32];
static int load_count, keyboard_calls;
static char last_step[80];

int hdd_requested(void) { return requested; }
const char *hdd_notice(void) { return ""; }
void sceSifInitRpc(int mode) { assert(mode == 0); }
int SifIopReset(const char *arg, int mode) { (void)arg; (void)mode; return 1; }
int SifIopSync(void) { return 1; }
int SifLoadFileInit(void) { return 0; }
int SifInitIopHeap(void) { return 0; }
int sbv_patch_enable_lmb(void) { return 0; }
int SifLoadModule(const char *path, int argc, const char *args)
{
	assert(strncmp(path, "rom0:", 5) == 0 && !argc && !args);
	return 1;
}
void nopdelay(void) {}
int PS2KbdInit(void) { keyboard_calls++; return 0; }
int PS2KbdSetReadmode(unsigned int mode) { (void)mode; return 0; }

static void progress(const char *step)
{
	snprintf(last_step, sizeof(last_step), "%s", step);
}

int SifExecModuleBuffer(void *buf, unsigned int size, unsigned int argc,
                        const char *args, int *result)
{
	const char *name = NULL;
#define NAME(n) if (buf == n##_irx) name = #n;
	MODULES(NAME)
#undef NAME
	assert(name && size == 16 && !argc && !args);
	assert(strncmp(last_step, "loading ", 8) == 0);
	assert(load_count < 32);
	snprintf(loaded[load_count++], sizeof(loaded[0]), "%s", name);
	if (buf == ps2hdd_irx) {
		*result = failed_start;
		return failed_load ? failed_load : load_count;
	}
	*result = 0;
	return load_count;
}

static int count(const char *name)
{
	int n = 0;
	for (int i = 0; i < load_count; i++)
		if (strcmp(loaded[i], name) == 0) n++;
	return n;
}

static void boot(int hdd)
{
	requested = hdd;
	load_count = keyboard_calls = 0;
	mods_load(progress);
	assert(count("dev9") == 1 && count("netman") == 1 && count("smap") == 1);
	assert(count("libsd") == 1 && count("audsrv") == 1);
	assert(count("usbd") == 1 && count("ps2kbd") == 1);
	assert(mods_have_keyboard() && keyboard_calls == 1);
}

int main(void)
{
	boot(1);
	assert(mods_have_hdd() && !mods_failures()[0]);
	assert(count("iomanX") == 1 && count("fileXio") == 1);
	assert(count("ps2atad") == 1 && count("ps2hdd") == 1 && count("ps2fs") == 1);
	assert(!count("bdm") && !count("bdmfs_fatfs") && !count("usbmass_bd"));

	boot(0);
	assert(!mods_have_hdd() && !mods_failures()[0]);
	assert(count("bdm") == 1 && count("bdmfs_fatfs") == 1 && count("usbmass_bd") == 1);
	assert(!count("iomanX") && !count("fileXio") && !count("ps2atad"));
	assert(!count("ps2hdd") && !count("ps2fs"));

	/* Allocation failure must leave networking available and skip PFS. */
	failed_load = -400;
	boot(1);
	assert(!mods_have_hdd() && !count("ps2fs"));
	assert(strstr(mods_failures(), "hdd(load=-400,start=0)"));
	/* A nonresident startup result is also a failure, even with a module ID. */
	failed_load = 0;
	failed_start = 1;
	boot(1);
	assert(!mods_have_hdd() && !count("ps2fs"));
	assert(strstr(mods_failures(), "start=1)"));
	puts("HDD/USB module selection and allocation-failure regression checks passed");
	return 0;
}
