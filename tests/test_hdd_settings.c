/* Run from a disposable working directory; all files remain local to it. */
#include "hdd.h"
#include "settings.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int init_calls, mount_calls, unmount_calls;
static int init_result, disk_status, mount_result, unmount_result;
static int modules_ready = 1;
static char partition[64];

int mods_have_hdd(void) { return modules_ready; }
void net_snooze(int ms) { (void)ms; }
int fileXioInit(void) { init_calls++; return init_result; }
int fileXioDevctl(const char *dev, int cmd, void *arg, unsigned int arglen,
                  void *buf, unsigned int buflen)
{
	assert(strcmp(dev, "hdd0:") == 0 && cmd == 0x4807);
	assert(!arg && !arglen && !buf && !buflen);
	return disk_status;
}
int fileXioMount(const char *target, const char *source, int flags)
{
	assert(strcmp(target, "pfs0:") == 0 && flags == 1);
	snprintf(partition, sizeof(partition), "%s", source);
	mount_calls++;
	return mount_result;
}
int fileXioUmount(const char *target)
{
	assert(strcmp(target, "pfs0:") == 0);
	unmount_calls++;
	return unmount_result;
}

static void put(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");
	assert(f);
	assert(fputs(text, f) >= 0);
	assert(fclose(f) == 0);
}

static void boot(char *path)
{
	hdd_close();
	char *argv[] = { path };
	settings_load(1, argv);
}

int main(void)
{
	const char *valid[] = {
		"hdd0:__common:pfs:/APPS/JF/a.elf",
		"hdd0:__common:pfs0:/APPS/JF/a.elf",
		"hdd0:__common:pfs12:APPS/JF/a.elf",
		"hdd0:/__common/APPS/JF/a.elf"
	};
	for (unsigned int i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
		hdd_prepare(valid[i]);
		assert(hdd_requested() && !hdd_notice()[0]);
		assert(strcmp(hdd_boot_path(), "pfs0:/APPS/JF/a.elf") == 0);
		assert(hdd_mount(1) == 0);
		assert(strcmp(partition, "hdd0:__common") == 0);
		hdd_close();
	}

	const char *invalid[] = {
		"pfs0:/APPS/JF/a.elf", "hdd1:part:pfs:/a.elf",
		"hdd0::pfs:/a.elf", "hdd0:part:/a.elf", "hdd0:part:pfs:",
		"hdd0:part:pfs:/folder/", "hdd0:part,secret:pfs:/a.elf",
		"hdd0:part/other:pfs:/a.elf", "hdd0:/part", "hdd0:part:pfs:/mass:a.elf"
	};
	int before = init_calls;
	for (unsigned int i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		hdd_prepare(invalid[i]);
		assert(hdd_requested() && hdd_notice()[0] && !hdd_boot_path()[0]);
		assert(hdd_mount(1) < 0);
	}
	assert(init_calls == before);
	char long_path[600];
	snprintf(long_path, sizeof(long_path), "hdd0:part:pfs:/%0500d.elf", 0);
	hdd_prepare(long_path);
	assert(hdd_notice()[0] && !hdd_boot_path()[0]);
	hdd_prepare("hdd0:123456789012345678901234567890123:pfs:/a.elf");
	assert(hdd_notice()[0]);
	hdd_prepare("hdd0:12345678901234567890123456789012:pfs:/a.elf");
	assert(!hdd_notice()[0]);

	hdd_prepare(valid[0]);
	assert(hdd_mount(0) < 0 && init_calls == before);
	hdd_prepare(valid[0]);
	init_result = -7;
	assert(hdd_mount(1) < 0 && strstr(hdd_notice(), "init failed (-7)"));
	init_result = 0;
	for (disk_status = -1; disk_status <= 3; disk_status++) {
		if (disk_status == 0) continue;
		hdd_prepare(valid[0]);
		before = mount_calls;
		assert(hdd_mount(1) < 0 && mount_calls == before);
		assert(strstr(hdd_notice(), "HDD status"));
	}
	disk_status = 0;
	hdd_prepare(valid[0]);
	mount_result = -2;
	assert(hdd_mount(1) < 0 && strstr(hdd_notice(), "mount hdd0:__common failed (-2)"));
	before = unmount_calls;
	hdd_close();
	assert(unmount_calls == before);
	mount_result = 0;
	hdd_prepare(valid[0]);
	assert(hdd_mount(1) == 0);
	before = mount_calls;
	assert(hdd_mount(1) == 0 && mount_calls == before);
	unmount_result = -16;
	hdd_close();
	assert(strstr(hdd_notice(), "unmount failed (-16)"));
	unmount_result = 0;
	hdd_close();
	before = unmount_calls;
	hdd_close();
	assert(unmount_calls == before);

	assert(mkdir("pfs0:", 0700) == 0);
	assert(mkdir("pfs0:/APPS", 0700) == 0);
	assert(mkdir("pfs0:/APPS/JF", 0700) == 0);
	put("pfs0:/APPS/JF/settings.txt", "server=192.168.1.2:8096\nnetwork=static\nip=192.168.1.3\nvideo=480p\n");
	boot("hdd0:__common:pfs:/APPS/JF/a.elf");
	assert(!settings_found() && !settings.server[0]); /* Read waits for modules. */
	assert(strcmp(settings_path(), "pfs0:/APPS/JF/settings.txt") == 0);
	settings_load_hdd();
	assert(settings_found() && !settings_notice()[0]);
	assert(strcmp(settings.server, "192.168.1.2:8096") == 0);
	assert(settings.network == NET_STATIC && settings.video_mode == VIDEO_480P);
	assert(settings_save_server("other:8096") < 0);
	assert(strstr(settings_notice(), "read-only"));
	assert(access("pfs0:/APPS/JF/settings.bak", F_OK) < 0);

	assert(unlink("pfs0:/APPS/JF/settings.txt") == 0);
	put("pfs0:/APPS/JF/settings.bak", "server=backup:8096\n");
	boot("hdd0:__common:pfs:/APPS/JF/a.elf");
	settings_load_hdd();
	assert(settings_found() && strcmp(settings.server, "backup:8096") == 0);
	assert(strstr(settings_notice(), "settings.bak"));
	assert(access("pfs0:/APPS/JF/settings.txt", F_OK) < 0);
	assert(unlink("pfs0:/APPS/JF/settings.bak") == 0);
	boot("hdd0:__common:pfs:/APPS/JF/a.elf");
	settings_load_hdd();
	assert(!settings_found() && strstr(settings_notice(), "is missing"));
	/* Reading a directory produces a read error rather than partial settings. */
	assert(mkdir("pfs0:/APPS/JF/settings.txt", 0700) == 0);
	boot("hdd0:__common:pfs:/APPS/JF/a.elf");
	settings_load_hdd();
	assert(!settings_found() && strstr(settings_notice(), "read/close failed"));
	/* The ELF path can fit while the longer settings filename cannot. */
	snprintf(long_path, sizeof(long_path), "hdd0:part:pfs:/%0270d/a", 0);
	boot(long_path);
	before = init_calls;
	settings_load_hdd();
	assert(strstr(settings_notice(), "path is too long"));
	assert(!settings_path()[0] && init_calls == before);
	boot("pfs0:/APPS/JF/a.elf");
	settings_load_hdd();
	assert(!settings_found() && strstr(settings_notice(), "no APA partition"));
	modules_ready = 0;
	boot("hdd0:__common:pfs:/APPS/JF/a.elf");
	before = init_calls;
	settings_load_hdd();
	assert(!settings_found() && init_calls == before);
	assert(strstr(settings_notice(), "drivers unavailable"));
	modules_ready = 1;

	/* USB keeps its early read and writable backup/save behavior. */
	assert(mkdir("mass:", 0700) == 0);
	put("mass:/settings.bak", "server=usb:8096\n");
	boot("mass:/a.elf");
	assert(!hdd_requested() && settings_found());
	assert(strcmp(settings.server, "usb:8096") == 0);
	assert(access("mass:/settings.txt", F_OK) == 0);
	assert(settings_save_server("new:8096") == 0);
	before = init_calls;
	settings_load_hdd();
	assert(init_calls == before);
	boot("mass:/a.elf");
	assert(strcmp(settings.server, "new:8096") == 0);
	puts("HDD path, mount lifecycle, settings read-only and USB regression checks passed");
	return 0;
}
