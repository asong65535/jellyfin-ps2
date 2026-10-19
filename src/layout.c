/*
 * The layouts; see layout.h.
 */

#include "layout.h"
#include "display.h"

static const Layout g_layouts[] = {
	[LAYOUT_STANDARD] = {
		.name          = "standard",
		.sx            = 1.0f,
		.sy            = 1.0f,
		.ox            = 0.0f,
		.oy            = 0.0f,
		.w             = 640.0f,
		.grid_cols     = 3,        /* 3 x 2 = JF_PAGE posters per page */
		.home_visible  = 3,        /* 3 per row, 2 rows = POSTER_SLOTS */
		.users_visible = 4,
	},

	/*
	 * 240p: the standard design at half size on the 320x240 canvas - the
	 * 640x448 layout space becomes 320x224, centred by the profile's
	 * 8-line margin. Text is the same font drawn at half size, so the
	 * screens look as in 480i - only without the flicker. The same
	 * counts: it is the same design, only smaller.
	 */
	/*
	 * 1080i (v56): a 16:9 layout space on the 960x540 canvas. The scale
	 * is 1.125 as before (448 layout lines fill 504 of the 540, the
	 * profile's 18-line margin centring them), but instead of centring
	 * the 640-wide design, the layout space itself is 960 / 1.125 = 853
	 * units wide. Every screen centres on it and stretches to it, and
	 * the counts grow: 5 posters per HOME row, a 5 x 2 library grid (10
	 * per page), 5 user tiles.
	 */
	[LAYOUT_WIDE] = {
		.name          = "wide",
		.sx            = 1.125f,
		.sy            = 1.125f,
		.ox            = 0.0f,
		.oy            = 0.0f,
		.w             = 960.0f / 1.125f,
		.grid_cols     = 5,
		.home_visible  = 5,
		.users_visible = 5,
	},

	[LAYOUT_COMPACT] = {
		.name          = "compact",
		.sx            = 0.5f,
		.sy            = 0.5f,
		.ox            = 0.0f,
		.oy            = 0.0f,
		.w             = 640.0f,
		.grid_cols     = 3,
		.home_visible  = 3,
		.users_visible = 4,
	},
};

const Layout *lay = &g_layouts[LAYOUT_STANDARD];

void layout_select(int id)
{
	if (id >= 0 && id < (int)(sizeof(g_layouts) / sizeof(g_layouts[0])))
		lay = &g_layouts[id];
}
