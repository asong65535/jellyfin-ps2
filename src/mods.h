/*
 * Loading IOP modules; see mods.c.
 */

#ifndef MODS_H
#define MODS_H

/*
 * Loads every IRX, reporting each name through `progress` before it is
 * tried. If the console freezes, the last name you saw is the culprit.
 */
typedef void (*ModsProgress)(const char *msg);

void mods_load(ModsProgress progress);

int mods_have_keyboard(void);
int mods_have_hdd(void);

/* "" when everything loaded, else a space-separated list of failures. */
const char *mods_failures(void);

#endif
