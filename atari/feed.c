/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * feed.c - Timer A as the sound's tick (feed.h). The MFP is set directly,
 * in supervisor mode with interrupts off: the timer's vector ($134), its
 * control and data, and its enable and mask bits are kept and put back
 * as they were. 2457600 Hz / 16 / 154: 997 ticks a second; / 64 / 154:
 * 249.
 */
#include "tos.h"
#include "feed.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */

#define MFP(o)	(*(volatile unsigned char *)(0xfffffa00L + (o)))
#define IERA	MFP(0x07)
#define IPRA	MFP(0x0b)
#define IMRA	MFP(0x13)
#define TACR	MFP(0x19)
#define TADR	MFP(0x1f)
#define TA_VEC	(*(void * volatile *)0x134L)
#define TA_BIT	0x20

extern void feed_vec(void);
extern void (*feed_fn)(void);
extern volatile short feed_busy;

static void *old_vec;
static unsigned char old_cr, old_ier, old_imr, cr, dr;
static int on;

static unsigned short sr_off(void)
{
	unsigned short sr;
	__asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "cc");
	return sr;
}

static void sr_back(unsigned short sr)
{
	__asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "cc");
}

static long take(void)
{
	unsigned short sr = sr_off();
	old_vec = TA_VEC;
	old_cr = TACR;
	old_ier = IERA & TA_BIT;
	old_imr = IMRA & TA_BIT;
	TACR = 0;
	TA_VEC = (void *)feed_vec;
	TADR = dr;
	IPRA = (unsigned char)~TA_BIT;		/* (a pending one dropped) */
	IERA |= TA_BIT;
	IMRA |= TA_BIT;
	TACR = cr;				/* delay mode: /16 or /64 */
	sr_back(sr);
	return 0;
}

static long give_back(void)
{
	unsigned short sr = sr_off();
	TACR = 0;
	IERA = (IERA & ~TA_BIT) | old_ier;
	IMRA = (IMRA & ~TA_BIT) | old_imr;
	IPRA = (unsigned char)~TA_BIT;
	TA_VEC = old_vec;
	TACR = old_cr;
	sr_back(sr);
	return 0;
}

/* the time since feed_t0 (in the tick), from Timer A's count down and the
   ticks that came meanwhile (each left out: feed_missed), in microseconds */
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
	/* (at level 6 the next tick waits, pending, instead of being left out) */
	long n = (feed_missed - m0 + ((IPRA & TA_BIT) != 0)) * dr + t0 - t;
	return n * (cr == 3 ? 625 : 2500) / 96;	/* (16 or 64 of 2,457,600 Hz) */
}

/* the tick taken away from supervisor mode (term.c: EPlay ending however
   it ends, killed too) */
void feed_term(void)
{
	if (on) {
		give_back();
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
	Supexec(take);
	on = 1;
}

void feed_off(void)
{
	if (on) {
		Supexec(give_back);
		on = 0;
	}

}
