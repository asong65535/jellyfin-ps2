/* EE-only display: reuse the app's gsKit setup and bitmap font, without UI services. */
#include "probe_screen.h"
#include "../src/font.h"
#include "../src/display.h"
#include <dmaKit.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static GSGLOBAL *gs;
static char rows[PROBE_SCREEN_ROWS][PROBE_SCREEN_COLS];

void probe_screen_init(void)
{
	display_select(DISPLAY_480I);
	memset(rows, 0, sizeof(rows));
	gs = gsKit_init_global();
	gs->Mode = disp->gs_mode;
	gs->Interlace = disp->interlace;
	gs->Field = disp->field;
	gs->Width = disp->fb_w;
	gs->Height = disp->fb_h;
	gs->PSM = disp->psm;
	gs->PSMZ = GS_PSMZ_16S;
	gs->ZBuffering = GS_SETTING_OFF;
	gs->DoubleBuffering = GS_SETTING_ON;
	gs->PrimAlphaEnable = GS_SETTING_ON;
	dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
	            D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
	dmaKit_chan_init(DMA_CHANNEL_GIF);
	gsKit_init_screen(gs);
	gsKit_mode_switch(gs, GS_ONESHOT);
	gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
	font_init(gs);
}

void probe_screen_printf(int row, const char *fmt, ...)
{
	if (row < 0 || row >= PROBE_SCREEN_ROWS)
		return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(rows[row], sizeof(rows[row]), fmt, ap);
	va_end(ap);

	/* Redraw retained rows into each backbuffer, then present before returning.
	 * 35 rows at 12 pixels fit inside the app's 640x448 overscan-safe canvas.
	 * Fit long rows horizontally rather than losing the final return code. */
	gsKit_clear(gs, GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));
	for (int i = 0; i < PROBE_SCREEN_ROWS; i++) {
		float scale = 0.5f;
		float width = font_width(scale, rows[i]);
		if (width > 608.0f)
			scale *= 608.0f / width;
		font_draw(gs, 16, 8 + i * 12, scale,
		          GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0), rows[i]);
	}
	gsKit_queue_exec(gs);
	gsKit_sync_flip(gs);
}
