/* Host test boundary: only the FileXio operations used by hdd.c. */
int fileXioInit(void);
void fileXioExit(void);
int fileXioDevctl(const char *, int, void *, unsigned int, void *, unsigned int);
int fileXioMount(const char *, const char *, int);
int fileXioUmount(const char *);
