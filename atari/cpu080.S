| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
|
| long cpu_is080(void): 1 when the CPU is an Apollo 68080 (a Vampire's),
| else 0. In supervisor mode (call it through Supexec), and only where the
| _CPU cookie says 40: EmuTOS says 40 for a 68080 (its bios/processor.S),
| and a 68080 is told from a 68040 there as EmuTOS tells it: it has a PCR
| (movec pcr,d1, which a 68040 refuses) and takes addiw.l (#0 to a0, a
| 68080's alone). Either refused (the illegal instruction or line F
| vector, taken at the VBR while this runs, interrupts off) puts the stack
| back and answers 0. For MP3 on the CPU (DESIGN.md, MP3 on the CPU: Erez,
| 16 Nov 2026, "a 68060 or 68080 ... should decode on the CPU").
	.text
	.globl	cpu_is080
cpu_is080:
	move.w	%sr,%d0
	move.l	%a2,-(%sp)
	move.w	%d0,saved_sr
	move.w	#0x2700,%sr
	.dc.l	0x4e7a9801		| movec vbr,a1
	move.l	0x10(%a1),old_ill
	move.l	0x2c(%a1),old_linef
	lea	refused(%pc),%a0
	move.l	%a0,0x10(%a1)
	move.l	%a0,0x2c(%a1)
	move.l	%sp,saved_sp
	moveq	#0,%d0
	.dc.l	0x4e7a1808		| movec pcr,d1
	movea.l	%a1,%a2
	.dc.l	0x06d00000		| addiw.l #0,a0 (a0 not read after)
	moveq	#1,%d0
	bra.s	back
refused:
	movea.l	saved_sp,%sp
	moveq	#0,%d0
	.dc.l	0x4e7a9801		| movec vbr,a1 (again: the fault's code may have used it)
	movea.l	%a1,%a2
back:
	move.l	old_ill,0x10(%a2)
	move.l	old_linef,0x2c(%a2)
	move.w	saved_sr,%sr
	movea.l	(%sp)+,%a2
	rts

	.bss
	.even
saved_sp:	.space	4
old_ill:	.space	4
old_linef:	.space	4
saved_sr:	.space	2
