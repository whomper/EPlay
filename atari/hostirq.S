| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
|
| hostirq.S - the DSP's host interrupt (hostirq.c): the DSP's word waiting
| (RREQ) or room for one to it (TREQ), on the 68030's level 6. Its
| requests are a level, so they are turned off in the host port (ICR)
| before the level is lowered to 5 (the MFP's keyboard, MIDI and 200 Hz
| clock answered meanwhile), and set again at the end to what the C
| function left in hirq_want. One that comes while it runs is left out.

	.text
	.globl	hirq_vec
hirq_vec:
	tst.b	hirq_busy
	bne.s	1f
	st	hirq_busy
	movem.l	%d0-%d2/%a0-%a2,-(%sp)
	andi.b	#0xfc,0xffffa200.w	| ICR: no requests while it works
	move.w	#0x2500,%sr
	movea.l	hirq_fn,%a0
	jsr	(%a0)
	ori.w	#0x0700,%sr		| (the requests set again whole)
	move.b	hirq_want,%d0
	andi.b	#3,%d0
	move.b	%d0,0xffffa200.w
	movem.l	(%sp)+,%d0-%d2/%a0-%a2
	sf	hirq_busy
1:	rte

	.bss
	.globl	hirq_busy, hirq_fn, hirq_want
	.even
hirq_fn:	.skip	4
hirq_busy:	.skip	1
hirq_want:	.skip	1
