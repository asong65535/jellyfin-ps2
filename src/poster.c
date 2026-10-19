/*
 * Artwork. A fixed set of VRAM slots (one page's worth for grids and rows,
 * one for a film's page), JPEG and PNG decoding into them, and drawing them fitted
 * to a box. The slots' VRAM is lent to the video texture during playback
 * and rebuilt after.
 */

#include "poster.h"
#include "ui.h"

#include <malloc.h>
#include <string.h>

#include <turbojpeg.h>

/*
 * TurboJPEG rather than nanojpeg: libturbojpeg is already in the image, and
 * TJPF_RGBA writes bytes in the order R, G, B, A, which read as a
 * little-endian word is exactly the GS's CT32 layout. So it decodes straight
 * into the texture buffer with no conversion pass.
 *
 * The one fixup needed is alpha. TurboJPEG fills it with 255, and PS2 alpha
 * runs 0 to 128 where 128 is opaque.
 */

typedef struct {
	GSTEXTURE tex;
	u32      *pixels;
	int       max_w, max_h;   /* the allocation, and the sampling stride */
	int       img_w, img_h;   /* what the decoded image actually fills */
	int       ready;
	int       used;           /* given VRAM in the current layout */
} Slot;

static Slot g_slot[SLOT_COUNT];
static int  g_inited;

/*
 * Reserve a slot's VRAM texture (width rounded up to 64 for the GS) and
 * its main-memory pixel buffer.
 */
static void slot_alloc(GSGLOBAL *gs, Slot *s, int w, int h)
{
	/*
	 * Round the width up to 64. The GS keeps a texture's buffer width in
	 * units of 64 pixels, so a slot that is not a multiple of that is read
	 * back with a wider stride than it was written with, and every row
	 * lands slightly wrong.
	 */
	w = (w + 63) & ~63;

	s->max_w = w;
	s->max_h = h;

	/*
	 * Keep the pixels if there are already some.
	 *
	 * This runs again when the VRAM is reclaimed after video, and
	 * allocating a fresh buffer there threw away the decoded artwork -
	 * which is the copy poster_reinit uploads from, so the grid came back
	 * blank. It also leaked the old buffer every time.
	 *
	 * Only the VRAM is being reassigned; main memory is untouched, and
	 * img_w and img_h still describe what is in it.
	 */
	if (!s->pixels) {
		s->pixels = memalign(128, w * h * 4);
		if (!s->pixels)
			return;

		memset(s->pixels, 0, w * h * 4);

		s->img_w = w;
		s->img_h = h;
		s->ready = 0;
	}

	s->tex.Width    = w;
	s->tex.Height   = h;
	s->tex.PSM      = GS_PSM_CT32;
	s->tex.Filter   = GS_FILTER_LINEAR;   /* photographs, so smooth is right */
	s->tex.Mem      = s->pixels;
	s->tex.Vram     = gsKit_vram_alloc(
	                      gs, gsKit_texture_size(w, h, GS_PSM_CT32),
	                      GSKIT_ALLOC_USERBUFFER);
	s->tex.VramClut = 0;
	s->tex.Clut     = NULL;
}

/*
 * Where VRAM stood before any slot was allocated.
 *
 * gsKit allocates by bumping GSGLOBAL::CurrentPointer and has no free, so
 * the only way to reclaim anything is to put that pointer back. Slots are
 * allocated last, after the framebuffers, font atlas and logo, so restoring
 * this mark releases exactly the slots and nothing else.
 *
 * Video needs it: a 640x448 texture is 1.25 MB and only 0.97 MB is free with
 * artwork resident. The slots are 0.65 MB, which is enough.
 */
static u32 g_vram_mark;
static int g_mark_set;

/*
 * Allocate every poster slot once, recording the VRAM mark they start at
 * so they can be released and rebuilt identically.
 */
void poster_init(GSGLOBAL *gs)
{
	if (g_inited)
		return;

	/*
	 * Record the mark once, and afterwards return to it.
	 *
	 * Re-recording it here was a real bug: on the way back from video the
	 * pointer still sat past the video texture, so the slots were
	 * allocated on top of it - 2.37 MB of framebuffers and fonts, plus
	 * 1.25 MB of video texture, plus 0.65 MB of slots, against 4 MB of
	 * VRAM. Allocation then failed or wrapped, and artwork came back
	 * missing or drawn over something else.
	 */
	if (!g_mark_set) {
		g_vram_mark = gs->CurrentPointer;
		g_mark_set  = 1;
	} else {
		gs->CurrentPointer = g_vram_mark;
	}

	/*
	 * A page's worth of grid slots - 6, or 10 in the wide layout - and
	 * the detail slot. Slots beyond the page get no VRAM and are never
	 * ready: after a switch back from the wide layout their texture
	 * addresses would point past the new allocation.
	 */
	for (int i = 0; i < POSTER_SLOTS_MAX; i++) {
		g_slot[i].used = (i < POSTER_SLOTS);
		if (g_slot[i].used)
			slot_alloc(gs, &g_slot[i], POSTER_MAX_W, POSTER_MAX_H);
		else
			g_slot[i].ready = 0;
	}

	g_slot[DETAIL_SLOT].used = 1;
	slot_alloc(gs, &g_slot[DETAIL_SLOT], DETAIL_MAX_W, DETAIL_MAX_H);

	g_inited = 1;
}

/* Which slots held artwork when the VRAM was given up. */
static int g_was_ready[SLOT_COUNT];

/*
 * Give the poster VRAM back (for the video texture), keeping the decoded
 * pixels in main memory.
 */
void poster_release(GSGLOBAL *gs)
{
	if (!g_inited)
		return;

	/*
	 * The decoded pixels stay in main memory; only the VRAM goes back. So
	 * remember which slots were showing something, and nothing may draw a
	 * slot until poster_reinit has put it back.
	 */
	for (int i = 0; i < SLOT_COUNT; i++) {
		g_was_ready[i]  = g_slot[i].ready;
		g_slot[i].ready = 0;
	}

	gs->CurrentPointer = g_vram_mark;
	g_inited = 0;
}

/*
 * Rebuild the slots at the recorded mark after video, re-uploading what
 * they showed.
 */
void poster_reinit(GSGLOBAL *gs)
{
	/*
	 * Back to the mark first, which reclaims the video texture as well as
	 * anything else taken since. Then the same allocations in the same
	 * order, so the layout is identical to what it was before.
	 */
	gs->CurrentPointer = g_vram_mark;

	poster_init(gs);

	/*
	 * Upload again from main memory.
	 *
	 * Allocating the VRAM only reserves it; the pixels have to be sent
	 * back. Without this the grid returns empty and stays that way until
	 * something forces a refetch, which is not obvious to the person
	 * looking at it.
	 */
	for (int i = 0; i < SLOT_COUNT; i++) {
		if (!g_was_ready[i] || !g_slot[i].pixels || !g_slot[i].used)
			continue;

		gsKit_texture_upload(gs, &g_slot[i].tex);
		g_slot[i].ready = 1;
		g_was_ready[i]  = 0;
	}
}

/*
 * After a display mode change: the framebuffers may have a new size, so
 * the slots start at a new place. Record a new mark there, reserve every
 * slot again, and upload the artwork they held from main memory.
 */
void poster_rebuild(GSGLOBAL *gs)
{
	for (int i = 0; i < SLOT_COUNT; i++) {
		g_was_ready[i] = g_slot[i].ready;
		g_slot[i].ready = 0;
	}

	g_mark_set = 0;                    /* a new mark, after the new framebuffers */
	g_inited   = 0;

	poster_init(gs);

	for (int i = 0; i < SLOT_COUNT; i++) {
		if (!g_was_ready[i] || !g_slot[i].pixels || !g_slot[i].used)
			continue;

		gsKit_texture_upload(gs, &g_slot[i].tex);
		g_slot[i].ready = 1;
		g_was_ready[i]  = 0;
	}
}

/* Mark every slot empty, without freeing anything. */
void poster_reset(void)
{
	for (int i = 0; i < SLOT_COUNT; i++)
		g_slot[i].ready = 0;
}

/* Mark one slot empty. */
void poster_clear(int slot)
{
	if (slot >= 0 && slot < SLOT_COUNT)
		g_slot[slot].ready = 0;
}

/* The decoded image size held in a slot (0 x 0 if none). */
void poster_dims(int slot, int *w, int *h)
{
	if (slot < 0 || slot >= SLOT_COUNT) { *w = 0; *h = 0; return; }

	*w = g_slot[slot].img_w;
	*h = g_slot[slot].img_h;
}

/* Whether a slot holds an image that can be drawn. */
int poster_ready(int slot)
{
	if (slot < 0 || slot >= SLOT_COUNT || !g_slot[slot].used)
		return 0;
	return g_slot[slot].ready;
}

/*
 * Decode a JPEG (or PNG) into a slot and upload it to VRAM. 0 on success;
 * on failure the slot stays empty rather than showing the previous image.
 */
int poster_upload(GSGLOBAL *gs, int slot,
                  const unsigned char *data, int len)
{
	if (!g_inited || slot < 0 || slot >= SLOT_COUNT || !g_slot[slot].used ||
	    len <= 0)
		return -1;

	Slot *s = &g_slot[slot];
	if (!s->pixels)
		return -1;

	/*
	 * Invalidate first. Without this a failed decode leaves the previous
	 * image in the slot and it keeps being drawn, which made every detail
	 * page show the first poster that ever loaded.
	 */
	s->ready = 0;

	tjhandle tj = tjInitDecompress();
	if (!tj)
		return -1;

	int w = 0, h = 0, subsamp = 0, cs = 0;

	if (tjDecompressHeader3(tj, data, (unsigned long)len,
	                        &w, &h, &subsamp, &cs) != 0) {
		tjDestroy(tj);
		return -1;
	}

	if (w <= 0 || h <= 0 || w > s->max_w || h > s->max_h) {
		/*
		 * Larger than the slot. The request bounds both axes now, so this
		 * should be rare; treat it as a miss rather than scaling on the EE.
		 * -2 rather than -1 so the caller can tell it apart from a decode
		 * failure, which needs a completely different fix.
		 */
		tjDestroy(tj);
		return -2;
	}

	/*
	 * Wipe first, so a smaller image following a larger one cannot leave
	 * old pixels showing along the edges under linear filtering.
	 */
	memset(s->pixels, 0, s->max_w * s->max_h * 4);

	/*
	 * Decode straight into the texture buffer. The pitch is the slot width,
	 * not the image width, so a narrower image sits in the top left.
	 */
	/*
	 * STOPONWARNING is essential here, not an optimisation.
	 *
	 * By default TurboJPEG salvages what it can from damaged data and
	 * reports success, which produced posters full of colour noise. Since
	 * the segment bug removes bytes from the middle and leaves both markers
	 * intact, a decode that refuses to guess is the only reliable proof an
	 * image arrived whole.
	 */
	int rc = tjDecompress2(tj, data, (unsigned long)len,
	                       (unsigned char *)s->pixels,
	                       w, s->max_w * 4, h,
	                       TJPF_RGBA, TJFLAG_FASTDCT | TJFLAG_STOPONWARNING);

	tjDestroy(tj);

	if (rc != 0)
		return -1;

	/* TurboJPEG writes alpha 255. The GS wants 128 for fully opaque. */
	for (int y = 0; y < h; y++) {
		u32 *row = &s->pixels[y * s->max_w];

		for (int x = 0; x < w; x++)
			row[x] = (row[x] & 0x00FFFFFFu) | 0x80000000u;
	}

	/*
	 * Upload the whole slot, and leave the texture dimensions alone.
	 *
	 * tex.Width is the stride gsKit samples with, not just a size. Setting
	 * it to the image width after uploading at the slot width made every row
	 * read from slightly the wrong place, which looked exactly like a
	 * corrupt JPEG: skewed bands of colour. The real size is kept separately
	 * and applied as texture coordinates when drawing.
	 */
	gsKit_texture_upload(gs, &s->tex);

	s->img_w = w;
	s->img_h = h;
	s->ready = 1;

	return 0;
}

/*
 * Draw a slot's image fitted into a box, keeping its proportions (posters
 * are 2:3, album covers square).
 */
void poster_draw(GSGLOBAL *gs, int slot, float x, float y, float w, float h)
{
	if (!poster_ready(slot))
		return;

	Slot *s = &g_slot[slot];

	if (s->img_w <= 0 || s->img_h <= 0)
		return;

	/*
	 * Fit inside the box without distorting it.
	 *
	 * Film posters are 2:3 and match the tile exactly, but album covers are
	 * square, and stretching one into a 2:3 tile makes it obviously wrong.
	 * Scale to whichever axis runs out first and centre the remainder.
	 */
	float sx = w / (float)s->img_w;
	float sy = h / (float)s->img_h;
	float sc = (sx < sy) ? sx : sy;

	float dw = s->img_w * sc;
	float dh = s->img_h * sc;
	float dx = x + (w - dw) * 0.5f;
	float dy = y + (h - dh) * 0.5f;

	/* Layout space to canvas (layout.h), then canvas to framebuffer. */
	dx = LAY_X(dx);
	dy = LAY_Y(dy) + SCR_Y0;
	dw = LAY_W(dw);
	dh = LAY_H(dh);

	/* Sample only the part of the slot the image fills. The texture keeps
	 * its full width, which is the stride. */
	ui_dither(1);                  /* pictures are dithered (16-bit only) */

	gsKit_prim_sprite_texture(gs, &s->tex,
	                          dx, dy,
	                          0.0f, 0.0f,
	                          dx + dw, dy + dh,
	                          (float)s->img_w, (float)s->img_h,
	                          3,
	                          GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0));

	ui_dither(0);
}