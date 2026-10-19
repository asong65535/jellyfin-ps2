/*
 * Colours, screen size and drawing helpers; see ui.c.
 */

#ifndef UI_H
#define UI_H

#include <gsKit.h>
#include "config.h"

/* Jellyfin dark theme, sampled from the web client. */
#define C_BG        GS_SETREG_RGBAQ(0x10, 0x12, 0x2A, 0x80, 0x00)
#define C_WHITE     GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0x00)
#define C_PURPLE    GS_SETREG_RGBAQ(0xAA, 0x5C, 0xC3, 0x80, 0x00)
#define C_LILAC     GS_SETREG_RGBAQ(0xC7, 0xB8, 0xE8, 0x80, 0x00)
#define C_FIELD     GS_SETREG_RGBAQ(0x5A, 0x4E, 0x8E, 0x80, 0x00)
#define C_FIELDLINE GS_SETREG_RGBAQ(0x9B, 0x87, 0xE0, 0x80, 0x00)
#define C_ROWSEL    GS_SETREG_RGBAQ(0x2C, 0x28, 0x56, 0x80, 0x00)
#define C_KEY       GS_SETREG_RGBAQ(0xF2, 0xF2, 0xF8, 0x80, 0x00)
#define C_KEYSEL    GS_SETREG_RGBAQ(0xCE, 0xCE, 0xD8, 0x80, 0x00)
#define C_KEYTEXT   GS_SETREG_RGBAQ(0x24, 0x24, 0x3A, 0x80, 0x00)
#define C_DIM       GS_SETREG_RGBAQ(0x8A, 0x8E, 0xB0, 0x80, 0x00)
#define C_RULE      GS_SETREG_RGBAQ(0x30, 0x32, 0x52, 0x80, 0x00)
#define C_OK        GS_SETREG_RGBAQ(0x6E, 0xE0, 0xA0, 0x80, 0x00)
#define C_ERR       GS_SETREG_RGBAQ(0xFF, 0x74, 0x66, 0x80, 0x00)
#define C_WAVE1     GS_SETREG_RGBAQ(0x39, 0x2E, 0x63, 0x80, 0x00)
#define C_WAVE2     GS_SETREG_RGBAQ(0x55, 0x45, 0x8E, 0x80, 0x00)

/* The GS context, for code that draws its own textures. */
GSGLOBAL *ui_gs(void);

void ui_init_video(void);
void ui_init_font(void);

/* Switch the screen to display profile id (display.h), from a menu or
 * start-up only. */
void ui_set_display(int id);

/* A box shaded between two colours, left-right or top-bottom. */
void ui_gradient(float x, float y, float w, float h, u64 l, u64 r, int vertical);

/* Dithering on or off for what is drawn next (16-bit modes only). */
void ui_dither(int on);

/* The display geometry test pattern over the whole picture (DEBUG_UI). */
void ui_geometry_pattern(void);

/* Full-screen colour ramps for judging 16-bit banding (test builds). */
void ui_color_ramps(void);

/* After a display mode change: font, logo and artwork back in VRAM. */
void ui_rebuild_video_memory(void);

void ui_boot(const char *msg);      /* diagnostic: clear, one line, flip */
void ui_splash(void);               /* brand and waves, nothing else */
void ui_goodbye(void);              /* the same, saying "See you later" */

void ui_begin(void);
void ui_end(void);

/* Exact widths now: every glyph carries its own advance. */
float ui_text_w(float scale, const char *s);
void  ui_text  (float x,  float y, float scale, u64 col, const char *s);
void  ui_text_c(float cx, float y, float scale, u64 col, const char *s);

void ui_box(float x, float y, float w, float h, u64 col);

/* Corners knocked off with two inset rectangles plus four small squares.
 * Cheap, and at 480i it reads as a rounded corner. */
void ui_round(float x, float y, float w, float h, float r, u64 col);

/*
 * All three return the number of lines the text needs in total, not the
 * number drawn, so a caller can tell whether anything is being cut off.
 *
 *   ui_wrap        draws from the beginning
 *   ui_wrap_from   draws a window starting at line `first`
 *   ui_wrap_lines  measures without drawing
 */
int  ui_wrap_from(float x, float y, float line_h, float scale, u64 col,
                  const char *s, float max_w, int max_lines, int first);
int  ui_wrap_lines(float scale, const char *s, float max_w);

/*
 * A slim vertical scrollbar beside a block of text.
 *
 * Draws nothing when everything already fits, so a caller can call it
 * unconditionally. The thumb's size and position show how much is off
 * screen, which says "there is more" better than a line of button names.
 */
void ui_scrollbar(float x, float y, float h, int total, int visible, int top);

int  ui_wrap(float x, float y, float line_h, float scale, u64 col,
             const char *s, float max_w, int max_lines);

/* Chrome */
void ui_waves (void);
void ui_brand (void);
void ui_header(const char *title, const char *right);

/* Footer hints drawn as real button glyphs: cross, square, circle, START. */
typedef enum { BTN_CROSS, BTN_SQUARE, BTN_CIRCLE, BTN_TRIANGLE,
               BTN_START, BTN_SELECT, BTN_DPAD, BTN_L1R1 } BtnGlyph;

void ui_hint_row(const BtnGlyph *btns, const char **labels, int n);

#endif