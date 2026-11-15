/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * term.c - the terminate vector (term.h). Its handler runs in supervisor
 * mode: the vectors and the MFP as they were, the DMA's play off, the
 * matrix as EPlay found it; the sound system's lock and the DSP's given
 * back where EPlay holds them (Unlocksnd, Dsp_Unlock: the XBIOS's two
 * flags, called as GEMDOS ends the process); itself off the vector; then
 * the one it took over. Under MiNT it runs at a memory violation, an
 * illegal instruction or a kill too (not at SIGKILL): a tester's CT60
 * under FreeMiNT, 15 Nov 2026, found the sound and the DSP still locked
 * after a crash, until a reboot.
 *
 * Its entry has an XBRA header ('XBRA', 'EPLY', the one before it), as the
 * guidelines want, and it takes itself off as EPlay ends: from the vector
 * when it is on top, else out of the XBRA chain of what hooked it after.
 * Without that, etv_term led into EPlay's freed memory once it had quit on
 * plain TOS (MiNT keeps the vector per process), and the next program to
 * end bombed; and the crash catcher, under it, couldn't take itself out
 * either (the hub, 10 Nov 2026: EFTP's and EMail's quit bombs; EPlay's
 * quit case found "etv_term: hooked over, not taken off").
 */
#include "tos.h"
#include "feed.h"
#include "hostirq.h"
#include "snd.h"
#include "term.h"
#include "ym.h"
#include "dsplock.h"

volatile short dsp_held;

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */

extern long term_old;		/* (below: the XBRA header's "before") */
void term_vec(void);
void term_c(void);

#define XBRA 0x58425241L
#define ETV_TERM ((volatile long *)0x408L)

/* term_vec off etv_term: from the vector, or out of the XBRA chain over it */
static void term_unhook(void)
{
	long a = *ETV_TERM;
	int n;
	if (a == (long)term_vec) {
		*ETV_TERM = term_old;
		return;
	}
	for (n = 0; n < 16 && a && !(a & 1); n++) {
		long *p = (long *)a;
		if (p[-3] != XBRA)
			return;		/* (hooked over without XBRA: nothing to walk) */
		if (p[-1] == (long)term_vec) {
			p[-1] = term_old;
			return;
		}
		a = p[-1];
	}
}

void term_c(void)
{
	term_unhook();
	feed_term();
	ym_term();		/* (a VGM: the chip silent, the click and bell back) */
	hostirq_term();
	snd_term();		/* (the sound system's lock too, if EPlay's) */
	if (dsp_held)
		dsp_unlock();
}

__asm__(
	"	.text\n"
	"	.even\n"
	"	.long	0x58425241, 0x45504C59\n"	/* 'XBRA', 'EPLY' */
	"	.globl	term_old\n"
	"term_old:\n"
	"	.long	0\n"
	"	.globl	term_vec\n"
	"term_vec:\n"
#ifdef __mcoldfire__
	/* (the ColdFire: no predecrementing movem) */
	"	lea	-24(%sp),%sp\n"
	"	movem.l	%d0-%d2/%a0-%a2,(%sp)\n"
	"	jsr	term_c\n"
	"	movem.l	(%sp),%d0-%d2/%a0-%a2\n"
	"	lea	24(%sp),%sp\n"
#else
	"	movem.l	%d0-%d2/%a0-%a2,-(%sp)\n"
	"	jsr	term_c\n"
	"	movem.l	(%sp)+,%d0-%d2/%a0-%a2\n"
#endif
	"	move.l	term_old,-(%sp)\n"
	"	rts\n");

/* Setexc(vec, new): the old one */
static long setexc(short vec, long fn)
{
	register long r __asm__("d0");
	__asm__ volatile(
		"move.l	%2,-(%%sp)\n\t"
		"move.w	%1,-(%%sp)\n\t"
		"move.w	#5,-(%%sp)\n\t"
		"trap	#13\n\t"
		"addq.l	#8,%%sp"
		: "=r"(r)
		: "r"(vec), "r"(fn)
		: "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	return r;
}

void term_install(void)
{
	if (!term_old)
		term_old = setexc(0x102, (long)term_vec);
}
