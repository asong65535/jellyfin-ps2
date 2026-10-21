/* Retained diagnostic rows, presented before each blocking probe operation. */
#ifndef PROBE_SCREEN_H
#define PROBE_SCREEN_H

#define PROBE_SCREEN_ROWS 35
#define PROBE_SCREEN_COLS 96

void probe_screen_init(void);
void probe_screen_printf(int row, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#endif
