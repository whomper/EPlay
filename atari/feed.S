| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
|
| feed.S - the MFP's Timer A interrupt that feeds the sound (feed.c):
| the in-service bit cleared at once (TOS runs the MFP in software
| end-of-interrupt mode), the processor's level lowered to 5 so that the
| keyboard, the MIDI port and the 200 Hz clock (the MFP's, level 6) are
| answered while the tick works, and a tick that comes while one runs is
| left out. The tick is feed_fn, a C function (d0-d1/a0-a1 are its to
| change; d2/a2 kept as well).

	.text
	.globl	feed_vec
feed_vec:
	bclr	#5,0xfffffa0f.w		| ISRA: Timer A no longer in service
	tst.b	feed_busy
	bne.s	1f
	st	feed_busy
	movem.l	%d0-%d2/%a0-%a2,-(%sp)
	move.w	#0x2500,%sr
	movea.l	feed_fn,%a0
	jsr	(%a0)
	movem.l	(%sp)+,%d0-%d2/%a0-%a2
	sf	feed_busy
1:	rte

	.bss
	.globl	feed_busy, feed_fn
	.even
feed_fn:	.skip	4
feed_busy:	.skip	2
