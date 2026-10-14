#include "freeze.h"
#include "net.h"

#include <kernel.h>

#define FREEZE_BLANKS 120            /* two seconds without a frame */
#define PARKED_PRIO   0x7E           /* below every thread that matters */

static volatile int      g_tid[FZ_COUNT] = { -1, -1, -1, -1, -1, -1, -1 };
static volatile unsigned g_ticks, g_seen;
static volatile int      g_stale;
static volatile char     g_code;     /* set by the interrupt, taken once */
static volatile int      g_armed;

static const char g_letter[FZ_COUNT] = { 'U', 'D', 'S', 'N', 'R', 'M', 'P' };

void freeze_register(int tid, int which)
{
	if (which >= 0 && which < FZ_COUNT)
		g_tid[which] = tid;
}

void freeze_tick(void) { g_ticks++; }

void freeze_arm(int on)
{
	g_seen  = g_ticks;              /* a fresh start either way */
	g_stale = 0;
	g_armed = on ? 1 : 0;
}

char freeze_take(void)
{
	char c = g_code;

	g_code = 0;
	return c;
}

/* Nothing can draw: both display circuits off, the GS background colour. */
static void colour(unsigned r, unsigned g, unsigned b)
{
	*(volatile u64 *)0x120000E0 = (u64)r | (u64)g << 8 | (u64)b << 16;
	*(volatile u64 *)0x12000000 = 0;
}

static void watch(void)
{
	if (!g_armed) {
		g_stale = 0;
		return;
	}

	if (g_ticks != g_seen) {
		g_seen  = g_ticks;
		g_stale = 0;
		return;
	}

	if (++g_stale != FREEZE_BLANKS)
		return;                      /* acts once per stall */

	int tid = _iGetThreadId();       /* the thread this interrupt broke into */
	int who = -1;

	for (int i = 0; i < FZ_COUNT; i++)
		if (g_tid[i] >= 0 && g_tid[i] == tid)
			who = i;

	if (who > FZ_UI) {
		/* A worker hogging the CPU: set it aside, the screen goes on. */
		iChangeThreadPriority(tid, PARKED_PRIO);
		g_code = g_letter[who];
		return;
	}

	if (who == FZ_UI)
		colour(0xFF, 0xFF, 0x00);    /* U: the screen thread, looping */
	else
		colour(0xFF, 0xFF, 0xFF);    /* W: all waiting, or unknown */
}

void freeze_init(void)
{
	net_add_vblank_hook(watch);
}
