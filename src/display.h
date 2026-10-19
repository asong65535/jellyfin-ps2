/*
 * Display profiles: everything that depends on the output mode, in one
 * place. The screens, the drawing, the video memory layout and the video
 * requests read the current profile (disp) instead of fixed numbers, so a
 * new output mode is one more entry in display.c, not changes all over.
 *
 * Only 480i exists for now. The comment above the table in display.c lists
 * what the planned modes (480p, 240p, 1080i) are expected to contain; each
 * gets its entry in the version that makes it work and tests it.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

/* Which arrangement of the screens a profile uses (see the plan). */
enum {
	LAYOUT_STANDARD = 0,     /* 640x448, 4:3: every screen as designed */
	LAYOUT_COMPACT,          /* the same at half size, for 240 lines */
	LAYOUT_WIDE,             /* 16:9: wider screens, more items per row */
};

typedef struct {
	const char *name;          /* "480i", shown in diagnostics */

	/* The menus: positions and sizes are laid out on this canvas. */
	int canvas_w, canvas_h;

	/* The framebuffer the GS displays, and where the canvas sits in it:
	 * safe_y0 lines down, keeping the menus out of a CRT's overscan.
	 * Full-screen things (video, backgrounds) use the whole fb_h. */
	int fb_w, fb_h;
	int safe_y0;

	/* gsKit set-up: pixel format, signal mode, interlacing. */
	int psm;                   /* GS_PSM_CT24 (32-bit) or GS_PSM_CT16 */
	int dither;                /* GS dithering: hides 16-bit colour steps */
	int gs_mode;               /* GS_MODE_NTSC ... */
	int interlace;             /* GS_INTERLACED / GS_NONINTERLACED */
	int field;                 /* GS_FIELD / GS_FRAME */

	/* The screen's shape and a framebuffer pixel's shape, width/height
	 * x 1000 (1333 = 4:3, 1778 = 16:9; 1000 = square pixels). */
	int screen_shape_milli;
	int pixel_shape_milli;

	/* Video: the largest picture to ask the transcoder for, and its
	 * bitrate. Never above VIDEO_MAX_W x VIDEO_MAX_H (config.h), the
	 * decoder's own limit. */
	int video_box_w, video_box_h;
	int video_bitrate;

	int layout;                /* LAYOUT_... */
} DisplayProfile;

enum {
	DISPLAY_480I = 0,
	DISPLAY_480P,
	DISPLAY_240P,
	DISPLAY_1080I,
	DISPLAY_COUNT
};

/* The profile in use. Never NULL: 480i until display_select changes it. */
extern const DisplayProfile *disp;

/* Make profile id current (out-of-range ids are ignored). Only selects
 * the description: switching the GS itself is ui.c's job (v51). */
void display_select(int id);

/* The id of the profile in use (DISPLAY_...). */
int display_current(void);

#endif
