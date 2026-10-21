#ifndef HDD_H
#define HDD_H

/* Capture the launch path before the IOP reset. Non-HDD boots are ignored. */
void hdd_prepare(const char *elf);
int hdd_requested(void);

/* ELF path rewritten to our mountpoint; "" for an unsupported HDD path. */
const char *hdd_boot_path(void);
const char *hdd_notice(void);

/* Mount after mods_load_hdd. Close before the runtime IOP reset. */
int hdd_mount(int modules_ready, void (*progress)(const char *));
void hdd_close(void);

/* Discard RPC/mount state after an IOP reset; retain the captured boot path. */
void hdd_iop_reset(void);

#endif
