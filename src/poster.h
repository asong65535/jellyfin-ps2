/*
 * Artwork slots in VRAM; see poster.c.
 */

#ifndef POSTER_H_INCLUDED
#define POSTER_H_INCLUDED

#include <gsKit.h>
#include "config.h"

/*
 * Poster artwork.
 *
 * VRAM is allocated once at startup and reused for the life of the program:
 * a page's worth of slots for the grid and one for the detail page. gsKit's
 * allocator has no reliable free, so allocating per page would fragment
 * VRAM into uselessness within a few page turns.
 *
 * Slots are overwritten in place. Decoding is done by nanojpeg, which is a
 * baseline-only decoder, and Jellyfin serves baseline JPEG.
 */

/*
 * How many grid slots: one library page, so 6, or 10 in the wide layout
 * (v56). Only that many are given VRAM; storage is sized for the most.
 * The detail slot keeps a fixed index after the largest set.
 */
#define POSTER_SLOTS_MAX 10
#define POSTER_SLOTS     (lay->grid_cols * 2)
#define DETAIL_SLOT      POSTER_SLOTS_MAX   /* the larger slot, after them */
#define SLOT_COUNT       (POSTER_SLOTS_MAX + 1)

void poster_init(GSGLOBAL *gs);

/* Forget every slot's contents. Call when the page changes. */
void poster_reset(void);

/*
 * Give the slots' VRAM back, and take it again.
 *
 * gsKit cannot free, so this restores its allocation pointer to where it
 * stood before the slots were taken. Only valid because slots are allocated
 * last: anything allocated after them would be released too.
 *
 * Between the two calls no slot may be drawn, and the artwork has to be
 * fetched again afterwards.
 */
void poster_release(GSGLOBAL *gs);
void poster_reinit(GSGLOBAL *gs);

/* After a display mode change: the slots again, after the new framebuffers. */
void poster_rebuild(GSGLOBAL *gs);

/* Forget one slot, so a failed load shows a placeholder rather than a
 * leftover image. */
void poster_clear(int slot);

/*
 * Decode an encoded image into a slot and upload it. Returns 0 on success.
 * A failure leaves the slot empty, and the caller should draw a placeholder.
 */
int  poster_upload(GSGLOBAL *gs, int slot,
                   const unsigned char *data, int len);

int  poster_ready(int slot);

/* Decoded dimensions of what is in a slot. For diagnostics: an image whose
 * size is unusual is the likeliest explanation for a display fault. */
void poster_dims(int slot, int *w, int *h);

/* Draws into the given box, scaling to fit. Does nothing if not ready. */
void poster_draw(GSGLOBAL *gs, int slot, float x, float y, float w, float h);

#endif  /* POSTER_H_INCLUDED */