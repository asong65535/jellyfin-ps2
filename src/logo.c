#include "logo.h"

#include <malloc.h>

static GSTEXTURE g_tex;
static u32      *g_pixels;
static int       g_ready;

int logo_present(void) { return logo_width > 0 && g_ready; }

void logo_init(GSGLOBAL *gs)
{
	if (g_ready || logo_width == 0)
		return;

	int n = logo_width * logo_height;

	g_pixels = memalign(128, n * 4);
	if (!g_pixels)
		return;

	/* CT32 on the GS is ABGR in a little-endian word. */
	for (int i = 0; i < n; i++) {
		unsigned char r = logo_rgba[i * 4 + 0];
		unsigned char g = logo_rgba[i * 4 + 1];
		unsigned char b = logo_rgba[i * 4 + 2];
		unsigned char a = logo_rgba[i * 4 + 3];

		g_pixels[i] = ((u32)a << 24) | ((u32)b << 16) |
		              ((u32)g <<  8) |  (u32)r;
	}

	g_tex.Width    = logo_width;
	g_tex.Height   = logo_height;
	g_tex.PSM      = GS_PSM_CT32;
	g_tex.Filter   = GS_FILTER_NEAREST;
	g_tex.Mem      = g_pixels;
	g_tex.Vram     = gsKit_vram_alloc(
	                     gs,
	                     gsKit_texture_size(logo_width, logo_height,
	                                        GS_PSM_CT32),
	                     GSKIT_ALLOC_USERBUFFER);
	g_tex.VramClut = 0;
	g_tex.Clut     = NULL;

	gsKit_texture_upload(gs, &g_tex);

	g_ready = 1;
}

void logo_draw(GSGLOBAL *gs, float x, float y)
{
	if (!logo_present())
		return;

	gsKit_prim_sprite_texture(gs, &g_tex,
	                          x, y,
	                          0, 0,
	                          x + logo_width, y + logo_height,
	                          logo_width, logo_height,
	                          3,
	                          GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0));
}
