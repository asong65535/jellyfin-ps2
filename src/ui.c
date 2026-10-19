/*
 * Drawing helpers on top of gsKit: screen set-up, frames, boxes and
 * rounded boxes, text, wrapping, the waves, headers, button hints, and the
 * splash and exit screens.
 */

#include <gsKit.h>
#include <gsInline.h>     /* gsKit_heap_alloc, GIF_TAG_AD: queued register writes */
#include "ui.h"
#include "freeze.h"
#include "font.h"
#include "logo.h"
#include "poster.h"

#include <dmaKit.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static GSGLOBAL *gs;
static int       g_font_ready;

/* ------------------------------------------------------------------ */

/*
 * Progressive 240-line modes: gsKit's default display geometry for them is
 * wider than a CRT's visible area, so a 320-pixel canvas spread past the
 * edges and text near them was cut off. Use the conventional active width
 * instead - 8 GS clocks per pixel (MAGH 7), the value the Rokusho 240p test
 * suite tunes for PVMs - and centre the picture where gsKit had it.
 * Interlaced and 480p modes keep gsKit's geometry, which is right for them.
 */
static void fix_progressive_geometry(void)
{
	if (gs->Interlace != GS_NONINTERLACED || disp->fb_h > 288)
		return;

	int cx = gs->StartX + gs->DW / 2;
	int cy = gs->StartY + gs->DH / 2;

	gs->MagH   = 7;
	gs->DW     = disp->fb_w * (gs->MagH + 1);
	gs->DH     = disp->fb_h * (gs->MagV + 1);
	gs->StartX = cx - gs->DW / 2;
	gs->StartY = cy - gs->DH / 2;

	gsKit_set_display_offset(gs, 0, 0);
}

/* The GS's mode-dependent settings, from the current display profile. */
static void gs_from_profile(void)
{
	gs->Mode      = disp->gs_mode;
	gs->Interlace = disp->interlace;
	gs->Field     = disp->field;
	gs->Width     = disp->fb_w;
	gs->Height    = disp->fb_h;   /* the full picture; menus use SCR_H */
	gs->PSM       = disp->psm;

	/* 16-bit colour shows steps on smooth gradients; the GS's ordered
	 * dithering breaks them into a fine, even grain. */
	gs->Dithering = disp->dither ? GS_SETTING_ON : GS_SETTING_OFF;
}

void ui_init_video(void)
{
	gs = gsKit_init_global();

	/* Everything mode-dependent comes from the display profile. */
	gs_from_profile();

	gs->PSMZ      = GS_PSMZ_16S;
	gs->ZBuffering      = GS_SETTING_OFF;
	gs->DoubleBuffering = GS_SETTING_ON;

	/* Needed so the font atlas blends instead of drawing black boxes. */
	gs->PrimAlphaEnable = GS_SETTING_ON;

	dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
	            D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
	dmaKit_chan_init(DMA_CHANNEL_GIF);

	gsKit_init_screen(gs);
	fix_progressive_geometry();
	gsKit_mode_switch(gs, GS_ONESHOT);

	/* Standard source-over: Cs*As + Cd*(1-As). */
	gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

/*
 * Upload the font, the logo and the poster slots to video memory. After
 * ui_init_video.
 */
void ui_init_font(void)
{
	font_init(gs);
	logo_init(gs);

	/* VRAM for artwork, allocated once and reused for the whole session. */
	poster_init(gs);

	g_font_ready = 1;
}

/*
 * After a display mode change (from v51): gsKit has set up the new
 * framebuffers, and everything that lived after them in VRAM - font, logo,
 * artwork - is reserved and uploaded again, in the same order as at
 * start-up, from the copies kept in main memory.
 */
void ui_rebuild_video_memory(void)
{
	font_reload(gs);
	logo_reload(gs);
	poster_rebuild(gs);
}

/*
 * Switch the screen to another display profile.
 *
 * All of VRAM is given up and gsKit sets the GS up again in the new mode,
 * with new framebuffers; then the font, logo and artwork go back after
 * them (ui_rebuild_video_memory). Only from a menu or the start-up - never
 * during playback, when the video texture lives in that memory too.
 */
void ui_set_display(int id)
{
	if (id == display_current())
		return;

	display_select(id);

	gsKit_vram_clear(gs);
	gs_from_profile();
	gsKit_init_screen(gs);
	fix_progressive_geometry();
	gsKit_mode_switch(gs, GS_ONESHOT);
	gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

	ui_rebuild_video_memory();
}

/*
 * A box shaded from colour l on the left to r on the right (horizontal),
 * or from top to bottom when vertical: the GS interpolates the colour
 * across it. Used by the colour ramps that judge 16-bit banding.
 */
void ui_gradient(float x, float y, float w, float h, u64 l, u64 r, int vertical)
{
	x = LAY_X(x);
	y = LAY_Y(y) + SCR_Y0;
	w = LAY_W(w);
	h = LAY_H(h);

	u64 tl = l, tr = vertical ? l : r, bl = vertical ? r : l, br = r;

	gsKit_prim_quad_gouraud(gs, x, y, x + w, y, x, y + h, x + w, y + h,
	                        2, tl, tr, bl, br);
}

/*
 * Colour ramps over the whole screen, for judging banding: black to white,
 * to red, to green, to blue, and a dark grey ramp - the hardest case,
 * where 8-bit-per-channel steps already show and 16-bit ones show most.
 */
void ui_color_ramps(void)
{
	ui_box(SCR_LEFT, SCR_TOP, SCR_FULL_W, SCR_FB_H,
	       GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));

	const u64 k = GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0);
	const u64 to[5] = {
		GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0),
		GS_SETREG_RGBAQ(0xFF, 0x00, 0x00, 0x80, 0),
		GS_SETREG_RGBAQ(0x00, 0xFF, 0x00, 0x80, 0),
		GS_SETREG_RGBAQ(0x00, 0x00, 0xFF, 0x80, 0),
		GS_SETREG_RGBAQ(0x40, 0x40, 0x40, 0x80, 0),
	};
	static const char *name[5] = { "white", "red", "green", "blue",
	                               "dark grey" };

	for (int i = 0; i < 5; i++) {
		float y = 40 + i * 70;

		ui_dither(1);              /* gradients are what dithering is for */
		ui_gradient(40, y, SCR_W - 80, 46, k, to[i], 0);
		ui_dither(0);
		ui_text(40, y + 48, 1.0f, C_DIM, name[i]);
	}

	ui_text_c(SCR_W / 2, 6, 1.0f, C_WHITE,
	          disp->psm == GS_PSM_CT16 ? "16-bit, dithered  (L2: back)"
	                                    : "32-bit  (L2: back)");
}

/* ------------------------------------------------------------------ */
/* Geometry test pattern (DEBUG_UI builds)                            */
/* ------------------------------------------------------------------ */

/* A box in framebuffer pixels, bypassing the layout. */
static void fb_box(float x, float y, float w, float h, u64 col)
{
	gsKit_prim_sprite(gs, x, y, x + w, y + h, 2, col);
}

/* Text with its top-left corner at framebuffer pixel (x, y). */
static void fb_text(float x, float y, u64 col, const char *s)
{
	ui_text((x - lay->ox) / lay->sx, (y - SCR_Y0 - lay->oy) / lay->sy,
	        1.0f, col, s);
}

/* Width of s in framebuffer pixels. */
static float fb_text_w(const char *s)
{
	return ui_text_w(1.0f, s) * lay->sx;
}

/*
 * A pattern to check the display geometry of the current mode, drawn in
 * framebuffer pixels over the whole picture:
 *
 *   - a 1-pixel white frame on the four edges, with yellow corner marks:
 *     the exact edges of what the GS sends;
 *   - rulers from each edge inward, a mark every 1% up to 10%, longer at
 *     5% and 10%, coloured in pairs (1-2 red, 3-4 yellow, 5-6 green,
 *     7-8 cyan, 9-10 magenta) and numbered at the even ones: the first
 *     number visible on each side is how much that side's overscan hides,
 *     so opposite sides should read the same;
 *   - a green frame at 5%, where every menu element must be;
 *   - a centre cross and a circle - round when the pixels are as wide as
 *     they are tall;
 *   - the GS values in use, to compare with what is seen.
 */
void ui_geometry_pattern(void)
{
	const float W = disp->fb_w, H = disp->fb_h;
	const u64 white  = GS_SETREG_RGBAQ(0xEB, 0xEB, 0xF5, 0x80, 0);
	const u64 yellow = GS_SETREG_RGBAQ(0xE6, 0xC8, 0x5A, 0x80, 0);
	const u64 green  = GS_SETREG_RGBAQ(0x6E, 0xDC, 0x96, 0x80, 0);
	const u64 purple = GS_SETREG_RGBAQ(0xBE, 0x6E, 0xDC, 0x80, 0);
	const u64 pair[5] = {
		GS_SETREG_RGBAQ(0xF0, 0x50, 0x50, 0x80, 0),   /* 1-2 */
		GS_SETREG_RGBAQ(0xF0, 0xDC, 0x50, 0x80, 0),   /* 3-4 */
		GS_SETREG_RGBAQ(0x50, 0xE6, 0x78, 0x80, 0),   /* 5-6 */
		GS_SETREG_RGBAQ(0x50, 0xDC, 0xF0, 0x80, 0),   /* 7-8 */
		GS_SETREG_RGBAQ(0xE6, 0x64, 0xE6, 0x80, 0),   /* 9-10 */
	};

	fb_box(0, 0, W, H, C_BG);

	/* Edge frame and corner marks. */
	fb_box(0, 0, W, 1, white);
	fb_box(0, H - 1, W, 1, white);
	fb_box(0, 0, 1, H, white);
	fb_box(W - 1, 0, 1, H, white);

	float c = W / 24.0f, t = W >= 640 ? 3.0f : 2.0f;

	fb_box(0, 0, c, t, yellow);          fb_box(0, 0, t, c, yellow);
	fb_box(W - c, 0, c, t, yellow);      fb_box(W - t, 0, t, c, yellow);
	fb_box(0, H - t, c, t, yellow);      fb_box(0, H - c, t, c, yellow);
	fb_box(W - c, H - t, c, t, yellow);  fb_box(W - t, H - c, t, c, yellow);

	/* Rulers: 1% to 10% from each edge, around the centre lines. */
	for (int i = 1; i <= 10; i++) {
		u64 col = pair[(i - 1) / 2];
		float len = (i % 5 == 0) ? H * 0.06f : H * 0.03f;
		float x   = W * i / 100.0f, y = H * i / 100.0f;
		char  n[4];

		/* left and right edges: vertical marks on the horizontal axis */
		fb_box(x, H / 2 - len / 2, 1, len, col);
		fb_box(W - 1 - x, H / 2 - len / 2, 1, len, col);

		/* top and bottom edges: horizontal marks on the vertical axis */
		fb_box(W / 2 - len / 2, y, len, 1, col);
		fb_box(W / 2 - len / 2, H - 1 - y, len, 1, col);

		if (i % 2 == 0) {
			snprintf(n, sizeof(n), "%d", i);

			fb_text(x - fb_text_w(n) / 2, H / 2 + len / 2 + 2, col, n);
			fb_text(W - 1 - x - fb_text_w(n) / 2, H / 2 + len / 2 + 2,
			        col, n);
			fb_text(W / 2 + len / 2 + 4, y - 8 * lay->sy, col, n);
			fb_text(W / 2 + len / 2 + 4, H - 1 - y - 8 * lay->sy, col, n);
		}
	}

	/* The 5% safe frame. */
	{
		float sx = W * 0.05f, sy = H * 0.05f;

		fb_box(sx, sy, W - 2 * sx, 1, green);
		fb_box(sx, H - 1 - sy, W - 2 * sx, 1, green);
		fb_box(sx, sy, 1, H - 2 * sy, green);
		fb_box(W - 1 - sx, sy, 1, H - 2 * sy, green);
	}

	/* Centre cross and a circle (round on square pixels). */
	{
		float cx = W / 2, cy = H / 2, r = H * 0.22f, a = H * 0.11f;

		fb_box(cx - a, cy, 2 * a, 1, purple);
		fb_box(cx, cy - a, 1, 2 * a, purple);

		for (int d = 0; d < 360; d++) {
			float rad = d * 3.14159265f / 180.0f;
			fb_box(cx + r * cosf(rad), cy + r * sinf(rad), 1.5f, 1.5f, purple);
		}
	}

	/* The values in use. */
	{
		char l1[64], l2[64], l3[64];
		float ty = H * 0.14f, lh = 24.0f * lay->sy;

		snprintf(l1, sizeof(l1), "GEOMETRY  %s  %dx%d  %s",
		         disp->name, disp->fb_w, disp->fb_h,
		         disp->psm == GS_PSM_CT16 ? "16-bit" : "32-bit");
		snprintf(l2, sizeof(l2), "MAGH %d  MAGV %d  DW %d  DH %d",
		         gs->MagH, gs->MagV, gs->DW, gs->DH);
		snprintf(l3, sizeof(l3), "StartX %d  StartY %d   R2: back",
		         gs->StartX, gs->StartY);

		fb_text(W / 2 - fb_text_w(l1) / 2, ty, white, l1);
		fb_text(W / 2 - fb_text_w(l2) / 2, ty + lh, white, l2);
		fb_text(W / 2 - fb_text_w(l3) / 2, ty + 2 * lh, C_DIM, l3);
	}
}

/* The gsKit context, for modules that draw textures themselves. */
GSGLOBAL *ui_gs(void) { return gs; }

/* Start a frame: clear to the background colour. */
void ui_begin(void)
{
	gsKit_clear(gs, C_BG);
	ui_dither(0);                  /* flat interface drawing: no dither */
}

/*
 * Dithering on or off for what is drawn next (16-bit modes only).
 *
 * The GS dithers when writing to a 16-bit framebuffer, which hides colour
 * steps in pictures - the film, posters, gradients - but turns flat
 * translucent areas (the subtitle backing, the busy panel) into a visible
 * grid of dots. So it is switched on just around pictures and off for the
 * interface: a queued write of the DTHE register (0x45) between drawing
 * operations. In 32-bit modes it has no effect, and nothing is queued.
 */
void ui_dither(int on)
{
	if (!disp->dither)
		return;

	u64 *p = gsKit_heap_alloc(gs, 1, 16, GIF_AD);

	*p++ = GIF_TAG_AD(1);
	*p++ = GIF_AD;
	*p++ = on ? 1 : 0;             /* DTHE: 1 dithers, 0 does not */
	*p++ = 0x45;                   /* the DTHE register */
}

/*
 * End a frame: tick the freeze safety net, send the queued drawing to the
 * GS, and flip at the next vertical blank.
 */
void ui_end(void)
{
	freeze_tick();
	gsKit_queue_exec(gs);
	gsKit_sync_flip(gs);
}

/* ------------------------------------------------------------------ */
/* Primitives                                                         */
/* ------------------------------------------------------------------ */

void ui_box(float x, float y, float w, float h, u64 col)
{
	/* Layout space to canvas (layout.h), then canvas to framebuffer. */
	x = LAY_X(x);
	y = LAY_Y(y) + SCR_Y0;
	w = LAY_W(w);
	h = LAY_H(h);
	gsKit_prim_sprite(gs, x, y, x + w, y + h, 2, col);
}

/*
 * Two overlapping rectangles form a plus shape, leaving the four corners
 * empty. Then a stepped quarter-circle fills each corner. At 480i with a
 * radius of 4 to 8 px this reads as a clean rounded rectangle.
 */
void ui_round(float x, float y, float w, float h, float r, u64 col)
{
	if (r < 1.0f) { ui_box(x, y, w, h, col); return; }
	if (r > w * 0.5f) r = w * 0.5f;
	if (r > h * 0.5f) r = h * 0.5f;

	ui_box(x,     y + r, w,         h - 2 * r, col);
	ui_box(x + r, y,     w - 2 * r, h,         col);

	int steps = (int)r;
	for (int i = 0; i < steps; i++) {
		float dy  = i + 0.5f;
		float dx  = r - sqrtf(r * r - (r - dy) * (r - dy));
		float top = y + i;
		float bot = y + h - i - 1;

		/* one scanline of each corner pair */
		ui_box(x + dx,             top, r - dx, 1, col);
		ui_box(x + w - r,          top, r - dx, 1, col);
		ui_box(x + dx,             bot, r - dx, 1, col);
		ui_box(x + w - r,          bot, r - dx, 1, col);
	}
}

/* ------------------------------------------------------------------ */
/* Text                                                               */
/* ------------------------------------------------------------------ */

float ui_text_w(float scale, const char *s)
{
	return font_width(scale, s);
}

/* Draw text with its top-left corner at (x, y). */
void ui_text(float x, float y, float scale, u64 col, const char *s)
{
	if (g_font_ready)
		font_draw(gs, x, y, scale, col, s);
}

/* Draw text centred on cx. */
void ui_text_c(float cx, float y, float scale, u64 col, const char *s)
{
	ui_text(cx - font_width(scale, s) * 0.5f, y, scale, col, s);
}

/*
 * Wrap once, with a window into the result.
 *
 * Returns the number of lines the text needs in total, whatever was actually
 * drawn. That is what lets a caller decide whether it has to offer scrolling
 * before it draws anything.
 *
 * Lines before `first` are measured and skipped; at most `max_lines` are
 * drawn from there. Pass draw = 0 to measure only.
 */
static int wrap_impl(float x, float y, float line_h, float scale, u64 col,
                     const char *s, float max_w, int max_lines, int first,
                     int draw)
{
	int lines = 0;
	int shown = 0;

	while (*s) {
		int   best = 0;
		int   i    = 0;
		int   hard = 0;
		float w    = 0.0f;
		char  buf[160];

		/* Walk forward, remembering the last space that still fits. */
		while (s[i] && i < (int)sizeof(buf) - 1) {
			if (s[i] == '\n') { hard = 1; break; }

			w += font_width(scale, (char[]){ s[i], 0 });
			if (w > max_w) break;
			if (s[i] == ' ') best = i;
			i++;
		}

		int take = (hard || s[i] == '\0') ? i : (best > 0 ? best : i);

		if (draw && lines >= first && shown < max_lines) {
			memcpy(buf, s, take);
			buf[take] = '\0';
			ui_text(x, y + shown * line_h, scale, col, buf);
			shown++;
		}

		s += take;
		if (*s == '\n') s++;             /* consume the break itself */
		while (*s == ' ') s++;
		lines++;
	}

	return lines;
}

/*
 * Draw text word-wrapped to max_w, at most max_lines lines; returns the
 * lines the whole text needs.
 */
int ui_wrap(float x, float y, float line_h, float scale, u64 col,
            const char *s, float max_w, int max_lines)
{
	return wrap_impl(x, y, line_h, scale, col, s, max_w, max_lines, 0, 1);
}

/* ui_wrap starting at wrapped line 'first', for scrolling long text. */
int ui_wrap_from(float x, float y, float line_h, float scale, u64 col,
                 const char *s, float max_w, int max_lines, int first)
{
	return wrap_impl(x, y, line_h, scale, col, s, max_w, max_lines, first, 1);
}

/* How many lines the text needs at max_w, without drawing. */
int ui_wrap_lines(float scale, const char *s, float max_w)
{
	return wrap_impl(0, 0, 0, scale, 0, s, max_w, 0, 0, 0);
}

/* A thin scrollbar showing which part of a long text is visible. */
void ui_scrollbar(float x, float y, float h, int total, int visible, int top)
{
	if (total <= visible || visible <= 0)
		return;                        /* nothing hidden, nothing to say */

	/* The thumb covers the same fraction of the track as the visible lines
	 * do of the whole, with a floor so it stays grabbable on long text. */
	float frac = (float)visible / (float)total;
	float th   = h * frac;

	if (th < 12.0f)
		th = 12.0f;

	float span = h - th;
	float pos  = (total == visible) ? 0.0f
	                                : span * ((float)top / (total - visible));

	ui_round(x, y, 3, h, 1, C_RULE);
	ui_round(x, y + pos, 3, th, 1, C_PURPLE);
}

/* ------------------------------------------------------------------ */
/* Chrome                                                             */
/* ------------------------------------------------------------------ */

/*
 * The Jellyfin banner wave. Two offset sine curves drawn as vertical
 * columns, which is the cheapest way to get a curve out of sprites.
 */
static void wave(float base, float amp, float period, float phase, u64 col)
{
	/* Across the whole screen, beyond the layout in 1080i. */
	int x0 = (int)SCR_LEFT - 2, x1 = (int)(SCR_LEFT + SCR_FULL_W) + 2;

	for (int x = x0; x < x1; x += 2) {
		float yy = base + sinf((x / period) + phase) * amp;
		ui_box(x, yy, 2, SCR_BOTTOM - yy, col);   /* to the very bottom */
	}
}

/* The two violet waves along the bottom of every screen. */
void ui_waves(void)
{
	wave(386.0f, 10.0f,  90.0f, 0.0f, C_WAVE1);
	wave(404.0f,  8.0f, 115.0f, 2.1f, C_WAVE2);
}

/*
 * "Jellyfin" large in white with "PS2" small and violet beside it, both
 * sitting on a shared baseline, then a hairline rule. Integer scales only:
 * this is a pixel typeface drawn with a NEAREST filter, so 2x is crisp and
 * 1.3x is not.
 */
void ui_brand(void)
{
	const float big = 2.0f, small = 1.0f;

	float nw = ui_text_w(big,   "Jellyfin");
	float sw = ui_text_w(small, "PS2");
	float x  = (SCR_W - (nw + 8 + sw)) * 0.5f;

	ui_text(x, 34, big, C_WHITE, "Jellyfin");

	/* Drop "PS2" so its baseline matches the big text. */
	ui_text(x + nw + 8, 34 + font_height(big) - font_height(small) - 4,
	        small, C_PURPLE, "PS2");

	ui_box(70, 96, SCR_W - 140, 1, C_RULE);
}

/*
 * A screen title top left, optional text top right, and a rule under them.
 */
void ui_header(const char *title, const char *right)
{
	ui_text(40, 30, 1.0f, C_WHITE, title);

	if (right && right[0])
		ui_text(SCR_W - 40 - ui_text_w(1.0f, right), 30, 1.0f, C_DIM, right);

	ui_box(40, 62, SCR_W - 80, 1, C_RULE);
}

/*
 * What the user sees while modules load. The logo if one was supplied,
 * with the wordmark beneath it; otherwise the wordmark alone, centred.
 */
/*
 * The splash layout: waves, the logo, and a line in the big font under it.
 * With ps2 set the line is the wordmark, "Jellyfin" with a small purple
 * "PS2" dropped to its baseline; otherwise just the text, centred.
 */
static void splash_screen(const char *line, int ps2)
{
	ui_begin();
	ui_waves();

	float y = 170.0f;                  /* where the line goes without a logo */

	if (logo_present()) {
		float ly = 96.0f;

		logo_draw(gs, (SCR_W - logo_width) * 0.5f, ly);
		y = ly + logo_height + 20.0f;
	}

	if (g_font_ready) {
		float nw = ui_text_w(2.0f, line);

		if (ps2) {
			float sw = ui_text_w(1.0f, "PS2");
			float x  = (SCR_W - (nw + 8 + sw)) * 0.5f;

			ui_text(x, y, 2.0f, C_WHITE, line);
			ui_text(x + nw + 8,
			        y + font_height(2.0f) - font_height(1.0f) - 4.0f,
			        1.0f, C_PURPLE, "PS2");
		} else {
			ui_text((SCR_W - nw) * 0.5f, y, 2.0f, C_WHITE, line);
		}

		/* The version, so a report can say which one it was. */
		ui_text_c(SCR_W * 0.5f, y + font_height(2.0f) + 10.0f, 1.0f, C_DIM,
		          JFPS2_BUILD[0] ? "v" JFPS2_VERSION " build " JFPS2_BUILD
		                         : "v" JFPS2_VERSION);
	}

	ui_end();
}

/* The start-up splash: logo, name and version. */
void ui_splash(void)  { splash_screen("Jellyfin", 1); }

/* The exit screen: the splash layout saying 'See you later'. */
void ui_goodbye(void) { splash_screen("See you later", 0); }

/* A plain start-up progress line, used by the boot log (DEBUG_BOOT_LOG). */
void ui_boot(const char *msg)
{
	ui_begin();

	/* No font on the first call, so the bar alone proves video is alive. */
	ui_round(40, 40, 180, 6, 3, C_PURPLE);
	ui_box(40, 62, SCR_W - 80, 1, C_RULE);

	if (g_font_ready) {
		ui_text(40, 84, 1.0f, C_WHITE, msg);
		ui_text(40, SCR_H - 36, 1.0f, C_DIM,
		        "built " __DATE__ " " __TIME__);
	}

	ui_end();
}

/* ------------------------------------------------------------------ */
/* Button glyphs                                                      */
/* ------------------------------------------------------------------ */

static void ring(float cx, float cy, float r, u64 col)
{
	int steps = (int)(r * 2);
	for (int i = 0; i < steps; i++) {
		float dy = -r + i + 0.5f;
		float dx = sqrtf(r * r - dy * dy);
		ui_box(cx - dx, cy + dy, 2, 1, col);          /* left edge  */
		ui_box(cx + dx - 2, cy + dy, 2, 1, col);      /* right edge */
	}
}

/*
 * A controller button symbol (cross, circle, square, triangle, START,
 * SELECT, d-pad) drawn from boxes.
 */
static void draw_glyph(BtnGlyph g, float cx, float cy)
{
	float r = 9.0f;

	switch (g) {
	case BTN_CROSS:
		ring(cx, cy, r, C_LILAC);
		for (int i = -5; i <= 5; i++) {
			ui_box(cx + i, cy + i, 2, 2, C_LILAC);
			ui_box(cx + i, cy - i, 2, 2, C_LILAC);
		}
		break;

	case BTN_SQUARE:
		ring(cx, cy, r, C_LILAC);
		ui_box(cx - 5, cy - 5, 10, 1, C_LILAC);
		ui_box(cx - 5, cy + 4, 10, 1, C_LILAC);
		ui_box(cx - 5, cy - 5, 1, 10, C_LILAC);
		ui_box(cx + 4, cy - 5, 1, 10, C_LILAC);
		break;

	case BTN_CIRCLE:
		ring(cx, cy, r, C_LILAC);
		ring(cx, cy, r - 4, C_LILAC);
		break;

	case BTN_TRIANGLE:
		ring(cx, cy, r, C_LILAC);
		for (int i = 0; i < 6; i++) {
			ui_box(cx - i, cy - 5 + i, 1, 1, C_LILAC);
			ui_box(cx + i, cy - 5 + i, 1, 1, C_LILAC);
		}
		ui_box(cx - 5, cy + 4, 10, 1, C_LILAC);
		break;

	case BTN_START:
		ui_round(cx - 12, cy - 5, 24, 10, 4, C_LILAC);
		ui_box(cx - 3, cy - 2, 7, 1, C_BG);
		ui_box(cx - 3, cy + 1, 7, 1, C_BG);
		break;

	case BTN_SELECT:
		/* Same pill, narrower, so the two read as a pair. */
		ui_round(cx - 11, cy - 5, 22, 10, 4, C_LILAC);
		ui_box(cx - 5, cy - 1, 10, 2, C_BG);
		break;

	case BTN_DPAD:
		ui_box(cx - 3, cy - 8, 6, 16, C_LILAC);
		ui_box(cx - 8, cy - 3, 16, 6, C_LILAC);
		break;

	case BTN_L1R1:
		ui_round(cx - 11, cy - 6, 22, 8, 3, C_LILAC);
		break;
	}
}

/* The centred row of button hints at the bottom of a screen. */
void ui_hint_row(const BtnGlyph *btns, const char **labels, int n)
{
	const float s   = 1.0f;
	const float gap = 22.0f;

	/* Measure first so the whole row can be centred. */
	float total = 0.0f;
	for (int i = 0; i < n; i++)
		total += 22.0f + ui_text_w(s, labels[i]) + (i < n - 1 ? gap : 0);

	float x = (SCR_W - total) * 0.5f;
	float y = SCR_H - 34;

	for (int i = 0; i < n; i++) {
		draw_glyph(btns[i], x + 10, y + 6);
		ui_text(x + 24, y - 1, s, C_LILAC, labels[i]);
		x += 22.0f + ui_text_w(s, labels[i]) + gap;
	}
}