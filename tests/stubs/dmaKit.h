#ifndef TEST_DMAKIT_H
#define TEST_DMAKIT_H
enum { D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
       D_CTRL_STD_OFF, D_CTRL_RCYC_8, DMA_CHANNEL_GIF };
void dmaKit_init(int, int, int, int, int, int);
void dmaKit_chan_init(int);
#endif
