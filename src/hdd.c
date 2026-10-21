/* Internal ATA HDD settings: retain the boot partition across the IOP reset. */
#include "hdd.h"
#include "config.h"

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <io_common.h>
#include <stdio.h>
#include <string.h>

static int g_requested;
static int g_mounted;
static char g_partition[5 + APA_IDMAX + 1];
static char g_boot_path[288];
static char g_notice[160];

int hdd_requested(void)          { return g_requested; }
const char *hdd_boot_path(void)  { return g_boot_path; }
const char *hdd_notice(void)     { return g_notice; }

void hdd_prepare(const char *elf)
{
	g_requested = strncmp(elf, "hdd", 3) == 0 ||
	              strncmp(elf, "pfs", 3) == 0;
	g_partition[0] = g_boot_path[0] = g_notice[0] = '\0';
	if (!g_requested)
		return;

#if !IOP_RESET
	/* Keeping a launcher's IOP would require inspecting its mount ownership. */
	snprintf(g_notice, sizeof(g_notice), "HDD settings require IOP_RESET=1");
	return;
#endif
	if (strncmp(elf, "pfs", 3) == 0) {
		snprintf(g_notice, sizeof(g_notice),
		         "HDD boot path has no APA partition; use hdd0:partition:pfs:/path");
		return;
	}
	if (strncmp(elf, "hdd0:", 5) != 0)
		goto invalid;

	const char *part = elf + 5;
	const char *end;
	const char *path;
	if (*part == '/') {
		/* A launcher's partition-qualified browsing path. */
		part++;
		end = strchr(part, '/');
		if (!end)
			goto invalid;
		path = end;
	} else {
		/* SDK/wLaunchELF composite path: hdd0:partition:pfs:/dir/app.elf. */
		end = strchr(part, ':');
		if (!end || strncmp(end + 1, "pfs", 3) != 0)
			goto invalid;
		path = end + 4;
		while (*path >= '0' && *path <= '9')
			path++;
		if (*path++ != ':')
			goto invalid;
	}

	int len = end - part;
	if (len < 1 || len > APA_IDMAX || !*path ||
	    path[strlen(path) - 1] == '/' || strchr(path, ':'))
		goto invalid;
	for (int i = 0; i < len; i++)
		if (part[i] == '/' || part[i] == '\\' || part[i] == ',' || part[i] == ':')
			goto invalid;

	int n = snprintf(g_boot_path, sizeof(g_boot_path), "pfs0:%s%s",
	                 *path == '/' ? "" : "/", path);
	if (n >= (int)sizeof(g_boot_path))
		goto invalid;
	snprintf(g_partition, sizeof(g_partition), "hdd0:%.*s", len, part);
	return;

invalid:
	g_boot_path[0] = '\0';
	snprintf(g_notice, sizeof(g_notice), "Unsupported or too long HDD boot path");
}

int hdd_mount(int modules_ready)
{
	if (!g_requested || g_notice[0])
		return -1;
	if (g_mounted)
		return 0;
	if (!modules_ready) {
		snprintf(g_notice, sizeof(g_notice), "HDD drivers unavailable; see module failures");
		return -1;
	}

	/* Never bind after a known server startup failure: FileXio retries forever. */
	int rc = fileXioInit();
	if (rc < 0) {
		snprintf(g_notice, sizeof(g_notice), "HDD FileXio init failed (%d)", rc);
		return -1;
	}
	rc = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
	if (rc != 0) {
		snprintf(g_notice, sizeof(g_notice), "HDD status %d (need APA disk, status 0)", rc);
		return -1;
	}
	rc = fileXioMount("pfs0:", g_partition, FIO_MT_RDONLY);
	if (rc < 0) {
		snprintf(g_notice, sizeof(g_notice), "HDD mount %s failed (%d)", g_partition, rc);
		return -1;
	}
	g_mounted = 1;
	return 0;
}

void hdd_close(void)
{
	if (!g_mounted)
		return;
	int rc = fileXioUmount("pfs0:");
	if (rc < 0) {
		snprintf(g_notice, sizeof(g_notice), "HDD unmount failed (%d)", rc);
		printf("%s\n", g_notice);
		return;
	}
	g_mounted = 0;
}
