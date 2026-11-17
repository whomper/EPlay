| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
| pad.S - written by tools/mkpad.py against 1.42's link map (v1.42, build/atari/eplay.map): do not edit.
| 35 pads, 5996 bytes, each section at its place there, mod the 68030's
| 256-byte caches (the 68030's build alone; the Makefile keeps each one)
	.section ".text.0hot.40dspmp3_tick_status!","ax"
	.globl ep_pad_0
ep_pad_0:
	.space 228
	.section ".text.apply_volume!","ax"
	.globl ep_pad_1
ep_pad_1:
	.space 198
	.section ".text.crash_vbl_on!","ax"
	.globl ep_pad_2
ep_pad_2:
	.space 150
	.section ".text.dspmp3_stream_misses!","ax"
	.globl ep_pad_3
ep_pad_3:
	.space 248
	.section ".text.dspmp3_tick_stream!","ax"
	.globl ep_pad_4
ep_pad_4:
	.space 244
	.section ".text.fb_run!","ax"
	.globl ep_pad_5
ep_pad_5:
	.space 164
	.section ".text.feed_poll!","ax"
	.globl ep_pad_6
ep_pad_6:
	.space 240
	.section ".text.fx_off!","ax"
	.globl ep_pad_7
ep_pad_7:
	.space 54
	.section ".text.fx_poll!","ax"
	.globl ep_pad_8
ep_pad_8:
	.space 216
	.section ".text.fx_t0!","ax"
	.globl ep_pad_9
ep_pad_9:
	.space 12
	.section ".text.fx_term!","ax"
	.globl ep_pad_10
ep_pad_10:
	.space 54
	.section ".text.fx_us!","ax"
	.globl ep_pad_11
ep_pad_11:
	.space 114
	.section ".text.play_keep_up!","ax"
	.globl ep_pad_12
ep_pad_12:
	.space 246
	.section ".text.play_step!","ax"
	.globl ep_pad_13
ep_pad_13:
	.space 106
	.section ".text.settings_load!","ax"
	.globl ep_pad_14
ep_pad_14:
	.space 246
	.section ".text.sup_install!","ax"
	.globl ep_pad_15
ep_pad_15:
	.space 220
	.section ".text.sx_open!","ax"
	.globl ep_pad_16
ep_pad_16:
	.space 12
	.section ".text.sx_play!","ax"
	.globl ep_pad_17
ep_pad_17:
	.space 254
	.section ".text.sx_poll!","ax"
	.globl ep_pad_18
ep_pad_18:
	.space 102
	.section ".text.sx_room!","ax"
	.globl ep_pad_19
ep_pad_19:
	.space 238
	.section ".text.sx_update!","ax"
	.globl ep_pad_20
ep_pad_20:
	.space 54
	.section ".text.term_c!","ax"
	.globl ep_pad_21
ep_pad_21:
	.space 246
	.section ".text.vp_open!","ax"
	.globl ep_pad_22
ep_pad_22:
	.space 248
	.section ".rodata.dsp_mp3_prog_ext!","a"
	.globl ep_pad_23
ep_pad_23:
	.space 176
	.section ".rodata.m3u_add.str1.1!","a"
	.globl ep_pad_24
ep_pad_24:
	.space 154
	.section ".data.ipl_user!","aw"
	.globl ep_pad_25
ep_pad_25:
	.space 254
	.section ".data.savptr_one!","aw"
	.globl ep_pad_26
ep_pad_26:
	.space 252
	.section ".data.xs_end_at!","aw"
	.globl ep_pad_27
ep_pad_27:
	.space 2
	.section ".bss.avail!","aw",@nobits
	.globl ep_pad_28
ep_pad_28:
	.space 4
	.section ".bss.b.5!","aw",@nobits
	.globl ep_pad_29
ep_pad_29:
	.space 216
	.section ".bss.b.7!","aw",@nobits
	.globl ep_pad_30
ep_pad_30:
	.space 36
	.section ".bss.crash_slot!","aw",@nobits
	.globl ep_pad_31
ep_pad_31:
	.space 252
	.section ".bss.done.4!","aw",@nobits
	.globl ep_pad_32
ep_pad_32:
	.space 252
	.section ".bss.s_m2!","aw",@nobits
	.globl ep_pad_33
ep_pad_33:
	.space 252
	.section ".bss.s_miss!","aw",@nobits
	.globl ep_pad_34
ep_pad_34:
	.space 252
