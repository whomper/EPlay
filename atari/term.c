/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * term.c - the terminate vector (term.h). Its handler runs in supervisor
 * mode with no XBIOS: the vectors and the MFP as they were, the DMA's play
 * off, the matrix as EPlay found it; then the vector it took over.
 */
#include "tos.h"
#include "feed.h"
#include "hostirq.h"
#include "snd.h"
#include "term.h"
#include "ym.h"

long term_old;
void term_vec(void);
void term_c(void);

void term_c(void)
{
	feed_term();
	ym_term();		/* (a VGM: the chip silent, the click and bell back) */
	hostirq_term();
	snd_term();
}

__asm__(
	"	.text\n"
	"	.globl	term_vec\n"
	"term_vec:\n"
	"	movem.l	%d0-%d2/%a0-%a2,-(%sp)\n"
	"	jsr	term_c\n"
	"	movem.l	(%sp)+,%d0-%d2/%a0-%a2\n"
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
		: "g"(vec), "g"(fn)
		: "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	return r;
}

void term_install(void)
{
	if (!term_old)
		term_old = setexc(0x102, (long)term_vec);
}
