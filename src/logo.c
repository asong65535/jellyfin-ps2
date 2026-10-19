/*
 * The Jellyfin logo, generated at build time from assets/logo.png
 * (logo_data.c), uploaded once and drawn on the splash and exit screens.
 */

#include "logo.h"
#include "config.h"

#include <malloc.h>
#include <string.h>

static GSTEXTURE g_tex;
static u32      *g_pixels;     /* one index byte per pixel */
static u32      *g_clut;       /* the 256-colour table */
static int       g_ready;

/* Whether the logo texture is uploaded and can be drawn. */
int logo_present(void) { return logo_width > 0 && g_ready; }

/*
 * Convert the generated logo (logo_data.c) to the GS's ABGR layout and
 * upload it to video memory once.
 */
void logo_init(GSGLOBAL *gs)
{
	if (g_ready || logo_width == 0)
		return;

	int n = logo_width * logo_height;

	/*
	 * An 8-bit palette texture (PSMT8): one byte per pixel, indexing the
	 * 256-colour table genlogo.py wrote already in the GS's order. About
	 * 17 KB of VRAM instead of 64 KB. Both are copied into aligned memory
	 * for the upload's DMA.
	 */
	g_pixels = memalign(128, n);
	g_clut   = memalign(64, 256 * 4);
	if (!g_pixels || !g_clut)
		return;

	memcpy(g_pixels, logo_index, n);
	memcpy(g_clut, logo_clut, 256 * 4);

	g_tex.Width    = logo_width;
	g_tex.Height   = logo_height;
	g_tex.PSM      = GS_PSM_T8;
	g_tex.ClutPSM  = GS_PSM_CT32;
	g_tex.Filter   = GS_FILTER_NEAREST;
	g_tex.Mem      = g_pixels;
	g_tex.Clut     = g_clut;
	g_tex.Vram     = gsKit_vram_alloc(
	                     gs,
	                     gsKit_texture_size(logo_width, logo_height,
	                                        GS_PSM_T8),
	                     GSKIT_ALLOC_USERBUFFER);
	g_tex.VramClut = gsKit_vram_alloc(
	                     gs, gsKit_texture_size(16, 16, GS_PSM_CT32),
	                     GSKIT_ALLOC_USERBUFFER);

	gsKit_texture_upload(gs, &g_tex);

	g_ready = 1;
}

/* Draw the logo at (x, y), its natural size. */
/*
 * After a display mode change: reserve the logo and its palette in VRAM
 * again and upload them from the copies kept in main memory.
 */
void logo_reload(GSGLOBAL *gs)
{
	if (!g_ready) {
		logo_init(gs);
		return;
	}

	g_tex.Vram     = gsKit_vram_alloc(
	                     gs,
	                     gsKit_texture_size(logo_width, logo_height,
	                                        GS_PSM_T8),
	                     GSKIT_ALLOC_USERBUFFER);
	g_tex.VramClut = gsKit_vram_alloc(
	                     gs, gsKit_texture_size(16, 16, GS_PSM_CT32),
	                     GSKIT_ALLOC_USERBUFFER);

	gsKit_texture_upload(gs, &g_tex);
}

void logo_draw(GSGLOBAL *gs, float x, float y)
{
	if (!logo_present())
		return;

	/* Layout space to canvas (layout.h), then canvas to framebuffer. */
	float w = LAY_W(logo_width), h = LAY_H(logo_height);

	x = LAY_X(x);
	y = LAY_Y(y) + SCR_Y0;

	gsKit_prim_sprite_texture(gs, &g_tex,
	                          x, y,
	                          0, 0,
	                          x + w, y + h,
	                          logo_width, logo_height,
	                          3,
	                          GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0));
}
