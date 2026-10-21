/* The probe's native boundaries are mocked; libc reads a real local fixture. */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static FILE *probe_fopen(const char *, const char *);
static size_t probe_fread(void *, size_t, size_t, FILE *);
static int probe_ferror(FILE *);
static int probe_fclose(FILE *);
#define fopen probe_fopen
#define fread probe_fread
#define ferror probe_ferror
#define fclose probe_fclose
#define main hdd_probe_main
#include "../tools/hdd_probe.c"
#undef main
#undef fopen
#undef fread
#undef ferror
#undef fclose
#include "../tools/probe_screen.c"

#define MODULES(X) X(iomanX) X(fileXio) X(dev9) X(ps2atad) X(ps2hdd) X(ps2fs)
#define BUFFER(n) unsigned char n##_irx[16] __attribute__((aligned(16))); \
                  unsigned int size_##n##_irx = 16;
MODULES(BUFFER)
#undef BUFFER

enum {
	SUCCESS, SERVER_FAIL, BIND_FAIL, HDD_LOAD_FAIL, REGISTRY_FAIL, STATUS_FAIL,
	ENUM_FAIL, PARTITION_MISSING, WRONG_TYPE, SUBPARTITION, MOUNT_FAIL,
	ROOT_FAIL, RAW_OPEN_FAIL, RAW_READ_FAIL, LIBC_OPEN_FAIL, LIBC_READ_FAIL,
	LIBC_MISMATCH, LIBC_CLOSE_FAIL, UNMOUNT_FAIL
};
static int fault, current_row, resets, loaded, bind_calls, bound, block_mode;
static int mounted, dir_open, raw_open, enum_entries, raw_offset, libc_calls;
static int mount_calls, raw_calls, exits, sleeps;
static int presented, submitted, draws, frames, screen_ready;
static char screen[40][160];
static char transcript[16384];
static char launch_arg[288] = "mass:/APPS/JELLYFIN/jellyfin-ps2-hdd-probe2.elf";
static const char content[] = "SECRET fixture contents must never reach the screen.\n"
                              "server=192.168.1.2:8096\nnetwork=dhcp\nvideo=480i\n";

static void waiting(void)
{
	assert(presented && !submitted);
	assert(strncmp(screen[current_row] + 3, "WAIT", 4) == 0);
}
GSGLOBAL *gsKit_init_global(void)
{
	static GSGLOBAL context;
	memset(&context, 0, sizeof(context));
	return &context;
}
void dmaKit_init(int a, int b, int c, int d, int e, int mask)
{
	assert(a == D_CTRL_RELE_OFF && b == D_CTRL_MFD_OFF && c == D_CTRL_STS_UNSPEC);
	assert(d == D_CTRL_STD_OFF && e == D_CTRL_RCYC_8 && mask == (1 << DMA_CHANNEL_GIF));
}
void dmaKit_chan_init(int channel) { assert(channel == DMA_CHANNEL_GIF); }
void gsKit_init_screen(GSGLOBAL *context)
{
	assert(context == gs && gs->Width == 640 && gs->Height == 480);
	assert(gs->Mode == GS_MODE_NTSC && gs->Interlace == GS_INTERLACED);
	assert(gs->DoubleBuffering && gs->PrimAlphaEnable && !gs->ZBuffering);
	screen_ready = 1;
}
void gsKit_mode_switch(GSGLOBAL *context, int mode) { assert(context == gs && mode == GS_ONESHOT); }
void gsKit_set_primalpha(GSGLOBAL *context, u64 alpha, int mode)
{
	assert(context == gs && alpha == GS_SETREG_ALPHA(0, 1, 0, 1, 0) && !mode);
}
void font_init(GSGLOBAL *context) { assert(context == gs && screen_ready); }
/* Conservative maximum advance of the app's current atlas, including cell padding. */
float font_width(float scale, const char *s) { return strlen(s) * 15 * scale; }
void gsKit_clear(GSGLOBAL *context, u64 color)
{
	assert(context == gs && !submitted && color == GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));
	draws = presented = 0;
}
void font_draw(GSGLOBAL *context, float x, float y, float scale, u64 color, const char *s)
{
	(void)color;
	assert(context == gs && !submitted && y == 8 + draws * 12);
	assert(x >= 16 && x + font_width(scale, s) <= 624.01f);
	assert(y >= 0 && y + 22 * scale <= 448); /* full cell, inside safe canvas */
	assert(scale > 0 && scale <= 0.5f && strlen(s) < PROBE_SCREEN_COLS);
	if (strcmp(screen[draws], s)) current_row = draws;
	snprintf(screen[draws], sizeof(screen[0]), "%s", s);
	draws++;
}
void gsKit_queue_exec(GSGLOBAL *context)
{
	assert(context == gs && !submitted && draws == PROBE_SCREEN_ROWS);
	submitted = 1;
}
void gsKit_sync_flip(GSGLOBAL *context)
{
	assert(context == gs && submitted);
	submitted = 0; presented = 1; frames++;
	assert(strlen(transcript) + strlen(screen[current_row]) + 1 < sizeof(transcript));
	strcat(transcript, screen[current_row]);
}
int SleepThread(void) { sleeps++; return 0; }
void sceSifInitRpc(int mode) { assert(mode == 0); }
int SifIopReset(const char *args, int mode)
{
	waiting(); assert(!args[0] && !mode && !bound); resets++; return 1;
}
int SifIopSync(void) { waiting(); return 1; }
int sceCdInit(int mode) { waiting(); assert(mode == SCECdINoD); return 1; }
int SifLoadFileInit(void) { waiting(); return 0; }
int SifInitIopHeap(void) { waiting(); return 0; }
int sbv_patch_enable_lmb(void) { waiting(); return 0; }
int sbv_patch_disable_prefix_check(void) { waiting(); return 0; }
int SifExecModuleBuffer(void *data, unsigned int size, unsigned int arglen,
                        const char *args, int *result)
{
	waiting(); assert(size == 16 && resets == 1);
	loaded++;
	*result = 0;
	if (data == fileXio_irx && fault == SERVER_FAIL) *result = 1;
	if (data == ps2hdd_irx) {
		const char expected[] = "-o\0" "4\0" "-n\0" "20";
		assert(arglen == sizeof(expected) && memcmp(args, expected, arglen) == 0);
		if (fault == HDD_LOAD_FAIL) return -400;
	} else if (data == ps2fs_irx) {
		const char expected[] = "-m\0" "1\0" "-o\0" "8\0" "-n\0" "24";
		assert(arglen == sizeof(expected) && memcmp(args, expected, arglen) == 0);
	} else {
		assert(!arglen && !args);
	}
	return loaded;
}
int fileXioInit(void)
{
	waiting(); assert(loaded == 2); bind_calls++;
	if (fault == BIND_FAIL) return -7;
	bound = 1; return 0;
}
void fileXioExit(void) { waiting(); assert(bound); bound = 0; exits++; }
void fileXioSetBlockMode(int mode) { waiting(); assert(bound && mode == FXIO_WAIT); block_mode = mode; }
int fileXioGetDeviceList(struct fileXioDevice *devices_out, unsigned int size)
{
	waiting(); assert(bound && !block_mode && loaded == 6 && size == FILEXIO_MAX_DEVICES);
	strcpy(devices_out[0].name, "hdd"); strcpy(devices_out[1].name, "pfs");
	return fault == REGISTRY_FAIL ? 1 : 2;
}
int fileXioDevctl(const char *dev, int cmd, void *arg, unsigned int arglen,
                  void *out, unsigned int outlen)
{
	waiting(); assert(bound && strcmp(dev, "hdd0:") == 0 && cmd == HDIOC_STATUS);
	assert(!arg && !arglen && !out && !outlen);
	return fault == STATUS_FAIL ? 3 : 0;
}
int fileXioDopen(const char *path)
{
	waiting(); assert(bound && !dir_open);
	if (strcmp(path, "hdd0:") == 0) { assert(!mounted); dir_open = 11; }
	else { assert(strcmp(path, "pfs0:/") == 0 && mounted); dir_open = 12; }
	return dir_open;
}
int fileXioDread(int fd, iox_dirent_t *entry_out)
{
	waiting(); assert(bound && fd == dir_open);
	memset(entry_out, 0, sizeof(*entry_out));
	if (fd == 11) {
		if (fault == ENUM_FAIL) return -5;
		if (enum_entries++ >= 2) return 0;
		strcpy(entry_out->name, enum_entries == 1 && fault != PARTITION_MISSING ? "__common" : "other");
		entry_out->stat.mode = fault == WRONG_TYPE ? 0x1337 : APA_TYPE_PFS;
		entry_out->stat.attr = fault == SUBPARTITION ? APA_FLAG_SUB : 0;
	} else {
		if (fault == ROOT_FAIL) return -5;
		strcpy(entry_out->name, "APPS");
	}
	return 1;
}
int fileXioDclose(int fd) { waiting(); assert(fd == dir_open); dir_open = 0; return 0; }
int fileXioMount(const char *target, const char *source, int flags)
{
	waiting(); assert(bound && !dir_open && !mounted && enum_entries == 3);
	assert(strcmp(target, "pfs0:") == 0 && strcmp(source, "hdd0:__common") == 0);
	assert(flags == FIO_MT_RDONLY); mount_calls++;
	if (fault == MOUNT_FAIL) return -2;
	mounted = 1; return 0;
}
int fileXioOpen(const char *path, int flags, ...)
{
	waiting(); assert(bound && mounted && !dir_open && !raw_open);
	assert(strcmp(path, "pfs0:/APPS/JELLYFIN/settings.txt") == 0 && flags == FIO_O_RDONLY);
	raw_calls++;
	if (fault == RAW_OPEN_FAIL) return -2;
	raw_open = 77; return raw_open;
}
int fileXioRead(int fd, void *buf, int size)
{
	waiting(); assert(fd == raw_open && size > 0);
	if (fault == RAW_READ_FAIL) return -5;
	/* Deliberately short reads prove the probe accumulates a bounded prefix. */
	int n = size < 7 ? size : 7;
	if (n > (int)strlen(content) - raw_offset) n = strlen(content) - raw_offset;
	memcpy(buf, content + raw_offset, n); raw_offset += n; return n;
}
int fileXioClose(int fd) { waiting(); assert(fd == raw_open); raw_open = 0; return 0; }
int fileXioUmount(const char *path)
{
	waiting(); assert(bound && mounted && !dir_open && !raw_open);
	assert(strcmp(path, "pfs0:") == 0);
	if (fault == UNMOUNT_FAIL) return -16;
	mounted = 0; return 0;
}
static FILE *probe_fopen(const char *path, const char *mode)
{
	waiting(); assert(raw_offset == 64 && !raw_open && strcmp(mode, "rb") == 0);
	libc_calls++;
	if (fault == LIBC_OPEN_FAIL) { errno = EACCES; return NULL; }
	return fopen(path, mode);
}
static size_t probe_fread(void *buf, size_t size, size_t count, FILE *fp)
{
	waiting();
	if (fault == LIBC_READ_FAIL) { errno = EIO; return 0; }
	size_t n = fread(buf, size, count, fp);
	if (fault == LIBC_MISMATCH && n) ((char *)buf)[0] ^= 1;
	return n;
}
static int probe_ferror(FILE *fp) { return fault == LIBC_READ_FAIL ? 1 : ferror(fp); }
static int probe_fclose(FILE *fp)
{
	waiting(); int rc = fclose(fp);
	if (fault == LIBC_CLOSE_FAIL) { errno = EIO; return EOF; }
	return rc;
}

static void run_case(int mode, const char *expected_failure)
{
	fault = mode;
	current_row = resets = loaded = bind_calls = bound = mounted = dir_open = raw_open = 0;
	enum_entries = raw_offset = libc_calls = mount_calls = raw_calls = exits = sleeps = 0;
	block_mode = -1;
	presented = submitted = draws = frames = screen_ready = 0;
	memset(screen, 0, sizeof(screen)); transcript[0] = '\0';
	char *argv[] = { launch_arg };
	int rc = hdd_probe_main(1, argv);
	assert(frames > 20 && presented && !submitted);
	assert(strstr(screen[0], "HDD probe 2 (gsKit)"));
	for (unsigned int i = 0; i < strlen(launch_arg); i += 70)
		assert(strncmp(screen[4 + i / 70], launch_arg + i, 70) == 0);
	assert(strstr(screen[33], "Photograph") && strstr(screen[34], "USB, then HDD"));
	assert(resets == 1 && sleeps == 1 && !bound && !dir_open && !raw_open);
	assert(!mounted || mode == UNMOUNT_FAIL);
	assert(strstr(transcript, "SECRET") == NULL);
	if (expected_failure) { assert(rc == 1 && strstr(screen[31], expected_failure)); }
	else { assert(rc == 0 && strstr(screen[31], "PASS:")); assert(libc_calls == 1); }
	if (mode == SERVER_FAIL) assert(!bind_calls && !exits);
	if (mode == BIND_FAIL) assert(bind_calls == 1 && !exits);
	if (mode >= RAW_OPEN_FAIL && mode <= RAW_READ_FAIL) assert(!libc_calls);
	if (mode >= ENUM_FAIL && mode <= SUBPARTITION) assert(!mount_calls);
	if (mode == MOUNT_FAIL || mode == ROOT_FAIL) assert(!raw_calls);
}

int main(void)
{
	assert(mkdir("pfs0:", 0700) == 0);
	assert(mkdir("pfs0:/APPS", 0700) == 0);
	assert(mkdir("pfs0:/APPS/JELLYFIN", 0700) == 0);
	FILE *fp = fopen("pfs0:/APPS/JELLYFIN/settings.txt", "wb");
	assert(fp && fwrite(content, 1, strlen(content), fp) == strlen(content));
	assert(fclose(fp) == 0);
	run_case(SUCCESS, NULL);
	strcpy(launch_arg, "hdd0:__common:pfs0:/APPS/JELLYFIN/jellyfin-ps2-hdd-probe2.elf");
	run_case(SUCCESS, NULL); /* Launch path never changes the explicit target. */
	run_case(SERVER_FAIL, "fileXio.irx");
	run_case(BIND_FAIL, "FileXio binding");
	run_case(HDD_LOAD_FAIL, "ps2hdd.irx");
	run_case(REGISTRY_FAIL, "Driver registry");
	run_case(STATUS_FAIL, "HDD status");
	run_case(ENUM_FAIL, "APA enumeration");
	run_case(PARTITION_MISSING, "APA enumeration");
	run_case(WRONG_TYPE, "APA enumeration");
	run_case(SUBPARTITION, "APA enumeration");
	run_case(MOUNT_FAIL, "PFS mount");
	run_case(ROOT_FAIL, "PFS root read");
	run_case(RAW_OPEN_FAIL, "Raw file read");
	run_case(RAW_READ_FAIL, "Raw file read");
	run_case(LIBC_OPEN_FAIL, "Libc file read");
	run_case(LIBC_READ_FAIL, "Libc file read");
	run_case(LIBC_MISMATCH, "Libc file read");
	run_case(LIBC_CLOSE_FAIL, "Libc file read");
	run_case(UNMOUNT_FAIL, "Cleanup");
	memset(launch_arg, 'W', sizeof(launch_arg) - 1);
	launch_arg[sizeof(launch_arg) - 1] = '\0';
	run_case(SUCCESS, NULL); /* All five launch-path rows remain visible. */
	/* Long rows fit horizontally; replacing one row retains earlier results. */
	probe_screen_printf(34, "%s", "123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890");
	assert(strlen(screen[34]) == 90 && strstr(screen[0], "probe 2"));
	puts("Standalone HDD probe display bounds/presentation, checkpoint order, failure isolation and cleanup checks passed");
	return 0;
}
