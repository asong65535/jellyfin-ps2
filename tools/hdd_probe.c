/* Standalone ATA/APA/PFS probe. No settings parser or application services. */
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <io_common.h>
#include "probe_screen.h"
#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <loadfile.h>
#include <libcdvd.h>
#include <sbv_patches.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define IRX(n) extern unsigned char n[] __attribute__((aligned(16))); \
               extern unsigned int size_##n;
IRX(iomanX_irx)
IRX(fileXio_irx)
IRX(dev9_irx)
IRX(ps2atad_irx)
IRX(ps2hdd_irx)
IRX(ps2fs_irx)

/* Explicit target confirmed by the console tester; independent of argv/CWD. */
static const char partition[] = "hdd0:__common";
static const char file_path[] = "pfs0:/APPS/JELLYFIN/settings.txt";
static const char apa_args[] = "-o\0" "4\0" "-n\0" "20";
static const char pfs_args[] = "-m\0" "1\0" "-o\0" "8\0" "-n\0" "24";

enum {
	RESET, CDVD, SERVICES, PATCHES, IOMANX, FILEXIO, BIND, DEV9, ATA, APA, PFS,
	REGISTRY, STATUS, PARTITIONS, MOUNT, ROOT, RAW, LIBC, CLEANUP, STEP_COUNT
};
static const char *const labels[] = {
	"IOP reset/sync", "CDVD no-disc init", "LOADFILE/heap", "SBV patches",
	"iomanX.irx", "fileXio.irx", "FileXio binding", "ps2dev9.irx",
	"ps2atad.irx", "ps2hdd.irx", "ps2fs.irx", "Driver registry", "HDD status",
	"APA enumeration", "PFS mount", "PFS root read", "Raw file read",
	"Libc file read", "Cleanup"
};

/* Each step has a fixed screen row: earlier results survive later calls. */
static void step(int which, const char *fmt, ...)
{
	char message[75];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(message, sizeof(message), fmt, ap);
	va_end(ap);
	probe_screen_printf(10 + which, "%02d %s", which + 1, message);
}

static int load_irx(int which, unsigned char *data, unsigned int size,
                    const char *args, unsigned int arglen)
{
	step(which, "WAIT %s bytes=%u args=%u", labels[which], size, arglen);
	int result = 0;
	int id = SifExecModuleBuffer(data, size, arglen, args, &result);
	int ok = id >= 0 && result == 0;
	step(which, "%s %s load=%d start=%d bytes=%u args=%u",
	     ok ? "OK" : "FAIL", labels[which], id, result, size, arglen);
	return ok;
}

static int run_probe(int argc, char **argv)
{
	char launch[288];
	int launch_len = snprintf(launch, sizeof(launch), "%s",
	                         argc > 0 && argv && argv[0] ? argv[0] : "(none)");
	probe_screen_init();
	probe_screen_printf(0, "Jellyfin HDD probe 2 (gsKit) - " __DATE__ " " __TIME__);
	probe_screen_printf(1, "Target: %s", partition);
	probe_screen_printf(2, "File: %s", file_path);
	probe_screen_printf(3, "argc=%d argv[0]%s:", argc,
	           launch_len >= (int)sizeof(launch) ? " (truncated)" : "");
	for (unsigned int i = 0; i < strlen(launch); i += 70)
		probe_screen_printf(4 + i / 70, "%.*s", 70, launch + i);
	for (int i = 0; i < STEP_COUNT; i++)
		step(i, "NOT RUN %s", labels[i]);

	int which = RESET, failed = -1;
	int bound = 0, mounted = 0, dir = -1, fd = -1;
	FILE *fp = NULL;
	int rc, close_rc, read_errno, close_errno;
	int found = 0, entries = 0;
	unsigned int selected_mode = 0, selected_attr = 0;
	static iox_dirent_t entry;
	static struct fileXioDevice devices[FILEXIO_MAX_DEVICES];
	static unsigned char raw[64] __attribute__((aligned(64)));
	unsigned char libc_data[64];
	int raw_size = 0;
	int cleanup_error = 0;

#define FAIL(...) do { step(which, "FAIL " __VA_ARGS__); failed = which; goto cleanup; } while (0)
#define LOAD(s, n, a, l) do { which = (s); \
	if (!load_irx(which, n, size_##n, a, l)) { failed = which; goto cleanup; } } while (0)

	step(which, "WAIT IOP reset/sync (one reset)");
	sceSifInitRpc(0);
	while (!SifIopReset("", 0)) {}
	while (!SifIopSync()) {}
	sceSifInitRpc(0);
	step(which, "OK IOP reset/sync count=1");

	which = CDVD;
	step(which, "WAIT sceCdInit(SCECdINoD)");
	rc = sceCdInit(SCECdINoD);
	if (rc != 1) FAIL("CDVD no-disc init rc=%d", rc);
	step(which, "OK CDVD no-disc init rc=%d", rc);

	which = SERVICES;
	step(which, "WAIT LOADFILE binding");
	rc = SifLoadFileInit();
	if (rc < 0) FAIL("LOADFILE rc=%d", rc);
	step(which, "WAIT IOP heap binding (LOADFILE=%d)", rc);
	rc = SifInitIopHeap();
	if (rc < 0) FAIL("IOP heap rc=%d", rc);
	step(which, "OK LOADFILE and IOP heap bound");

	which = PATCHES;
	step(which, "WAIT load-module-buffer patch");
	rc = sbv_patch_enable_lmb();
	if (rc < 0) FAIL("load-module-buffer patch rc=%d", rc);
	int lmb_result = rc;
	step(which, "WAIT prefix patch (LMB=%d)", lmb_result);
	rc = sbv_patch_disable_prefix_check();
	if (rc < 0) FAIL("prefix patch rc=%d", rc);
	step(which, "OK patches LMB=%d prefix=%d", lmb_result, rc);

	LOAD(IOMANX, iomanX_irx, NULL, 0);
	LOAD(FILEXIO, fileXio_irx, NULL, 0);
	which = BIND;
	step(which, "WAIT fileXioInit (server startup succeeded)");
	rc = fileXioInit();
	if (rc < 0) FAIL("FileXio binding rc=%d", rc);
	bound = 1;
	fileXioSetBlockMode(FXIO_WAIT);
	step(which, "OK FileXio binding rc=%d blocking=FXIO_WAIT", rc);

	LOAD(DEV9, dev9_irx, NULL, 0);
	LOAD(ATA, ps2atad_irx, NULL, 0);
	LOAD(APA, ps2hdd_irx, apa_args, sizeof(apa_args));
	LOAD(PFS, ps2fs_irx, pfs_args, sizeof(pfs_args));

	which = REGISTRY;
	step(which, "WAIT fileXioGetDeviceList");
	rc = fileXioGetDeviceList(devices, FILEXIO_MAX_DEVICES);
	if (rc < 0 || rc > FILEXIO_MAX_DEVICES) FAIL("device list rc=%d", rc);
	int have_hdd = 0, have_pfs = 0;
	for (int i = 0; i < rc; i++) {
		if (strncmp(devices[i].name, "hdd", sizeof(devices[i].name)) == 0) have_hdd = 1;
		if (strncmp(devices[i].name, "pfs", sizeof(devices[i].name)) == 0) have_pfs = 1;
	}
	if (!have_hdd || !have_pfs) FAIL("registry hdd=%d pfs=%d", have_hdd, have_pfs);
	step(which, "OK registry devices=%d hdd=1 pfs=1", rc);

	which = STATUS;
	step(which, "WAIT HDIOC_STATUS hdd0:");
	rc = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
	if (rc != 0) FAIL("HDD status=%d (need 0)", rc);
	step(which, "OK HDD status=0");

	which = PARTITIONS;
	step(which, "WAIT APA Dopen hdd0:");
	dir = fileXioDopen("hdd0:");
	if (dir < 0) FAIL("APA Dopen=%d", dir);
	for (;;) {
		step(which, "WAIT APA Dread entries=%d", entries);
		rc = fileXioDread(dir, &entry);
		if (rc < 0) FAIL("APA Dread=%d entries=%d", rc, entries);
		if (rc == 0) break;
		entries++;
		if (strncmp(entry.name, partition + 5, sizeof(entry.name)) == 0) {
			found = 1;
			selected_mode = entry.stat.mode;
			selected_attr = entry.stat.attr;
		}
	}
	step(which, "WAIT APA Dclose entries=%d target=%d", entries, found);
	rc = fileXioDclose(dir);
	dir = -1;
	if (rc < 0) FAIL("APA Dclose=%d", rc);
	if (!found) FAIL("APA entries=%d __common absent", entries);
	if (selected_mode != APA_TYPE_PFS || (selected_attr & APA_FLAG_SUB))
		FAIL("__common mode=%x attr=%x (need main PFS)", selected_mode, selected_attr);
	step(which, "OK APA entries=%d __common mode=%x attr=%x",
	     entries, selected_mode, selected_attr);

	which = MOUNT;
	step(which, "WAIT mount pfs0: <- %s read-only", partition);
	rc = fileXioMount("pfs0:", partition, FIO_MT_RDONLY);
	if (rc < 0) FAIL("PFS mount rc=%d", rc);
	mounted = 1;
	step(which, "OK mount pfs0: <- %s rc=%d", partition, rc);

	which = ROOT;
	step(which, "WAIT PFS root Dopen pfs0:/");
	dir = fileXioDopen("pfs0:/");
	if (dir < 0) FAIL("PFS root Dopen=%d", dir);
	step(which, "WAIT PFS root Dread");
	rc = fileXioDread(dir, &entry);
	if (rc < 0) FAIL("PFS root Dread=%d", rc);
	step(which, "WAIT PFS root Dclose (Dread=%d)", rc);
	close_rc = fileXioDclose(dir);
	dir = -1;
	if (close_rc < 0) FAIL("PFS root Dclose=%d", close_rc);
	step(which, "OK PFS root Dread=%d name=%.32s", rc, rc ? entry.name : "(empty)");

	which = RAW;
	step(which, "WAIT raw FileXioOpen FIO_O_RDONLY");
	fd = fileXioOpen(file_path, FIO_O_RDONLY);
	if (fd < 0) FAIL("raw open rc=%d", fd);
	while (raw_size < (int)sizeof(raw)) {
		step(which, "WAIT raw FileXioRead bytes=%d", raw_size);
		rc = fileXioRead(fd, raw + raw_size, sizeof(raw) - raw_size);
		if (rc < 0) FAIL("raw read rc=%d bytes=%d", rc, raw_size);
		if (rc == 0) break;
		if (rc > (int)sizeof(raw) - raw_size) FAIL("raw read invalid count=%d", rc);
		raw_size += rc;
	}
	step(which, "WAIT raw FileXioClose bytes=%d", raw_size);
	rc = fileXioClose(fd);
	fd = -1;
	if (rc < 0) FAIL("raw close rc=%d", rc);
	if (raw_size == 0) FAIL("raw file is empty; no content read");
	step(which, "OK raw read bytes=%d close=0 (contents hidden)", raw_size);

	which = LIBC;
	step(which, "WAIT libc fopen rb (separate stream)");
	errno = 0;
	fp = fopen(file_path, "rb");
	if (!fp) FAIL("libc fopen errno=%d", errno);
	step(which, "WAIT libc fread");
	errno = 0;
	int libc_size = fread(libc_data, 1, sizeof(libc_data), fp);
	read_errno = errno;
	int read_error = ferror(fp);
	step(which, "WAIT libc fclose bytes=%d", libc_size);
	errno = 0;
	close_rc = fclose(fp);
	close_errno = errno;
	fp = NULL;
	if (read_error) FAIL("libc fread errno=%d bytes=%d", read_errno, libc_size);
	if (close_rc != 0) FAIL("libc fclose errno=%d", close_errno);
	if (libc_size != raw_size || memcmp(raw, libc_data, raw_size) != 0)
		FAIL("raw/libc mismatch raw=%d libc=%d", raw_size, libc_size);
	step(which, "OK libc bytes=%d raw prefix matches (contents hidden)", libc_size);

cleanup:
	step(CLEANUP, "WAIT close handles, unmount owned PFS, exit FileXio");
	if (fp && fclose(fp) != 0) cleanup_error = errno ? errno : -1;
	if (fd >= 0 && (rc = fileXioClose(fd)) < 0) cleanup_error = rc;
	if (dir >= 0 && (rc = fileXioDclose(dir)) < 0) cleanup_error = rc;
	if (mounted && (rc = fileXioUmount("pfs0:")) < 0) cleanup_error = rc;
	if (bound) fileXioExit();
	if (cleanup_error) {
		step(CLEANUP, "FAIL cleanup rc=%d", cleanup_error);
		if (failed < 0) failed = CLEANUP;
	} else {
		step(CLEANUP, "OK handles closed; owned mount/client released");
	}
	if (failed >= 0)
		probe_screen_printf(31, "FIRST FAILURE: %02d %s", failed + 1, labels[failed]);
	else
		probe_screen_printf(31, "PASS: FileXio -> APA -> PFS -> raw -> libc");
	probe_screen_printf(33, "Photograph this screen. Power off to leave the probe.");
	probe_screen_printf(34, "Run this same ELF from USB, then HDD. No app services started.");
	return failed >= 0 ? 1 : 0;
#undef LOAD
#undef FAIL
}

int main(int argc, char **argv)
{
	int result = run_probe(argc, argv);
	SleepThread();
	return result;
}
