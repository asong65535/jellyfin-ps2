/*
 * The display profiles; see display.h.
 *
 * Planned entries, not active - each is added, with its values checked on
 * hardware, in the version that makes it work:
 *
 *   540p (later, optional): the 1080i framebuffer and layout, with the
 *                1080i timing made progressive.
 */

#include "display.h"
#include "layout.h"
#include "config.h"

#include <gsKit.h>

/*
 * The framebuffer format of the 4:3 modes: 32-bit, or 16-bit with
 * dithering when TEST_16BIT_FB is set (config.h) - a test of what 1080i
 * will need, on the displays that exist before it.
 */
#if TEST_16BIT_FB
#define FB_PSM    GS_PSM_CT16
#define FB_DITHER 1
#else
#define FB_PSM    GS_PSM_CT24
#define FB_DITHER 0
#endif

static const DisplayProfile g_profiles[DISPLAY_COUNT] = {
	[DISPLAY_480I] = {
		.name               = "480i",
		.canvas_w           = 640,
		.canvas_h           = 448,
		.fb_w               = 640,
		.fb_h               = 480,
		.safe_y0            = 16,          /* (480 - 448) / 2 */
		.psm                = FB_PSM,
		.dither             = FB_DITHER,
		.gs_mode            = GS_MODE_NTSC,
		.interlace          = GS_INTERLACED,
		.field              = GS_FIELD,
		.screen_shape_milli = 1333,
		.pixel_shape_milli  = 1000,
		.video_box_w        = VIDEO_MAX_W,
		.video_box_h        = VIDEO_MAX_H,
		.video_bitrate      = VIDEO_BITRATE,
		.layout             = LAYOUT_STANDARD,
	},

	/*
	 * 480p: the same 640x480 picture, sent progressively - every line on
	 * every refresh, no interlace flicker. Needs a component cable and a
	 * display that accepts 31 kHz. The menus keep their 448-line layout,
	 * centred; a flat screen shows the 16-line margins as background.
	 */
	[DISPLAY_480P] = {
		.name               = "480p",
		.canvas_w           = 640,
		.canvas_h           = 448,
		.fb_w               = 640,
		.fb_h               = 480,
		.safe_y0            = 16,
		.psm                = FB_PSM,
		.dither             = FB_DITHER,
		.gs_mode            = GS_MODE_DTV_480P,
		.interlace          = GS_NONINTERLACED,
		.field              = GS_FRAME,
		.screen_shape_milli = 1333,
		.pixel_shape_milli  = 1000,
		.video_box_w        = VIDEO_MAX_W,
		.video_box_h        = VIDEO_MAX_H,
		.video_bitrate      = VIDEO_BITRATE,
		.layout             = LAYOUT_STANDARD,
	},

	/*
	 * 240p: NTSC without interlacing - the same 240 lines on every
	 * field, the classic console signal: no flicker, visible scanlines.
	 * A 320x240 canvas with square pixels; the menus use the compact
	 * layout (the design at half size, with the small font), and video
	 * is asked at the size the screen shows, at a lower bitrate.
	 */
	[DISPLAY_240P] = {
		.name               = "240p",
		.canvas_w           = 320,
		.canvas_h           = 224,         /* the 448-line design, halved */
		.fb_w               = 320,
		.fb_h               = 240,
		.safe_y0            = 8,           /* (240 - 224) / 2 */
		.psm                = FB_PSM,
		.dither             = FB_DITHER,
		.gs_mode            = GS_MODE_NTSC,
		.interlace          = GS_NONINTERLACED,
		.field              = GS_FRAME,
		.screen_shape_milli = 1333,
		.pixel_shape_milli  = 1000,
		.video_box_w        = 320,
		.video_box_h        = 240,
		.video_bitrate      = 700000,
		.layout             = LAYOUT_COMPACT,
	},

	/*
	 * 1080i: a 960x540 framebuffer in 16-bit colour (two of them at
	 * 32-bit, plus the video texture, would not fit in the GS's 4 MB),
	 * dithered, sent as 1080i: the display circuit doubles it to
	 * 1920x1080, and each field shows all 540 lines (frame mode). A 16:9
	 * screen with square pixels. The menus use the wide layout; the
	 * video fills the 16:9 screen.
	 */
	[DISPLAY_1080I] = {
		.name               = "1080i",
		.canvas_w           = 960,
		.canvas_h           = 504,         /* the 448-line design x 1.125 */
		.fb_w               = 960,
		.fb_h               = 540,
		.safe_y0            = 18,          /* (540 - 504) / 2 */
		.psm                = GS_PSM_CT16,
		.dither             = 1,
		.gs_mode            = GS_MODE_DTV_1080I,
		.interlace          = GS_INTERLACED,
		.field              = GS_FRAME,
		.screen_shape_milli = 1778,
		.pixel_shape_milli  = 1000,
		.video_box_w        = VIDEO_MAX_W,
		.video_box_h        = VIDEO_MAX_H,
		.video_bitrate      = VIDEO_BITRATE,
		.layout             = LAYOUT_WIDE,
	},
};

const DisplayProfile *disp = &g_profiles[DISPLAY_480I];

void display_select(int id)
{
	if (id >= 0 && id < DISPLAY_COUNT) {
		disp = &g_profiles[id];
		layout_select(disp->layout);     /* the profile's arrangement */
	}
}

int display_current(void)
{
	return (int)(disp - g_profiles);
}
