#ifndef OSK_H
#define OSK_H

#include <tamtypes.h>

void osk_init(void);
void osk_bind(char *buf, int size);      /* what the keys type into */

/* Returns 1 when Enter was pressed. */
int  osk_input(u32 pressed);

void osk_draw(void);

#endif
