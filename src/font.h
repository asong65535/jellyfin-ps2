#ifndef FONT_H
#define FONT_H

#include <gsKit.h>

/*
 * Bitmap font atlas, rasterised at build time by tools/genfont.py from
 * assets/mania.ttf. Same approach as Rokusho: every glyph occupies a full
 * cell on a shared baseline, and carries its own advance.
 *
 * FONTM, the ROM font, has no width query, so text could only be centred
 * by guesswork. Here centring and wrapping are exact.
 *
 * Alpha is 1 bit. Anti-aliased edges produced coloured fringing on analog
 * GS output, so the generator thresholds them away. The filter is NEAREST
 * to match, which also suits a pixel typeface.
 */

typedef struct {
	unsigned short x, y;      /* cell origin in the atlas */
	unsigned short adv;       /* pen advance in pixels    */
} FontGlyph;

extern const int font_atlas_w, font_atlas_h;
extern const int font_cell_w,  font_cell_h;
extern const int font_line_h;
extern const int font_first,   font_count;

extern const FontGlyph     font_glyphs[];
extern const unsigned char font_bits[];

void  font_init(GSGLOBAL *gs);

float font_width (float scale, const char *s);   /* exact */
float font_height(float scale);

/* y is the top of the cell. All glyphs share a baseline within it. */
void  font_draw(GSGLOBAL *gs, float x, float y, float scale,
                u64 color, const char *s);

#endif
