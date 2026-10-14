#include "font.h"

#include <malloc.h>
#include <string.h>

static GSTEXTURE g_tex;
static u32      *g_pixels;
static int       g_ready;

void font_init(GSGLOBAL *gs)
{
	if (g_ready)
		return;

	int w = font_atlas_w, h = font_atlas_h;
	int stride = (w + 7) / 8;

	g_pixels = memalign(128, w * h * 4);
	if (!g_pixels)
		return;

	/*
	 * Unpack 1 bit per pixel into CT32. RGB stays white even where alpha
	 * is zero, so nothing can bleed a dark edge if the filter is ever
	 * changed to LINEAR. PS2 alpha runs 0 to 128, so opaque is 0x80.
	 */
	for (int y = 0; y < h; y++) {
		const unsigned char *row = &font_bits[y * stride];

		for (int x = 0; x < w; x++) {
			int on = row[x >> 3] & (0x80 >> (x & 7));
			g_pixels[y * w + x] = on ? 0x80FFFFFF : 0x00FFFFFF;
		}
	}

	g_tex.Width    = w;
	g_tex.Height   = h;
	g_tex.PSM      = GS_PSM_CT32;
	g_tex.Filter   = GS_FILTER_NEAREST;
	g_tex.Mem      = g_pixels;
	g_tex.Vram     = gsKit_vram_alloc(
	                     gs, gsKit_texture_size(w, h, GS_PSM_CT32),
	                     GSKIT_ALLOC_USERBUFFER);
	g_tex.VramClut = 0;
	g_tex.Clut     = NULL;

	gsKit_texture_upload(gs, &g_tex);

	g_ready = 1;
}

static const FontGlyph *glyph_for(unsigned char c)
{
	int i = (int)c - font_first;
	if (i < 0 || i >= font_count)
		i = '?' - font_first;
	return &font_glyphs[i];
}

float font_width(float scale, const char *s)
{
	float w = 0.0f;
	for (; *s; s++)
		w += glyph_for((unsigned char)*s)->adv * scale;
	return w;
}

float font_height(float scale)
{
	return font_line_h * scale;
}

void font_draw(GSGLOBAL *gs, float x, float y, float scale,
               u64 color, const char *s)
{
	if (!g_ready)
		return;

	float pen = x;
	float cw  = font_cell_w * scale;
	float ch  = font_cell_h * scale;

	for (; *s; s++) {
		const FontGlyph *g = glyph_for((unsigned char)*s);

		if (*s != ' ') {          /* nothing to draw, just advance */
			gsKit_prim_sprite_texture(gs, &g_tex,
			                          pen,      y,
			                          g->x,     g->y,
			                          pen + cw, y + ch,
			                          g->x + font_cell_w,
			                          g->y + font_cell_h,
			                          3, color);
		}
		pen += g->adv * scale;
	}
}
