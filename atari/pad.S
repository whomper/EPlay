| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
| pad.S - written by tools/mkpad.py against 1.42's link map (v1.42, build/atari/eplay.map), its constants and data at 1.43's (v1.43): do not edit.
| 126 pads, 20844 bytes, each section at its place there, mod the 68030's
| 256-byte caches (the 68030's build alone; the Makefile keeps each one)
	.section ".text.!","ax"
	.globl ep_pad_0
ep_pad_0:
	.space 192
	.section ".text.0hot.40dspmp3_tick_status!","ax"
	.globl ep_pad_1
ep_pad_1:
	.space 228
	.section ".text.1st.inflate.inflate_piece!","ax"
	.globl ep_pad_2
ep_pad_2:
	.space 164
	.section ".text.1st.inflate.inflate_read!","ax"
	.globl ep_pad_3
ep_pad_3:
	.space 154
	.section ".text.1st.inflate.z_byte!","ax"
	.globl ep_pad_4
ep_pad_4:
	.space 164
	.section ".text.1st.inflate.z_decode!","ax"
	.globl ep_pad_5
ep_pad_5:
	.space 252
	.section ".text.1st.inflate.z_dynamic!","ax"
	.globl ep_pad_6
ep_pad_6:
	.space 194
	.section ".text.1st.inflate.z_take_slow!","ax"
	.globl ep_pad_7
ep_pad_7:
	.space 166
	.section ".text.1st.list.list_add.part.0!","ax"
	.globl ep_pad_8
ep_pad_8:
	.space 74
	.section ".text.add_host!","ax"
	.globl ep_pad_9
ep_pad_9:
	.space 112
	.section ".text.aes!","ax"
	.globl ep_pad_10
ep_pad_10:
	.space 224
	.section ".text.app_main!","ax"
	.globl ep_pad_11
ep_pad_11:
	.space 74
	.section ".text.appl_exit!","ax"
	.globl ep_pad_12
ep_pad_12:
	.space 12
	.section ".text.appl_search!","ax"
	.globl ep_pad_13
ep_pad_13:
	.space 190
	.section ".text.apply_volume!","ax"
	.globl ep_pad_14
ep_pad_14:
	.space 198
	.section ".text.av_init!","ax"
	.globl ep_pad_15
ep_pad_15:
	.space 128
	.section ".text.av_window!","ax"
	.globl ep_pad_16
ep_pad_16:
	.space 248
	.section ".text.box!","ax"
	.globl ep_pad_17
ep_pad_17:
	.space 186
	.section ".text.box_run!","ax"
	.globl ep_pad_18
ep_pad_18:
	.space 190
	.section ".text.button.constprop.0!","ax"
	.globl ep_pad_19
ep_pad_19:
	.space 152
	.section ".text.cf_cookie!","ax"
	.globl ep_pad_20
ep_pad_20:
	.space 42
	.section ".text.cmp3_cpu!","ax"
	.globl ep_pad_21
ep_pad_21:
	.space 18
	.section ".text.cmp3_kind!","ax"
	.globl ep_pad_22
ep_pad_22:
	.space 66
	.section ".text.cmp3_load!","ax"
	.globl ep_pad_23
ep_pad_23:
	.space 134
	.section ".text.conn_log!","ax"
	.globl ep_pad_24
ep_pad_24:
	.space 186
	.section ".text.conn_log_append!","ax"
	.globl ep_pad_25
ep_pad_25:
	.space 230
	.section ".text.conn_log_flush!","ax"
	.globl ep_pad_26
ep_pad_26:
	.space 198
	.section ".text.conn_log_idle!","ax"
	.globl ep_pad_27
ep_pad_27:
	.space 84
	.section ".text.conn_open!","ax"
	.globl ep_pad_28
ep_pad_28:
	.space 156
	.section ".text.cpu_is080!","ax"
	.globl ep_pad_29
ep_pad_29:
	.space 148
	.section ".text.crash_vbl_on!","ax"
	.globl ep_pad_30
ep_pad_30:
	.space 150
	.section ".text.cs_to_atari!","ax"
	.globl ep_pad_31
ep_pad_31:
	.space 172
	.section ".text.dlg_do!","ax"
	.globl ep_pad_32
ep_pad_32:
	.space 218
	.section ".text.dns_query!","ax"
	.globl ep_pad_33
ep_pad_33:
	.space 226
	.section ".text.dspmp3_stream_misses!","ax"
	.globl ep_pad_34
ep_pad_34:
	.space 248
	.section ".text.edge_rows!","ax"
	.globl ep_pad_35
ep_pad_35:
	.space 64
	.section ".text.fb_run!","ax"
	.globl ep_pad_36
ep_pad_36:
	.space 164
	.section ".text.feed_poll!","ax"
	.globl ep_pad_37
ep_pad_37:
	.space 240
	.section ".text.fill!","ax"
	.globl ep_pad_38
ep_pad_38:
	.space 90
	.section ".text.fx_off!","ax"
	.globl ep_pad_39
ep_pad_39:
	.space 54
	.section ".text.fx_poll!","ax"
	.globl ep_pad_40
ep_pad_40:
	.space 216
	.section ".text.fx_t0!","ax"
	.globl ep_pad_41
ep_pad_41:
	.space 12
	.section ".text.fx_term!","ax"
	.globl ep_pad_42
ep_pad_42:
	.space 54
	.section ".text.fx_us!","ax"
	.globl ep_pad_43
ep_pad_43:
	.space 114
	.section ".text.handshake!","ax"
	.globl ep_pad_44
ep_pad_44:
	.space 168
	.section ".text.header!","ax"
	.globl ep_pad_45
ep_pad_45:
	.space 98
	.section ".text.hosts_save!","ax"
	.globl ep_pad_46
ep_pad_46:
	.space 62
	.section ".text.icon256_pens.constprop.0!","ax"
	.globl ep_pad_47
ep_pad_47:
	.space 74
	.section ".text.icon_pens!","ax"
	.globl ep_pad_48
ep_pad_48:
	.space 226
	.section ".text.jpeg_on_dsp!","ax"
	.globl ep_pad_49
ep_pad_49:
	.space 160
	.section ".text.lf_forget!","ax"
	.globl ep_pad_50
ep_pad_50:
	.space 236
	.section ".text.lf_init!","ax"
	.globl ep_pad_51
ep_pad_51:
	.space 234
	.section ".text.load!","ax"
	.globl ep_pad_52
ep_pad_52:
	.space 184
	.section ".text.log_add!","ax"
	.globl ep_pad_53
ep_pad_53:
	.space 24
	.section ".text.log_write!","ax"
	.globl ep_pad_54
ep_pad_54:
	.space 192
	.section ".text.map_see!","ax"
	.globl ep_pad_55
ep_pad_55:
	.space 94
	.section ".text.memchr!","ax"
	.globl ep_pad_56
ep_pad_56:
	.space 178
	.section ".text.net_open!","ax"
	.globl ep_pad_57
ep_pad_57:
	.space 132
	.section ".text.next_name!","ax"
	.globl ep_pad_58
ep_pad_58:
	.space 52
	.section ".text.ovl_load!","ax"
	.globl ep_pad_59
ep_pad_59:
	.space 56
	.section ".text.play_keep_up!","ax"
	.globl ep_pad_60
ep_pad_60:
	.space 44
	.section ".text.play_net_low!","ax"
	.globl ep_pad_61
ep_pad_61:
	.space 184
	.section ".text.play_open!","ax"
	.globl ep_pad_62
ep_pad_62:
	.space 234
	.section ".text.play_step!","ax"
	.globl ep_pad_63
ep_pad_63:
	.space 108
	.section ".text.probe!","ax"
	.globl ep_pad_64
ep_pad_64:
	.space 38
	.section ".text.set_radio!","ax"
	.globl ep_pad_65
ep_pad_65:
	.space 28
	.section ".text.settings_load!","ax"
	.globl ep_pad_66
ep_pad_66:
	.space 246
	.section ".text.startup.main!","ax"
	.globl ep_pad_67
ep_pad_67:
	.space 230
	.section ".text.step!","ax"
	.globl ep_pad_68
ep_pad_68:
	.space 242
	.section ".text.sup_install!","ax"
	.globl ep_pad_69
ep_pad_69:
	.space 220
	.section ".text.sup_nested!","ax"
	.globl ep_pad_70
ep_pad_70:
	.space 14
	.section ".text.sx_open!","ax"
	.globl ep_pad_71
ep_pad_71:
	.space 248
	.section ".text.sx_play!","ax"
	.globl ep_pad_72
ep_pad_72:
	.space 254
	.section ".text.sx_poll!","ax"
	.globl ep_pad_73
ep_pad_73:
	.space 102
	.section ".text.sx_room!","ax"
	.globl ep_pad_74
ep_pad_74:
	.space 238
	.section ".text.sx_update!","ax"
	.globl ep_pad_75
ep_pad_75:
	.space 54
	.section ".text.term_c!","ax"
	.globl ep_pad_76
ep_pad_76:
	.space 246
	.section ".text.text_font!","ax"
	.globl ep_pad_77
ep_pad_77:
	.space 146
	.section ".text.text_plain!","ax"
	.globl ep_pad_78
ep_pad_78:
	.space 194
	.section ".text.turn!","ax"
	.globl ep_pad_79
ep_pad_79:
	.space 120
	.section ".text.vp_cpu!","ax"
	.globl ep_pad_80
ep_pad_80:
	.space 52
	.section ".text.vp_halt.part.0!","ax"
	.globl ep_pad_81
ep_pad_81:
	.space 180
	.section ".text.vp_open!","ax"
	.globl ep_pad_82
ep_pad_82:
	.space 230
	.section ".text.vp_step!","ax"
	.globl ep_pad_83
ep_pad_83:
	.space 186
	.section ".rodata.CSWTCH.1307!","a"
	.globl ep_pad_84
ep_pad_84:
	.space 32
	.section ".rodata.CSWTCH.433!","a"
	.globl ep_pad_85
ep_pad_85:
	.space 224
	.section ".rodata.m3u_add.str1.1!","a"
	.globl ep_pad_86
ep_pad_86:
	.space 18
	.section ".rodata.pages.13!","a"
	.globl ep_pad_87
ep_pad_87:
	.space 11
	.section ".rodata.pen.0!","a"
	.globl ep_pad_88
ep_pad_88:
	.space 248
	.section ".rodata.pls_read.str1.1!","a"
	.globl ep_pad_89
ep_pad_89:
	.space 1
	.section ".rodata.v16.2!","a"
	.globl ep_pad_90
ep_pad_90:
	.space 240
	.section ".data.!","aw"
	.globl ep_pad_91
ep_pad_91:
	.space 252
	.section ".data.cpu_kind!","aw"
	.globl ep_pad_92
ep_pad_92:
	.space 252
	.section ".data.kind!","aw"
	.globl ep_pad_93
ep_pad_93:
	.space 252
	.section ".data.last_id.8!","aw"
	.globl ep_pad_94
ep_pad_94:
	.space 254
	.section ".data.log_h!","aw"
	.globl ep_pad_95
ep_pad_95:
	.space 252
	.section ".data.margin_least!","aw"
	.globl ep_pad_96
ep_pad_96:
	.space 252
	.section ".data.net_sendbuf!","aw"
	.globl ep_pad_97
ep_pad_97:
	.space 4
	.section ".data.xs_end_at!","aw"
	.globl ep_pad_98
ep_pad_98:
	.space 66
	.section ".bss.aes_hosts!","aw",@nobits
	.globl ep_pad_99
ep_pad_99:
	.space 252
	.section ".bss.avail!","aw",@nobits
	.globl ep_pad_100
ep_pad_100:
	.space 4
	.section ".bss.b.5!","aw",@nobits
	.globl ep_pad_101
ep_pad_101:
	.space 216
	.section ".bss.b.7!","aw",@nobits
	.globl ep_pad_102
ep_pad_102:
	.space 36
	.section ".bss.cmp_hz!","aw",@nobits
	.globl ep_pad_103
ep_pad_103:
	.space 248
	.section ".bss.cmp_snd!","aw",@nobits
	.globl ep_pad_104
ep_pad_104:
	.space 252
	.section ".bss.conn_hosts_path!","aw",@nobits
	.globl ep_pad_105
ep_pad_105:
	.space 56
	.section ".bss.cpu_is080!","aw",@nobits
	.globl ep_pad_106
ep_pad_106:
	.space 242
	.section ".bss.crash_slot!","aw",@nobits
	.globl ep_pad_107
ep_pad_107:
	.space 252
	.section ".bss.ctab!","aw",@nobits
	.globl ep_pad_108
ep_pad_108:
	.space 254
	.section ".bss.done.4!","aw",@nobits
	.globl ep_pad_109
ep_pad_109:
	.space 252
	.section ".bss.ecdsa_hosts!","aw",@nobits
	.globl ep_pad_110
ep_pad_110:
	.space 252
	.section ".bss.hosts_read!","aw",@nobits
	.globl ep_pad_111
ep_pad_111:
	.space 252
	.section ".bss.is080!","aw",@nobits
	.globl ep_pad_112
ep_pad_112:
	.space 252
	.section ".bss.last_h.6!","aw",@nobits
	.globl ep_pad_113
ep_pad_113:
	.space 254
	.section ".bss.last_w.7!","aw",@nobits
	.globl ep_pad_114
ep_pad_114:
	.space 254
	.section ".bss.lf.0!","aw",@nobits
	.globl ep_pad_115
ep_pad_115:
	.space 248
	.section ".bss.log_h_for!","aw",@nobits
	.globl ep_pad_116
ep_pad_116:
	.space 244
	.section ".bss.map_rgb!","aw",@nobits
	.globl ep_pad_117
ep_pad_117:
	.space 250
	.section ".bss.named!","aw",@nobits
	.globl ep_pad_118
ep_pad_118:
	.space 252
	.section ".bss.p.3!","aw",@nobits
	.globl ep_pad_119
ep_pad_119:
	.space 56
	.section ".bss.play_busy_turn!","aw",@nobits
	.globl ep_pad_120
ep_pad_120:
	.space 252
	.section ".bss.pool_used!","aw",@nobits
	.globl ep_pad_121
ep_pad_121:
	.space 254
	.section ".bss.s_m2!","aw",@nobits
	.globl ep_pad_122
ep_pad_122:
	.space 252
	.section ".bss.s_miss!","aw",@nobits
	.globl ep_pad_123
ep_pad_123:
	.space 252
	.section ".bss.std_pens!","aw",@nobits
	.globl ep_pad_124
ep_pad_124:
	.space 252
	.section ".bss.was_reset!","aw",@nobits
	.globl ep_pad_125
ep_pad_125:
	.space 252
