/*
 * The splash logo; see logo.c.
 */

#ifndef LOGO_H
#define LOGO_H

#include <gsKit.h>

/*
 * Splash logo, converted from assets/logo.png at build time by
 * tools/genlogo.py. Drawn 1:1 with a NEAREST filter, so no interpolation
 * and therefore no fringing on the transparent edges.
 *
 * logo_width is 0 when no PNG was supplied, and everything here becomes a
 * no-op.
 */

extern const int logo_width, logo_height;
extern const unsigned char logo_index[];      /* one palette index per pixel */
extern const unsigned int  logo_clut[256];    /* ABGR, CSM1 order */

void logo_init(GSGLOBAL *gs);

/* After a display mode change: put the logo back in VRAM (display.h). */
void logo_reload(GSGLOBAL *gs);
int  logo_present(void);

/* x and y are the top left corner. */
void logo_draw(GSGLOBAL *gs, float x, float y);

#endif
