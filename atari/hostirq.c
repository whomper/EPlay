/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * hostirq.c - the DSP's host interrupt (hostirq.h): vector 255 ($3FC),
 * as TOS names it in the host port's IVR, taken and given back with the
 * IVR and the ICR as they were.
 */
#include "tos.h"
#include "hostirq.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */

#define ICR	(*(volatile unsigned char *)0xffffa200L)
#define IVR	(*(volatile unsigned char *)0xffffa203L)
#define VEC	(*(void * volatile *)(255 * 4L))

extern void hirq_vec(void);
extern void spur_vec(void);
#define SPUR	(*(void * volatile *)(24 * 4L))
static void *old_spur;
static int spur_on_;
extern void (*hirq_fn)(void);

static void *old_vec;
static unsigned char old_icr, old_ivr;
static int on;

static long take(void)
{
	unsigned short sr;
	__asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "cc");
	old_vec = VEC;
	old_icr = ICR;
	old_ivr = IVR;
	VEC = (void *)hirq_vec;
	IVR = 255;
	ICR = (old_icr & ~3) | (hirq_want & 3);
	__asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "cc");
	return 0;
}

static long give_back(void)
{
	unsigned short sr;
	__asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "cc");
	ICR = old_icr & ~3;
	IVR = old_ivr;
	VEC = old_vec;
	__asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "cc");
	return 0;
}

void hostirq_on(void (*fn)(void))
{
	if (on)
		hostirq_off();
	hirq_fn = fn;
	hirq_want = HIRQ_RX;
	Supexec(take);
	on = 1;
}

void hostirq_off(void)
{
	if (on) {
		Supexec(give_back);
		on = 0;
	}
}

static long spur_give(void);

/* the host port's interrupt and the spurious one given back, from
   supervisor mode (term.c) */
void hostirq_term(void)
{
	if (on) {
		give_back();
		on = 0;
	}
	if (spur_on_) {
		spur_give();
		spur_on_ = 0;
	}
}

/* vector 24, the spurious interrupt, answered by counting it (hirq_spur)
   while EPlay plays, and given back */
static long spur_take(void)
{
	unsigned short sr;
	__asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "cc");
	old_spur = SPUR;
	SPUR = (void *)spur_vec;
	__asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "cc");
	return 0;
}

static long spur_give(void)
{
	unsigned short sr;
	__asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "cc");
	SPUR = old_spur;
	__asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "cc");
	return 0;
}

void spurious_on(void)
{
	if (!spur_on_) {
		hirq_spur = 0;
		Supexec(spur_take);
		spur_on_ = 1;
	}
}

void spurious_off(void)
{
	if (spur_on_) {
		Supexec(spur_give);
		spur_on_ = 0;
	}
}
