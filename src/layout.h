/*
 * Layouts: how the screens are arranged on a display profile's canvas.
 *
 * Every screen is designed once, on a 640x448 "layout space". A layout says
 * how that space maps onto the real canvas - a scale and an offset applied
 * by the drawing functions (ui.c, font.c, poster.c, logo.c) - and how many
 * items fit: library grid columns, posters per HOME row, user tiles.
 *
 * Three layouts: standard (480i/480p, the identity), compact (240p, the
 * same design at half size) and wide (1080i, since v56: a 16:9 layout
 * space, 853 units wide, with more items per row).
 */

#ifndef LAYOUT_H
#define LAYOUT_H

typedef struct {
	const char *name;

	/* Layout space to canvas: canvas = offset + layout * scale. */
	float sx, sy;
	float ox, oy;

	/* Width of the layout space (SCR_W): 640 in the 4:3 layouts, 853 in
	 * the wide one. Screens centre on SCR_W / 2 and stretch rules and
	 * fields to SCR_W, so most of them widen with no code of their own.
	 * The height is always 448 (SCR_H). */
	float w;

	/* What fits. grid_cols x 2 is one library page (JF_PAGE items) and
	 * the number of poster slots (POSTER_SLOTS, poster.h); each HOME row
	 * cycles through home_visible of them, so home_visible must not
	 * exceed grid_cols. grid_cols x 2 must not exceed JF_PAGE_MAX. */
	int grid_cols;
	int home_visible;
	int users_visible;
} Layout;

/* The layout in use, chosen by the display profile. Never NULL. */
extern const Layout *lay;

/* Make the layout with this id (LAYOUT_..., display.h) current. */
void layout_select(int id);

#endif
