/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * feedfb.c - the FireBee's tick (feed.h), as feed.c's on Timer A, set
 * through the XBIOS rather than the MFP's registers: FireTOS keeps the
 * ColdFire's vectors where its VBR is, not at $134. Xbtimer sets the
 * vector, the control and the data and enables the interrupt (FBPROBE 2:
 * 994 a second of 997); off, the timer stopped, its interrupt disabled
 * (Jdisint) and the vector as it was (Setexc). The time in the tick from
 * Timer A's count, read at its register (FBPROBE: the MFP answers there).
 */
#include "tos.h"
#include "feed.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */

#define MFP(o)	(*(volatile unsigned char *)(0xfffffa00L + (o)))
#define IPRA	MFP(0x0b)
#define TADR	MFP(0x1f)
#define TA_BIT	0x20
#define TA_VECNO 77		/* $134 / 4: the MFP's base $40, Timer A 13 */

extern void feed_vec(void);
extern void (*feed_fn)(void);
extern volatile short feed_busy;

static long old_vec;
static unsigned char cr, dr;
static int on;

/* XBIOS Xbtimer (31): timer, control, data, vector; Jdisint (26) */
static long xbtimer(short t, short c, short d, long v)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %5,-(%%sp)\n\tmove.w %4,-(%%sp)\n\tmove.w %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\t"
			 "move.w %1,-(%%sp)\n\ttrap #14\n\tlea 12(%%sp),%%sp"
			 : "=r"(r) : "r"((short)31), "r"(t), "r"(c), "r"(d), "r"(v) : TRAP_CLOBBER);
	return r;
}

static long setexc(short n, long v)
{
	return trap13_wwl(5, n, v);
}

/* the time since feed_t0 (in the tick), from Timer A's count down and the
   ticks that came meanwhile, in microseconds (feed.c's) */
static unsigned char t0;
static long m0;

void feed_t0(void)
{
	t0 = TADR;
	m0 = feed_missed;
}

long feed_us(void)
{
	unsigned char t = TADR;
	long n = (feed_missed - m0 + ((IPRA & TA_BIT) != 0)) * dr + t0 - t;
	return n * (cr == 3 ? 625 : 2500) / 96;
}

static void stop(void)
{
	xbtimer(0, 0, 0, old_vec);
	trap14_ww(26, 13);		/* Jdisint: Timer A */
	setexc(TA_VECNO, old_vec);
}

void feed_term(void)
{
	if (on) {
		stop();
		on = 0;
	}
}

void feed_on(void (*tick)(void), int hz)
{
	if (on)
		feed_off();
	/* 1000 a second: /16 and 154; 500: /64 and 77; 250: /64 and 154 */
	cr = hz > 500 ? 3 : 5;
	dr = hz > 250 && hz <= 500 ? 77 : 154;
	feed_fn = tick;
	feed_busy = 0;
	feed_ticks = feed_missed = 0;
	old_vec = setexc(TA_VECNO, -1L);
	xbtimer(0, cr, dr, (long)feed_vec);
	on = 1;
}

void feed_off(void)
{
	if (on) {
		stop();
		on = 0;
	}
}
