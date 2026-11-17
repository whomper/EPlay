| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
| pad.S - written by tools/mkpad.py against this commit's build/atari/eplay.map (the profiling build at the shipped layout): do not edit.
| 71 pads, 11876 bytes, each section at its place there, mod the 68030's
| 256-byte caches (the 68030's build alone; the Makefile keeps each one)
	.section ".text.0hot.40dspmp3_tick_status!","ax"
	.globl ep_pad_0
ep_pad_0:
	.space 228
	.section ".text.1st.inflate.inflate_piece!","ax"
	.globl ep_pad_1
ep_pad_1:
	.space 164
	.section ".text.1st.inflate.inflate_read!","ax"
	.globl ep_pad_2
ep_pad_2:
	.space 154
	.section ".text.1st.inflate.z_byte!","ax"
	.globl ep_pad_3
ep_pad_3:
	.space 164
	.section ".text.1st.inflate.z_decode!","ax"
	.globl ep_pad_4
ep_pad_4:
	.space 252
	.section ".text.1st.inflate.z_dynamic!","ax"
	.globl ep_pad_5
ep_pad_5:
	.space 194
	.section ".text.1st.inflate.z_take_slow!","ax"
	.globl ep_pad_6
ep_pad_6:
	.space 166
	.section ".text.1st.list.list_add.part.0!","ax"
	.globl ep_pad_7
ep_pad_7:
	.space 74
	.section ".text.aes!","ax"
	.globl ep_pad_8
ep_pad_8:
	.space 150
	.section ".text.app_main!","ax"
	.globl ep_pad_9
ep_pad_9:
	.space 74
	.section ".text.apply_volume!","ax"
	.globl ep_pad_10
ep_pad_10:
	.space 198
	.section ".text.cmp3_cpu!","ax"
	.globl ep_pad_11
ep_pad_11:
	.space 18
	.section ".text.cmp3_kind!","ax"
	.globl ep_pad_12
ep_pad_12:
	.space 66
	.section ".text.cmp3_load!","ax"
	.globl ep_pad_13
ep_pad_13:
	.space 134
	.section ".text.cpu_is080!","ax"
	.globl ep_pad_14
ep_pad_14:
	.space 148
	.section ".text.crash_vbl_on!","ax"
	.globl ep_pad_15
ep_pad_15:
	.space 150
	.section ".text.dns_query!","ax"
	.globl ep_pad_16
ep_pad_16:
	.space 226
	.section ".text.dspmp3_stream_misses!","ax"
	.globl ep_pad_17
ep_pad_17:
	.space 248
	.section ".text.dspmp3_tick_stream!","ax"
	.globl ep_pad_18
ep_pad_18:
	.space 244
	.section ".text.fb_run!","ax"
	.globl ep_pad_19
ep_pad_19:
	.space 164
	.section ".text.feed_poll!","ax"
	.globl ep_pad_20
ep_pad_20:
	.space 240
	.section ".text.fill!","ax"
	.globl ep_pad_21
ep_pad_21:
	.space 90
	.section ".text.fx_off!","ax"
	.globl ep_pad_22
ep_pad_22:
	.space 54
	.section ".text.fx_poll!","ax"
	.globl ep_pad_23
ep_pad_23:
	.space 216
	.section ".text.fx_t0!","ax"
	.globl ep_pad_24
ep_pad_24:
	.space 12
	.section ".text.fx_term!","ax"
	.globl ep_pad_25
ep_pad_25:
	.space 54
	.section ".text.fx_us!","ax"
	.globl ep_pad_26
ep_pad_26:
	.space 114
	.section ".text.net_open!","ax"
	.globl ep_pad_27
ep_pad_27:
	.space 132
	.section ".text.part_read!","ax"
	.globl ep_pad_28
ep_pad_28:
	.space 176
	.section ".text.play_keep_up!","ax"
	.globl ep_pad_29
ep_pad_29:
	.space 44
	.section ".text.play_now!","ax"
	.globl ep_pad_30
ep_pad_30:
	.space 218
	.section ".text.play_open!","ax"
	.globl ep_pad_31
ep_pad_31:
	.space 234
	.section ".text.play_step!","ax"
	.globl ep_pad_32
ep_pad_32:
	.space 108
	.section ".text.playing_drawn!","ax"
	.globl ep_pad_33
ep_pad_33:
	.space 18
	.section ".text.settings_load!","ax"
	.globl ep_pad_34
ep_pad_34:
	.space 246
	.section ".text.sup_install!","ax"
	.globl ep_pad_35
ep_pad_35:
	.space 220
	.section ".text.sx_open!","ax"
	.globl ep_pad_36
ep_pad_36:
	.space 248
	.section ".text.sx_play!","ax"
	.globl ep_pad_37
ep_pad_37:
	.space 254
	.section ".text.sx_poll!","ax"
	.globl ep_pad_38
ep_pad_38:
	.space 102
	.section ".text.sx_room!","ax"
	.globl ep_pad_39
ep_pad_39:
	.space 238
	.section ".text.sx_update!","ax"
	.globl ep_pad_40
ep_pad_40:
	.space 54
	.section ".text.term_c!","ax"
	.globl ep_pad_41
ep_pad_41:
	.space 246
	.section ".text.turn!","ax"
	.globl ep_pad_42
ep_pad_42:
	.space 162
	.section ".text.vp_cpu!","ax"
	.globl ep_pad_43
ep_pad_43:
	.space 52
	.section ".text.vp_halt.part.0!","ax"
	.globl ep_pad_44
ep_pad_44:
	.space 180
	.section ".text.vp_open!","ax"
	.globl ep_pad_45
ep_pad_45:
	.space 230
	.section ".text.vp_step!","ax"
	.globl ep_pad_46
ep_pad_46:
	.space 186
	.section ".rodata.m3u_add.str1.1!","a"
	.globl ep_pad_47
ep_pad_47:
	.space 176
	.section ".rodata.pages.13!","a"
	.globl ep_pad_48
ep_pad_48:
	.space 11
	.section ".rodata.pls_read.str1.1!","a"
	.globl ep_pad_49
ep_pad_49:
	.space 1
	.section ".data.hz_at.4!","aw"
	.globl ep_pad_50
ep_pad_50:
	.space 252
	.section ".data.kind!","aw"
	.globl ep_pad_51
ep_pad_51:
	.space 252
	.section ".data.margin_least!","aw"
	.globl ep_pad_52
ep_pad_52:
	.space 252
	.section ".data.xs_end_at!","aw"
	.globl ep_pad_53
ep_pad_53:
	.space 68
	.section ".bss.avail!","aw",@nobits
	.globl ep_pad_54
ep_pad_54:
	.space 4
	.section ".bss.b.5!","aw",@nobits
	.globl ep_pad_55
ep_pad_55:
	.space 216
	.section ".bss.b.7!","aw",@nobits
	.globl ep_pad_56
ep_pad_56:
	.space 36
	.section ".bss.cmp_hz!","aw",@nobits
	.globl ep_pad_57
ep_pad_57:
	.space 248
	.section ".bss.cmp_snd!","aw",@nobits
	.globl ep_pad_58
ep_pad_58:
	.space 252
	.section ".bss.cpu_is080!","aw",@nobits
	.globl ep_pad_59
ep_pad_59:
	.space 242
	.section ".bss.crash_slot!","aw",@nobits
	.globl ep_pad_60
ep_pad_60:
	.space 252
	.section ".bss.ctab!","aw",@nobits
	.globl ep_pad_61
ep_pad_61:
	.space 254
	.section ".bss.done.4!","aw",@nobits
	.globl ep_pad_62
ep_pad_62:
	.space 252
	.section ".bss.is080!","aw",@nobits
	.globl ep_pad_63
ep_pad_63:
	.space 252
	.section ".bss.name.10!","aw",@nobits
	.globl ep_pad_64
ep_pad_64:
	.space 192
	.section ".bss.name.11!","aw",@nobits
	.globl ep_pad_65
ep_pad_65:
	.space 64
	.section ".bss.part_us_now!","aw",@nobits
	.globl ep_pad_66
ep_pad_66:
	.space 248
	.section ".bss.play_busy_turn!","aw",@nobits
	.globl ep_pad_67
ep_pad_67:
	.space 252
	.section ".bss.s_m2!","aw",@nobits
	.globl ep_pad_68
ep_pad_68:
	.space 252
	.section ".bss.s_miss!","aw",@nobits
	.globl ep_pad_69
ep_pad_69:
	.space 252
	.section ".bss.was_reset!","aw",@nobits
	.globl ep_pad_70
ep_pad_70:
	.space 252
