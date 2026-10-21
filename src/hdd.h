#ifndef HDD_H
#define HDD_H

/* Capture the launch path before the IOP reset. Non-HDD boots are ignored. */
void hdd_prepare(const char *elf);
int hdd_requested(void);

/* ELF path rewritten to our mountpoint; "" for an unsupported HDD path. */
const char *hdd_boot_path(void);
const char *hdd_notice(void);

/* Mount only after our modules loaded. Owns one read-only pfs0: mount. */
int hdd_mount(int modules_ready);
void hdd_close(void);

#endif
