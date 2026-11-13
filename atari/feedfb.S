| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
|
| feedfb.S - the FireBee's tick (EPLAYFB.PRG): feed.S's Timer A handler in
| ColdFire code, set by feedfb.c through Xbtimer (FireTOS's MFP: FBPROBE 2
| counted 994 a second of 997). The ColdFire has no predecrementing movem,
| no Scc to memory and no bit operation on an absolute address: the same
| steps in its forms. The in-service bit cleared at once, the level lowered
| to 5, a tick that comes while one runs left out (counted), the tick on a
| stack of its own.

FEED_STACK = 4096

	.text
	.globl	feed_vec
feed_vec:
	lea	-24(%sp),%sp
	movem.l	%d0-%d2/%a0-%a2,(%sp)
	lea	0xfffffa0f,%a0		| ISRA: Timer A no longer in service
	bclr	#5,(%a0)
	addq.l	#1,feed_ticks
	tst.b	feed_busy
	bne.s	2f
	moveq	#1,%d0
	move.b	%d0,feed_busy
	movea.l	%sp,%a1
	lea	feed_stack+FEED_STACK,%sp
	move.l	%a1,-(%sp)		| (the stack it came in on)
	move.w	#0x2500,%sr
	movea.l	feed_fn,%a0
	jsr	(%a0)
	movea.l	(%sp),%sp
	clr.b	feed_busy
	movem.l	(%sp),%d0-%d2/%a0-%a2
	lea	24(%sp),%sp
	rte
2:	addq.l	#1,feed_missed		| (left out: the one before still ran)
	movem.l	(%sp),%d0-%d2/%a0-%a2
	lea	24(%sp),%sp
	rte

	.bss
	.globl	feed_busy, feed_fn, feed_ticks, feed_missed
	.even
feed_fn:	.skip	4
feed_ticks:	.skip	4
feed_missed:	.skip	4
feed_busy:	.skip	2
	.even
feed_stack:	.skip	FEED_STACK
