#ifndef TEST_GSKIT_H
#define TEST_GSKIT_H
#include <stdint.h>
typedef uint64_t u64;
typedef struct {
    int Mode, Interlace, Field, Width, Height, PSM, PSMZ;
    int ZBuffering, DoubleBuffering, PrimAlphaEnable;
} GSGLOBAL;
enum {
    GS_MODE_NTSC, GS_MODE_DTV_480P, GS_MODE_DTV_1080I,
    GS_INTERLACED, GS_NONINTERLACED, GS_FIELD, GS_FRAME,
    GS_PSM_CT24, GS_PSM_CT16, GS_PSMZ_16S, GS_ONESHOT
};
#define GS_SETTING_OFF 0
#define GS_SETTING_ON 1
#define GS_SETREG_ALPHA(a,b,c,d,e) ((u64)(a)|((u64)(b)<<2)|((u64)(c)<<4)|((u64)(d)<<6)|((u64)(e)<<32))
#define GS_SETREG_RGBAQ(r,g,b,a,q) ((u64)(r)|((u64)(g)<<8)|((u64)(b)<<16)|((u64)(a)<<24)|((u64)(q)<<32))
GSGLOBAL *gsKit_init_global(void);
void gsKit_init_screen(GSGLOBAL *);
void gsKit_mode_switch(GSGLOBAL *, int);
void gsKit_set_primalpha(GSGLOBAL *, u64, int);
void gsKit_clear(GSGLOBAL *, u64);
void gsKit_queue_exec(GSGLOBAL *);
void gsKit_sync_flip(GSGLOBAL *);
#endif
