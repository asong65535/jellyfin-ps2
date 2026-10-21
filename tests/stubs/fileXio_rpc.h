/* Host test boundary for HDD access and the standalone probe. */
#ifndef TEST_FILEXIO_RPC_H
#define TEST_FILEXIO_RPC_H
#define FXIO_WAIT 0
#define FILEXIO_MAX_DEVICES 32
typedef struct {
	struct { unsigned int mode, attr; } stat;
	char name[256];
} iox_dirent_t;
struct fileXioDevice {
	char name[16];
	unsigned int type, version;
	char desc[128];
};
int fileXioInit(void);
void fileXioExit(void);
void fileXioSetBlockMode(int);
int fileXioGetDeviceList(struct fileXioDevice *, unsigned int);
int fileXioDevctl(const char *, int, void *, unsigned int, void *, unsigned int);
int fileXioMount(const char *, const char *, int);
int fileXioUmount(const char *);
int fileXioDopen(const char *);
int fileXioDread(int, iox_dirent_t *);
int fileXioDclose(int);
int fileXioOpen(const char *, int, ...);
int fileXioRead(int, void *, int);
int fileXioClose(int);
#endif
