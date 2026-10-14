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

void ui_init_video(void)
{
	gs = gsKit_init_global();

	gs->Mode      = GS_MODE_NTSC;
	gs->Interlace = GS_INTERLACED;
	gs->Field     = GS_FIELD;
	gs->Width     = SCR_W;
	gs->Height    = SCR_H;
	gs->PSM       = GS_PSM_CT24;
	gs->PSMZ      = GS_PSMZ_16S;
	gs->ZBuffering      = GS_SETTING_OFF;
	gs->DoubleBuffering = GS_SETTING_ON;

	/* Needed so the font atlas blends instead of drawing black boxes. */
	gs->PrimAlphaEnable = GS_SETTING_ON;

	dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
	            D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
	dmaKit_chan_init(DMA_CHANNEL_GIF);

	gsKit_init_screen(gs);
	gsKit_mode_switch(gs, GS_ONESHOT);

	/* Standard source-over: Cs*As + Cd*(1-As). */
	gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

void ui_init_font(void)
{
	font_init(gs);
	logo_init(gs);

	/* VRAM for artwork, allocated once and reused for the whole session. */
	poster_init(gs);

	g_font_ready = 1;
}

GSGLOBAL *ui_gs(void) { return gs; }

void ui_begin(void) { gsKit_clear(gs, C_BG); }

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

void ui_text(float x, float y, float scale, u64 col, const char *s)
{
	if (g_font_ready)
		font_draw(gs, x, y, scale, col, s);
}

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

int ui_wrap(float x, float y, float line_h, float scale, u64 col,
            const char *s, float max_w, int max_lines)
{
	return wrap_impl(x, y, line_h, scale, col, s, max_w, max_lines, 0, 1);
}

int ui_wrap_from(float x, float y, float line_h, float scale, u64 col,
                 const char *s, float max_w, int max_lines, int first)
{
	return wrap_impl(x, y, line_h, scale, col, s, max_w, max_lines, first, 1);
}

int ui_wrap_lines(float scale, const char *s, float max_w)
{
	return wrap_impl(0, 0, 0, scale, 0, s, max_w, 0, 0, 0);
}

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
	for (int x = 0; x < SCR_W; x += 2) {
		float yy = base + sinf((x / period) + phase) * amp;
		ui_box(x, yy, 2, SCR_H - yy, col);
	}
}

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
	ui_text(x + nw + 8, 34 + (font_line_h * big) - (font_line_h * small) - 4,
	        small, C_PURPLE, "PS2");

	ui_box(70, 96, SCR_W - 140, 1, C_RULE);
}

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
		          "v" JFPS2_VERSION);
	}

	ui_end();
}

void ui_splash(void)  { splash_screen("Jellyfin", 1); }

void ui_goodbye(void) { splash_screen("See you later", 0); }

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