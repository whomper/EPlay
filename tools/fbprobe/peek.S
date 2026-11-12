| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
|
| peek.S - fbprobe's guarded read (68000 code; the FireBee runs it in its
| 68000 emulation): fb_peek(addr, size) in supervisor mode (Supexec's
| routine fb_peek_s, its arguments in fb_addr and fb_size) reads a byte
| or a word at addr with the bus error (and the address error) vector set
| to a handler that puts the stack back and says it failed. fb_ok is 1 when
| the read answered, fb_val what it read.

	.text
	.globl	fb_peek_s
fb_peek_s:
	move.l	0x08.w,old_berr
	move.l	0x0c.w,old_aerr
	move.l	#fail,0x08.w
	move.l	#fail,0x0c.w
	move.l	%sp,saved_sp
	clr.w	fb_ok
	movea.l	fb_addr,%a0
	moveq	#0,%d0
	cmp.w	#2,fb_size
	beq.s	1f
	move.b	(%a0),%d0
	bra.s	2f
1:	move.w	(%a0),%d0
2:	move.l	%d0,fb_val
	move.w	#1,fb_ok
back:	move.l	old_berr,0x08.w
	move.l	old_aerr,0x0c.w
	moveq	#0,%d0
	rts
fail:	movea.l	saved_sp,%sp		| (the exception's frame dropped, whatever its format)
	clr.w	fb_ok
	bra.s	back

| FBPROBE 2: fb_tick, an MFP Timer A handler (Xbtimer's) that counts in
| fb_ticks and clears Timer A's in-service bit (ISRA bit 5); fb_hz200 reads
| the 200 Hz clock ($4BA) in supervisor mode (Supexec's) into fb_val
	.globl	fb_tick, fb_hz200
fb_tick:
	addq.l	#1,fb_ticks
	bclr	#5,0xfffffa0f.w
	rte
fb_hz200:
	move.l	0x4ba.w,fb_val
	moveq	#0,%d0
	rts

	.bss
	.even
	.globl	fb_ticks
fb_ticks:	.skip	4
	.globl	fb_addr, fb_size, fb_ok, fb_val
fb_addr:	.skip	4
fb_val:		.skip	4
fb_size:	.skip	2
fb_ok:		.skip	2
saved_sp:	.skip	4
old_berr:	.skip	4
old_aerr:	.skip	4
