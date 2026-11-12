| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
|
| stack.S - long ovl_on_stack(void *top, long (*f)(void *), void *a): f(a)
| run on the stack whose top is top, the caller's stack put back after.
| MP3FPU.OVL's: minimp3 keeps some 16 KB of scratch on the stack.

	.text
	.globl	ovl_on_stack
ovl_on_stack:
	move.l	4(%sp),%a0		| top
	move.l	8(%sp),%a1		| f
	move.l	12(%sp),%d0		| a
	move.l	%sp,-(%a0)		| the caller's stack, kept on the new one
	movea.l	%a0,%sp
	move.l	%d0,-(%sp)
	jsr	(%a1)			| (d0: its result)
	addq.l	#4,%sp
	movea.l	(%sp),%sp		| back
	rts
