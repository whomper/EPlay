| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
| pad.S - written by tools/mkpad.py against 1.47's link map (v1.47, build/atari/eplay.map): do not edit.
| 191 pads, 31044 bytes, each section at its place there, mod the 68030's
| 256-byte caches (the 68030's build alone; the Makefile keeps each one)
	.section ".text.!","ax"
	.globl ep_pad_0
ep_pad_0:
	.space 192
	.section ".text.0hot.40dspmp3_tick_status!","ax"
	.globl ep_pad_1
ep_pad_1:
	.space 228
	.section ".text.0hot.6Fsncmd_write!","ax"
	.globl ep_pad_2
ep_pad_2:
	.space 170
	.section ".text.0hot.6Jsncmd_amp!","ax"
	.globl ep_pad_3
ep_pad_3:
	.space 120
	.section ".text.1st.dspopl.dspopl_volume!","ax"
	.globl ep_pad_4
ep_pad_4:
	.space 168
	.section ".text.1st.dspopn.dspopn_volume!","ax"
	.globl ep_pad_5
ep_pad_5:
	.space 182
	.section ".text.1st.inflate.inflate_piece!","ax"
	.globl ep_pad_6
ep_pad_6:
	.space 164
	.section ".text.1st.inflate.inflate_read!","ax"
	.globl ep_pad_7
ep_pad_7:
	.space 154
	.section ".text.1st.inflate.z_byte!","ax"
	.globl ep_pad_8
ep_pad_8:
	.space 164
	.section ".text.1st.inflate.z_decode!","ax"
	.globl ep_pad_9
ep_pad_9:
	.space 252
	.section ".text.1st.inflate.z_dynamic!","ax"
	.globl ep_pad_10
ep_pad_10:
	.space 194
	.section ".text.1st.inflate.z_take_slow!","ax"
	.globl ep_pad_11
ep_pad_11:
	.space 166
	.section ".text.1st.list.list_add.part.0!","ax"
	.globl ep_pad_12
ep_pad_12:
	.space 74
	.section ".text.add_host!","ax"
	.globl ep_pad_13
ep_pad_13:
	.space 112
	.section ".text.add_names!","ax"
	.globl ep_pad_14
ep_pad_14:
	.space 230
	.section ".text.aes!","ax"
	.globl ep_pad_15
ep_pad_15:
	.space 224
	.section ".text.app_main!","ax"
	.globl ep_pad_16
ep_pad_16:
	.space 64
	.section ".text.appl_exit!","ax"
	.globl ep_pad_17
ep_pad_17:
	.space 12
	.section ".text.appl_search!","ax"
	.globl ep_pad_18
ep_pad_18:
	.space 190
	.section ".text.apply_volume!","ax"
	.globl ep_pad_19
ep_pad_19:
	.space 140
	.section ".text.av_init!","ax"
	.globl ep_pad_20
ep_pad_20:
	.space 128
	.section ".text.av_window!","ax"
	.globl ep_pad_21
ep_pad_21:
	.space 248
	.section ".text.badge!","ax"
	.globl ep_pad_22
ep_pad_22:
	.space 178
	.section ".text.bar_btn_area!","ax"
	.globl ep_pad_23
ep_pad_23:
	.space 218
	.section ".text.bar_draw!","ax"
	.globl ep_pad_24
ep_pad_24:
	.space 146
	.section ".text.bar_hit!","ax"
	.globl ep_pad_25
ep_pad_25:
	.space 4
	.section ".text.bar_layout!","ax"
	.globl ep_pad_26
ep_pad_26:
	.space 70
	.section ".text.bar_skip_stale!","ax"
	.globl ep_pad_27
ep_pad_27:
	.space 192
	.section ".text.bar_tip!","ax"
	.globl ep_pad_28
ep_pad_28:
	.space 176
	.section ".text.bar_vol_at!","ax"
	.globl ep_pad_29
ep_pad_29:
	.space 252
	.section ".text.box!","ax"
	.globl ep_pad_30
ep_pad_30:
	.space 186
	.section ".text.box_run!","ax"
	.globl ep_pad_31
ep_pad_31:
	.space 190
	.section ".text.button.constprop.0!","ax"
	.globl ep_pad_32
ep_pad_32:
	.space 152
	.section ".text.cf_cookie!","ax"
	.globl ep_pad_33
ep_pad_33:
	.space 42
	.section ".text.cmp3_cpu!","ax"
	.globl ep_pad_34
ep_pad_34:
	.space 18
	.section ".text.cmp3_kind!","ax"
	.globl ep_pad_35
ep_pad_35:
	.space 66
	.section ".text.cmp3_load!","ax"
	.globl ep_pad_36
ep_pad_36:
	.space 134
	.section ".text.command.part.0!","ax"
	.globl ep_pad_37
ep_pad_37:
	.space 176
	.section ".text.conn_log!","ax"
	.globl ep_pad_38
ep_pad_38:
	.space 186
	.section ".text.conn_log_append!","ax"
	.globl ep_pad_39
ep_pad_39:
	.space 230
	.section ".text.conn_log_flush!","ax"
	.globl ep_pad_40
ep_pad_40:
	.space 198
	.section ".text.conn_log_idle!","ax"
	.globl ep_pad_41
ep_pad_41:
	.space 84
	.section ".text.conn_open!","ax"
	.globl ep_pad_42
ep_pad_42:
	.space 156
	.section ".text.cpu_is080!","ax"
	.globl ep_pad_43
ep_pad_43:
	.space 148
	.section ".text.crash_vbl_on!","ax"
	.globl ep_pad_44
ep_pad_44:
	.space 150
	.section ".text.cs_to_atari!","ax"
	.globl ep_pad_45
ep_pad_45:
	.space 172
	.section ".text.dlg_area!","ax"
	.globl ep_pad_46
ep_pad_46:
	.space 4
	.section ".text.dlg_close!","ax"
	.globl ep_pad_47
ep_pad_47:
	.space 214
	.section ".text.dlg_cut!","ax"
	.globl ep_pad_48
ep_pad_48:
	.space 4
	.section ".text.dlg_do!","ax"
	.globl ep_pad_49
ep_pad_49:
	.space 38
	.section ".text.dlg_draw!","ax"
	.globl ep_pad_50
ep_pad_50:
	.space 2
	.section ".text.dlg_draw_range!","ax"
	.globl ep_pad_51
ep_pad_51:
	.space 248
	.section ".text.dlg_evnt_!","ax"
	.globl ep_pad_52
ep_pad_52:
	.space 162
	.section ".text.dlg_find!","ax"
	.globl ep_pad_53
ep_pad_53:
	.space 236
	.section ".text.dlg_in_window!","ax"
	.globl ep_pad_54
ep_pad_54:
	.space 248
	.section ".text.dlg_more!","ax"
	.globl ep_pad_55
ep_pad_55:
	.space 248
	.section ".text.dlg_press_waiting!","ax"
	.globl ep_pad_56
ep_pad_56:
	.space 210
	.section ".text.dns_query!","ax"
	.globl ep_pad_57
ep_pad_57:
	.space 226
	.section ".text.draw_some!","ax"
	.globl ep_pad_58
ep_pad_58:
	.space 2
	.section ".text.draw_vol!","ax"
	.globl ep_pad_59
ep_pad_59:
	.space 72
	.section ".text.dspmp3_start_fail_test!","ax"
	.globl ep_pad_60
ep_pad_60:
	.space 220
	.section ".text.dspmp3_stream_misses!","ax"
	.globl ep_pad_61
ep_pad_61:
	.space 248
	.section ".text.edge_rows!","ax"
	.globl ep_pad_62
ep_pad_62:
	.space 64
	.section ".text.fb_run!","ax"
	.globl ep_pad_63
ep_pad_63:
	.space 164
	.section ".text.feed_poll!","ax"
	.globl ep_pad_64
ep_pad_64:
	.space 240
	.section ".text.fill!","ax"
	.globl ep_pad_65
ep_pad_65:
	.space 90
	.section ".text.find_in!","ax"
	.globl ep_pad_66
ep_pad_66:
	.space 178
	.section ".text.fx_off!","ax"
	.globl ep_pad_67
ep_pad_67:
	.space 54
	.section ".text.fx_poll!","ax"
	.globl ep_pad_68
ep_pad_68:
	.space 216
	.section ".text.fx_t0!","ax"
	.globl ep_pad_69
ep_pad_69:
	.space 12
	.section ".text.fx_term!","ax"
	.globl ep_pad_70
ep_pad_70:
	.space 54
	.section ".text.fx_us!","ax"
	.globl ep_pad_71
ep_pad_71:
	.space 114
	.section ".text.handshake!","ax"
	.globl ep_pad_72
ep_pad_72:
	.space 168
	.section ".text.header!","ax"
	.globl ep_pad_73
ep_pad_73:
	.space 98
	.section ".text.held_msg!","ax"
	.globl ep_pad_74
ep_pad_74:
	.space 244
	.section ".text.hosts_save!","ax"
	.globl ep_pad_75
ep_pad_75:
	.space 62
	.section ".text.icon256_pens.constprop.0!","ax"
	.globl ep_pad_76
ep_pad_76:
	.space 74
	.section ".text.icon_pens!","ax"
	.globl ep_pad_77
ep_pad_77:
	.space 226
	.section ".text.jpeg_on_dsp!","ax"
	.globl ep_pad_78
ep_pad_78:
	.space 160
	.section ".text.lf_forget!","ax"
	.globl ep_pad_79
ep_pad_79:
	.space 236
	.section ".text.lf_init!","ax"
	.globl ep_pad_80
ep_pad_80:
	.space 234
	.section ".text.list_press!","ax"
	.globl ep_pad_81
ep_pad_81:
	.space 14
	.section ".text.load!","ax"
	.globl ep_pad_82
ep_pad_82:
	.space 188
	.section ".text.log_add!","ax"
	.globl ep_pad_83
ep_pad_83:
	.space 24
	.section ".text.log_write!","ax"
	.globl ep_pad_84
ep_pad_84:
	.space 192
	.section ".text.map_see!","ax"
	.globl ep_pad_85
ep_pad_85:
	.space 94
	.section ".text.mask_skip!","ax"
	.globl ep_pad_86
ep_pad_86:
	.space 10
	.section ".text.memchr!","ax"
	.globl ep_pad_87
ep_pad_87:
	.space 178
	.section ".text.message!","ax"
	.globl ep_pad_88
ep_pad_88:
	.space 226
	.section ".text.net_open!","ax"
	.globl ep_pad_89
ep_pad_89:
	.space 132
	.section ".text.next_name!","ax"
	.globl ep_pad_90
ep_pad_90:
	.space 52
	.section ".text.note!","ax"
	.globl ep_pad_91
ep_pad_91:
	.space 212
	.section ".text.open_any!","ax"
	.globl ep_pad_92
ep_pad_92:
	.space 104
	.section ".text.ovl_load!","ax"
	.globl ep_pad_93
ep_pad_93:
	.space 56
	.section ".text.play_chain!","ax"
	.globl ep_pad_94
ep_pad_94:
	.space 222
	.section ".text.play_index!","ax"
	.globl ep_pad_95
ep_pad_95:
	.space 4
	.section ".text.play_keep_up!","ax"
	.globl ep_pad_96
ep_pad_96:
	.space 44
	.section ".text.play_net_low!","ax"
	.globl ep_pad_97
ep_pad_97:
	.space 184
	.section ".text.play_now!","ax"
	.globl ep_pad_98
ep_pad_98:
	.space 234
	.section ".text.play_open!","ax"
	.globl ep_pad_99
ep_pad_99:
	.space 234
	.section ".text.play_step!","ax"
	.globl ep_pad_100
ep_pad_100:
	.space 60
	.section ".text.play_vol_att!","ax"
	.globl ep_pad_101
ep_pad_101:
	.space 218
	.section ".text.playing_drawn!","ax"
	.globl ep_pad_102
ep_pad_102:
	.space 220
	.section ".text.probe!","ax"
	.globl ep_pad_103
ep_pad_103:
	.space 38
	.section ".text.redraw_nav!","ax"
	.globl ep_pad_104
ep_pad_104:
	.space 44
	.section ".text.redraw_under!","ax"
	.globl ep_pad_105
ep_pad_105:
	.space 248
	.section ".text.rows_redraw!","ax"
	.globl ep_pad_106
ep_pad_106:
	.space 250
	.section ".text.save_in!","ax"
	.globl ep_pad_107
ep_pad_107:
	.space 252
	.section ".text.scroll_to!","ax"
	.globl ep_pad_108
ep_pad_108:
	.space 248
	.section ".text.set_radio!","ax"
	.globl ep_pad_109
ep_pad_109:
	.space 28
	.section ".text.settings_dialog!","ax"
	.globl ep_pad_110
ep_pad_110:
	.space 198
	.section ".text.settings_key!","ax"
	.globl ep_pad_111
ep_pad_111:
	.space 116
	.section ".text.settings_load!","ax"
	.globl ep_pad_112
ep_pad_112:
	.space 112
	.section ".text.show_page!","ax"
	.globl ep_pad_113
ep_pad_113:
	.space 178
	.section ".text.skip_by!","ax"
	.globl ep_pad_114
ep_pad_114:
	.space 144
	.section ".text.sncmd_reamp!","ax"
	.globl ep_pad_115
ep_pad_115:
	.space 180
	.section ".text.snd_dac_volume!","ax"
	.globl ep_pad_116
ep_pad_116:
	.space 32
	.section ".text.snd_init!","ax"
	.globl ep_pad_117
ep_pad_117:
	.space 12
	.section ".text.startup.main!","ax"
	.globl ep_pad_118
ep_pad_118:
	.space 230
	.section ".text.step!","ax"
	.globl ep_pad_119
ep_pad_119:
	.space 194
	.section ".text.sup_install!","ax"
	.globl ep_pad_120
ep_pad_120:
	.space 220
	.section ".text.sup_nested!","ax"
	.globl ep_pad_121
ep_pad_121:
	.space 14
	.section ".text.sx_dac_volume!","ax"
	.globl ep_pad_122
ep_pad_122:
	.space 240
	.section ".text.sx_open!","ax"
	.globl ep_pad_123
ep_pad_123:
	.space 248
	.section ".text.sx_play!","ax"
	.globl ep_pad_124
ep_pad_124:
	.space 254
	.section ".text.sx_poll!","ax"
	.globl ep_pad_125
ep_pad_125:
	.space 102
	.section ".text.sx_room!","ax"
	.globl ep_pad_126
ep_pad_126:
	.space 238
	.section ".text.sx_update!","ax"
	.globl ep_pad_127
ep_pad_127:
	.space 54
	.section ".text.term_c!","ax"
	.globl ep_pad_128
ep_pad_128:
	.space 246
	.section ".text.text_plain!","ax"
	.globl ep_pad_129
ep_pad_129:
	.space 194
	.section ".text.track!","ax"
	.globl ep_pad_130
ep_pad_130:
	.space 228
	.section ".text.turn!","ax"
	.globl ep_pad_131
ep_pad_131:
	.space 76
	.section ".text.vp_cpu!","ax"
	.globl ep_pad_132
ep_pad_132:
	.space 52
	.section ".text.vp_halt.part.0!","ax"
	.globl ep_pad_133
ep_pad_133:
	.space 180
	.section ".text.vp_open!","ax"
	.globl ep_pad_134
ep_pad_134:
	.space 230
	.section ".text.vp_step!","ax"
	.globl ep_pad_135
ep_pad_135:
	.space 186
	.section ".text.vp_volume!","ax"
	.globl ep_pad_136
ep_pad_136:
	.space 232
	.section ".rodata.CSWTCH.1318!","a"
	.globl ep_pad_137
ep_pad_137:
	.space 32
	.section ".rodata.CSWTCH.433!","a"
	.globl ep_pad_138
ep_pad_138:
	.space 224
	.section ".rodata.gain_tab!","a"
	.globl ep_pad_139
ep_pad_139:
	.space 240
	.section ".rodata.m3u_add.str1.1!","a"
	.globl ep_pad_140
ep_pad_140:
	.space 218
	.section ".rodata.pages.13!","a"
	.globl ep_pad_141
ep_pad_141:
	.space 11
	.section ".rodata.pcm_probe.str1.1!","a"
	.globl ep_pad_142
ep_pad_142:
	.space 1
	.section ".rodata.pen.0!","a"
	.globl ep_pad_143
ep_pad_143:
	.space 248
	.section ".rodata.pls_read.str1.1!","a"
	.globl ep_pad_144
ep_pad_144:
	.space 2
	.section ".rodata.skip_big!","a"
	.globl ep_pad_145
ep_pad_145:
	.space 72
	.section ".rodata.skip_small!","a"
	.globl ep_pad_146
ep_pad_146:
	.space 8
	.section ".rodata.v16.2!","a"
	.globl ep_pad_147
ep_pad_147:
	.space 240
	.section ".rodata.vol_att!","a"
	.globl ep_pad_148
ep_pad_148:
	.space 232
	.section ".rodata.xb_rates!","a"
	.globl ep_pad_149
ep_pad_149:
	.space 254
	.section ".data.!","aw"
	.globl ep_pad_150
ep_pad_150:
	.space 252
	.section ".data.cpu_kind!","aw"
	.globl ep_pad_151
ep_pad_151:
	.space 252
	.section ".data.kind!","aw"
	.globl ep_pad_152
ep_pad_152:
	.space 252
	.section ".data.log_h!","aw"
	.globl ep_pad_153
ep_pad_153:
	.space 252
	.section ".data.margin_least!","aw"
	.globl ep_pad_154
ep_pad_154:
	.space 252
	.section ".data.net_sendbuf!","aw"
	.globl ep_pad_155
ep_pad_155:
	.space 4
	.section ".data.skip_drawn!","aw"
	.globl ep_pad_156
ep_pad_156:
	.space 252
	.section ".data.xs_end_at!","aw"
	.globl ep_pad_157
ep_pad_157:
	.space 68
	.section ".bss.aes_hosts!","aw",@nobits
	.globl ep_pad_158
ep_pad_158:
	.space 252
	.section ".bss.avail!","aw",@nobits
	.globl ep_pad_159
ep_pad_159:
	.space 224
	.section ".bss.b.8!","aw",@nobits
	.globl ep_pad_160
ep_pad_160:
	.space 32
	.section ".bss.b_btn!","aw",@nobits
	.globl ep_pad_161
ep_pad_161:
	.space 248
	.section ".bss.cmp_hz!","aw",@nobits
	.globl ep_pad_162
ep_pad_162:
	.space 248
	.section ".bss.cmp_snd!","aw",@nobits
	.globl ep_pad_163
ep_pad_163:
	.space 252
	.section ".bss.conn_hosts_path!","aw",@nobits
	.globl ep_pad_164
ep_pad_164:
	.space 56
	.section ".bss.cpu_is080!","aw",@nobits
	.globl ep_pad_165
ep_pad_165:
	.space 242
	.section ".bss.crash_slot!","aw",@nobits
	.globl ep_pad_166
ep_pad_166:
	.space 252
	.section ".bss.ctab!","aw",@nobits
	.globl ep_pad_167
ep_pad_167:
	.space 254
	.section ".bss.dlg_badge!","aw",@nobits
	.globl ep_pad_168
ep_pad_168:
	.space 252
	.section ".bss.done.5!","aw",@nobits
	.globl ep_pad_169
ep_pad_169:
	.space 248
	.section ".bss.dw!","aw",@nobits
	.globl ep_pad_170
ep_pad_170:
	.space 250
	.section ".bss.ecdsa_hosts!","aw",@nobits
	.globl ep_pad_171
ep_pad_171:
	.space 252
	.section ".bss.find_near!","aw",@nobits
	.globl ep_pad_172
ep_pad_172:
	.space 254
	.section ".bss.hosts_read!","aw",@nobits
	.globl ep_pad_173
ep_pad_173:
	.space 252
	.section ".bss.is080!","aw",@nobits
	.globl ep_pad_174
ep_pad_174:
	.space 252
	.section ".bss.k_fwd!","aw",@nobits
	.globl ep_pad_175
ep_pad_175:
	.space 236
	.section ".bss.lf.0!","aw",@nobits
	.globl ep_pad_176
ep_pad_176:
	.space 248
	.section ".bss.log_h_for!","aw",@nobits
	.globl ep_pad_177
ep_pad_177:
	.space 244
	.section ".bss.map_rgb!","aw",@nobits
	.globl ep_pad_178
ep_pad_178:
	.space 250
	.section ".bss.named!","aw",@nobits
	.globl ep_pad_179
ep_pad_179:
	.space 252
	.section ".bss.p.3!","aw",@nobits
	.globl ep_pad_180
ep_pad_180:
	.space 56
	.section ".bss.pcmbuf!","aw",@nobits
	.globl ep_pad_181
ep_pad_181:
	.space 2
	.section ".bss.pend!","aw",@nobits
	.globl ep_pad_182
ep_pad_182:
	.space 246
	.section ".bss.play_busy_turn!","aw",@nobits
	.globl ep_pad_183
ep_pad_183:
	.space 252
	.section ".bss.pool_used!","aw",@nobits
	.globl ep_pad_184
ep_pad_184:
	.space 2
	.section ".bss.s_m2!","aw",@nobits
	.globl ep_pad_185
ep_pad_185:
	.space 252
	.section ".bss.s_miss!","aw",@nobits
	.globl ep_pad_186
ep_pad_186:
	.space 252
	.section ".bss.settings_changed!","aw",@nobits
	.globl ep_pad_187
ep_pad_187:
	.space 252
	.section ".bss.sl_tree!","aw",@nobits
	.globl ep_pad_188
ep_pad_188:
	.space 240
	.section ".bss.std_pens!","aw",@nobits
	.globl ep_pad_189
ep_pad_189:
	.space 252
	.section ".bss.was_reset!","aw",@nobits
	.globl ep_pad_190
ep_pad_190:
	.space 252
