/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * feedfb.c - the tick where the sound is the XBIOS's alone (EPLAYFB.PRG's
 * feed.h; EPLAY.PRG's feedxb.o): no interrupt at all. mxPlay's way on the
 * FireBee (its plugins "100% system friendly, not a single HW access":
 * Timer A only in its ARAnyM build, whose DMA pointer runs away): the
 * XBIOS plays a ring of two seconds, and GEM's turns refill it. Each turn
 * (feed_poll, from play_step: the window's, a dialog's, the file dialog's)
 * runs the tick once, in supervisor mode by Supexec, after snd_poll has
 * asked Buffptr where the sound plays. No vector, no MFP register, no
 * Xbtimer: 1.38's FireBee went to a black screen at each Pause, seek and
 * track's end with the tick on Timer A (the tester, 15 Nov 2026), and
 * mxPlay's notes warn of the same from a player that takes the OS's
 * vectors or timers. The time in the tick from the 200 Hz clock alone.
 */
#include "tos.h"
#include "feed.h"
#include "snd.h"
#ifdef FEED_XB_IN
/* (EPLAY.PRG's: this file built again as feedxb.o, feed.c giving its calls
   to these where the sound is the XBIOS's alone: snd_xb) */
#include "feedxb.h"
#define feed_t0 fx_t0
#define feed_us fx_us
#define feed_term fx_term
#define feed_on fx_on
#define feed_off fx_off
#define feed_poll fx_poll
#else
volatile long feed_ticks, feed_missed;
#endif

#pragma GCC diagnostic ignored "-Warray-bounds"	/* (a system variable's fixed address) */
#define HZ200 (*(volatile unsigned long *)0x4baL)

static void (*fn)(void);
static int on, busy;
static long rate;
static unsigned long h0, t0;

static long fb_mark(void)
{
	h0 = HZ200;
	return 0;
}

/* (supervisor mode) the ticks the rate would have had since feed_on, for
   whoever counts them (feed_ticks), and the tick */
static long fb_run(void)
{
	int k;
	feed_ticks = (long)((HZ200 - h0) * (unsigned long)rate / 200);
	/* (play.c's tick puts 1,024 frames at most, the Falcon's Timer A's
	   way; here a turn fills all the ring's room, so the tick again
	   until it puts nothing: the Falcon's tick code left as it was, where
	   its place in the 68030's cache counts) */
	for (k = 0; k < 256; k++) {
		long w = snd_written();
		fn();
		if (snd_written() == w)
			break;
	}
	return 0;
}

void feed_poll(void)
{
	if (on && !busy) {
		busy = 1;
		Supexec(fb_run);
		busy = 0;
	}
}

/* (in the tick: supervisor mode) */
void feed_t0(void)
{
	t0 = HZ200;
}

long feed_us(void)
{
	return (long)(HZ200 - t0) * 5000;
}

void feed_term(void)
{
	on = 0;
}

void feed_on(void (*tick)(void), int hz)
{
	fn = tick;
	rate = hz;
	feed_ticks = feed_missed = 0;
	Supexec(fb_mark);
	on = 1;
}

void feed_off(void)
{
	on = 0;
}
