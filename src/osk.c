#include "osk.h"
#include "ui.h"

#include <libpad.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define MAX_KEYS  64
#define KEY_H     24.0f
#define KEY_GAP   6.0f
#define ROW_PITCH 30.0f
#define KB_TOP    204.0f

typedef enum { K_CHAR, K_CAPS, K_BACK, K_SYM, K_SPACE, K_ENTER } KeyKind;

typedef struct {
	const char *label;
	char        ch;
	KeyKind     kind;
	float       x, y, w, h;
} Key;

typedef struct { const char *label; KeyKind kind; char ch; float w; } SpecDef;

static Key  g_keys[MAX_KEYS];
static char g_klabel[MAX_KEYS][2];
static char g_midlbl[7][2];

static int g_count, g_rows;
static int g_row_start[8], g_row_len[8];
static int g_sel_row, g_sel_col;
static int g_caps, g_symbols;

static char *g_buf;
static int   g_buf_size;

/* ------------------------------------------------------------------ */

static void row_chars(const char *chars, float kw)
{
	int   n = strlen(chars);
	float total = n * kw + (n - 1) * KEY_GAP;
	float x = (SCR_W - total) * 0.5f;
	float y = KB_TOP + g_rows * ROW_PITCH;

	g_row_start[g_rows] = g_count;
	g_row_len[g_rows]   = n;

	for (int i = 0; i < n; i++) {
		int id = g_count++;
		g_klabel[id][0] = chars[i];
		g_klabel[id][1] = '\0';

		Key *k = &g_keys[id];
		k->label = g_klabel[id];
		k->ch    = chars[i];
		k->kind  = K_CHAR;
		k->x = x; k->y = y; k->w = kw; k->h = KEY_H;
		x += kw + KEY_GAP;
	}
	g_rows++;
}

static void row_special(const SpecDef *d, int n)
{
	float total = -KEY_GAP;
	for (int i = 0; i < n; i++) total += d[i].w + KEY_GAP;

	float x = (SCR_W - total) * 0.5f;
	float y = KB_TOP + g_rows * ROW_PITCH;

	g_row_start[g_rows] = g_count;
	g_row_len[g_rows]   = n;

	for (int i = 0; i < n; i++) {
		Key *k = &g_keys[g_count++];
		k->label = d[i].label;
		k->ch    = d[i].ch;
		k->kind  = d[i].kind;
		k->x = x; k->y = y; k->w = d[i].w; k->h = KEY_H;
		x += d[i].w + KEY_GAP;
	}
	g_rows++;
}

static void build(void)
{
	g_count = 0;
	g_rows  = 0;

	if (!g_symbols) {
		row_chars("1234567890", 44.0f);
		row_chars("qwertyuiop", 44.0f);
		row_chars("asdfghjkl",  44.0f);
	} else {
		row_chars("!@#$%^&*()", 44.0f);
		row_chars("-_=+[]{}\\|", 44.0f);
		row_chars(".:/?~;',\"", 44.0f);
	}

	{
		static SpecDef row[9];
		const char *mid = g_symbols ? "<>`^%$#" : "zxcvbnm";

		row[0].label = "Caps"; row[0].kind = K_CAPS; row[0].ch = 0; row[0].w = 74.0f;
		for (int i = 0; i < 7; i++) {
			g_midlbl[i][0] = mid[i];
			g_midlbl[i][1] = '\0';
			row[i + 1].label = g_midlbl[i];
			row[i + 1].kind  = K_CHAR;
			row[i + 1].ch    = mid[i];
			row[i + 1].w     = 44.0f;
		}
		row[8].label = "Backspace"; row[8].kind = K_BACK; row[8].ch = 0; row[8].w = 90.0f;
		row_special(row, 9);
	}

	{
		static SpecDef row[3];
		row[0].label = g_symbols ? "abc" : "#+="; row[0].kind = K_SYM;   row[0].ch = 0;   row[0].w = 74.0f;
		row[1].label = "Space";                   row[1].kind = K_SPACE; row[1].ch = ' '; row[1].w = 240.0f;
		row[2].label = "Enter";                   row[2].kind = K_ENTER; row[2].ch = 0;   row[2].w = 120.0f;
		row_special(row, 3);
	}

	if (g_sel_row >= g_rows) g_sel_row = g_rows - 1;
	if (g_sel_col >= g_row_len[g_sel_row]) g_sel_col = g_row_len[g_sel_row] - 1;
}

void osk_init(void) { g_sel_row = g_sel_col = 0; build(); }

void osk_bind(char *buf, int size) { g_buf = buf; g_buf_size = size; }

/* ------------------------------------------------------------------ */

void osk_append(char c)
{
	if (!g_buf) return;
	int n = strlen(g_buf);
	if (n < g_buf_size - 1) { g_buf[n] = c; g_buf[n + 1] = '\0'; }
}

void osk_backspace(void)
{
	if (!g_buf) return;
	int n = strlen(g_buf);
	if (n > 0) g_buf[n - 1] = '\0';
}

int osk_input(u32 p)
{
	if (p & PAD_UP)    { if (g_sel_row > 0) g_sel_row--; }
	if (p & PAD_DOWN)  { if (g_sel_row < g_rows - 1) g_sel_row++; }
	if (p & PAD_LEFT)  { if (g_sel_col > 0) g_sel_col--; }
	if (p & PAD_RIGHT) { if (g_sel_col < g_row_len[g_sel_row] - 1) g_sel_col++; }

	/* Rows differ in length, so clamp after a vertical move. */
	if (g_sel_col > g_row_len[g_sel_row] - 1)
		g_sel_col = g_row_len[g_sel_row] - 1;

	if (p & PAD_SQUARE) osk_backspace();

	if (p & PAD_CROSS) {
		Key *k = &g_keys[g_row_start[g_sel_row] + g_sel_col];

		switch (k->kind) {
		case K_CHAR:  osk_append(g_caps ? (char)toupper((unsigned char)k->ch)
		                                : k->ch); break;
		case K_SPACE: osk_append(' ');  break;
		case K_BACK:  osk_backspace();  break;
		case K_CAPS:  g_caps = !g_caps; break;
		case K_SYM:   g_symbols = !g_symbols; build(); break;
		case K_ENTER: return 1;
		}
	}
	return 0;
}

void osk_draw(void)
{
	for (int i = 0; i < g_count; i++) {
		Key *k  = &g_keys[i];
		int  on = (i == g_row_start[g_sel_row] + g_sel_col);

		ui_round(k->x, k->y, k->w, k->h, 4, on ? C_KEYSEL : C_KEY);
		if (on)
			ui_box(k->x + 2, k->y + k->h - 3, k->w - 4, 3, C_FIELDLINE);

		char lbl[12];
		if (k->kind == K_CHAR && g_caps)
			snprintf(lbl, sizeof(lbl), "%c", toupper((unsigned char)k->ch));
		else
			snprintf(lbl, sizeof(lbl), "%s", k->label);

		ui_text_c(k->x + k->w * 0.5f, k->y + 1, 1.0f, C_KEYTEXT, lbl);
	}
}
