| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
|
| feed.S - the MFP's Timer A interrupt that feeds the sound (feed.c):
| the in-service bit cleared at once (TOS runs the MFP in software
| end-of-interrupt mode), the processor's level lowered to 5 so that the
| keyboard, the MIDI port and the 200 Hz clock (the MFP's, level 6) are
| answered while the tick works, and a tick that comes while one runs is
| left out. The tick is feed_fn, a C function (d0-d1/a0-a1 are its to
| change; d2/a2 kept as well). It runs on its own stack (feed_stack), not
| on the one the interrupt came in on: that may be the AES's or the VDI's,
| deep in a call with little room left, and the YM2612's tick takes some
| 700 bytes (EPlay 1.18 overran the AES's in a wind_get: Erez, 11 Nov 2026).
| One stack does, as a tick never runs inside another.

FEED_STACK = 4096		| the tick's, and an interrupt or two above it

	.text
	.globl	feed_vec
feed_vec:
	bclr	#5,0xfffffa0f.w		| ISRA: Timer A no longer in service
	tst.b	feed_busy
	bne.s	1f
	st	feed_busy
	movem.l	%d0-%d2/%a0-%a2,-(%sp)
	movea.l	%sp,%a1
	lea	feed_stack+FEED_STACK,%sp
	move.l	%a1,-(%sp)		| (the stack it came in on)
	move.w	#0x2500,%sr
	movea.l	feed_fn,%a0
	jsr	(%a0)
	movea.l	(%sp),%sp
	movem.l	(%sp)+,%d0-%d2/%a0-%a2
	sf	feed_busy
1:	rte

	.bss
	.globl	feed_busy, feed_fn
	.even
feed_fn:	.skip	4
feed_busy:	.skip	2
	.even
feed_stack:	.skip	FEED_STACK
