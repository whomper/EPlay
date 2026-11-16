| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
| pad.S - written by tools/mkpad.py against 1.41's link map (v1.41, build/atari/eplay.map): do not edit.
| 14 pads, 2012 bytes, each section at its place there, mod the 68030's
| 256-byte cache (the 68030's build alone; the Makefile keeps each one)
	.section ".text.fb_run!","ax"
	.globl ep_pad_0
ep_pad_0:
	.space 164
	.section ".text.feed_poll!","ax"
	.globl ep_pad_1
ep_pad_1:
	.space 240
	.section ".text.fx_off!","ax"
	.globl ep_pad_2
ep_pad_2:
	.space 54
	.section ".text.fx_poll!","ax"
	.globl ep_pad_3
ep_pad_3:
	.space 216
	.section ".text.fx_t0!","ax"
	.globl ep_pad_4
ep_pad_4:
	.space 12
	.section ".text.fx_term!","ax"
	.globl ep_pad_5
ep_pad_5:
	.space 54
	.section ".text.fx_us!","ax"
	.globl ep_pad_6
ep_pad_6:
	.space 114
	.section ".text.play_step!","ax"
	.globl ep_pad_7
ep_pad_7:
	.space 250
	.section ".text.sx_open!","ax"
	.globl ep_pad_8
ep_pad_8:
	.space 12
	.section ".text.sx_play!","ax"
	.globl ep_pad_9
ep_pad_9:
	.space 254
	.section ".text.sx_poll!","ax"
	.globl ep_pad_10
ep_pad_10:
	.space 102
	.section ".text.sx_room!","ax"
	.globl ep_pad_11
ep_pad_11:
	.space 238
	.section ".text.sx_update!","ax"
	.globl ep_pad_12
ep_pad_12:
	.space 54
	.section ".text.vp_open!","ax"
	.globl ep_pad_13
ep_pad_13:
	.space 248
