| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
| pad.S - written by tools/mkpad.py against 1.58's build/atari/eplay.map: do not edit.
| 334 pads, 55880 bytes, each section at its place there, mod the 68030's
| 256-byte caches (the 68030's build alone; the Makefile keeps each one)
	.section ".text.!","ax"
	.globl ep_pad_0
ep_pad_0:
	.space 192
	.section ".text.0hot.40dspmp3_tick_status!","ax"
	.globl ep_pad_1
ep_pad_1:
	.space 206
	.section ".text.0hot.6Fsncmd_write!","ax"
	.globl ep_pad_2
ep_pad_2:
	.space 170
	.section ".text.0hot.6Jsncmd_amp!","ax"
	.globl ep_pad_3
ep_pad_3:
	.space 120
	.section ".text.0hot.70mp_tick!","ax"
	.globl ep_pad_4
ep_pad_4:
	.space 106
	.section ".text.1st.dspopl.dspopl_volume!","ax"
	.globl ep_pad_5
ep_pad_5:
	.space 168
	.section ".text.1st.dspopn.dspopn_volume!","ax"
	.globl ep_pad_6
ep_pad_6:
	.space 182
	.section ".text.1st.inflate.inflate_piece!","ax"
	.globl ep_pad_7
ep_pad_7:
	.space 164
	.section ".text.1st.inflate.inflate_read!","ax"
	.globl ep_pad_8
ep_pad_8:
	.space 154
	.section ".text.1st.inflate.z_byte!","ax"
	.globl ep_pad_9
ep_pad_9:
	.space 164
	.section ".text.1st.inflate.z_decode!","ax"
	.globl ep_pad_10
ep_pad_10:
	.space 252
	.section ".text.1st.inflate.z_dynamic!","ax"
	.globl ep_pad_11
ep_pad_11:
	.space 194
	.section ".text.1st.inflate.z_take_slow!","ax"
	.globl ep_pad_12
ep_pad_12:
	.space 166
	.section ".text.1st.list.list_add.part.0!","ax"
	.globl ep_pad_13
ep_pad_13:
	.space 74
	.section ".text.1st.list.list_step!","ax"
	.globl ep_pad_14
ep_pad_14:
	.space 100
	.section ".text.add_host!","ax"
	.globl ep_pad_15
ep_pad_15:
	.space 112
	.section ".text.add_names!","ax"
	.globl ep_pad_16
ep_pad_16:
	.space 230
	.section ".text.aes!","ax"
	.globl ep_pad_17
ep_pad_17:
	.space 224
	.section ".text.all_off!","ax"
	.globl ep_pad_18
ep_pad_18:
	.space 240
	.section ".text.app_main!","ax"
	.globl ep_pad_19
ep_pad_19:
	.space 64
	.section ".text.appl_exit!","ax"
	.globl ep_pad_20
ep_pad_20:
	.space 12
	.section ".text.appl_search!","ax"
	.globl ep_pad_21
ep_pad_21:
	.space 190
	.section ".text.apply_volume!","ax"
	.globl ep_pad_22
ep_pad_22:
	.space 84
	.section ".text.art_step!","ax"
	.globl ep_pad_23
ep_pad_23:
	.space 184
	.section ".text.atoi!","ax"
	.globl ep_pad_24
ep_pad_24:
	.space 198
	.section ".text.atol!","ax"
	.globl ep_pad_25
ep_pad_25:
	.space 198
	.section ".text.av_init!","ax"
	.globl ep_pad_26
ep_pad_26:
	.space 204
	.section ".text.av_message!","ax"
	.globl ep_pad_27
ep_pad_27:
	.space 194
	.section ".text.av_window!","ax"
	.globl ep_pad_28
ep_pad_28:
	.space 248
	.section ".text.badge!","ax"
	.globl ep_pad_29
ep_pad_29:
	.space 178
	.section ".text.badge_draw!","ax"
	.globl ep_pad_30
ep_pad_30:
	.space 202
	.section ".text.bar_btn_area!","ax"
	.globl ep_pad_31
ep_pad_31:
	.space 218
	.section ".text.bar_colours!","ax"
	.globl ep_pad_32
ep_pad_32:
	.space 42
	.section ".text.bar_draw!","ax"
	.globl ep_pad_33
ep_pad_33:
	.space 146
	.section ".text.bar_hit!","ax"
	.globl ep_pad_34
ep_pad_34:
	.space 4
	.section ".text.bar_layout!","ax"
	.globl ep_pad_35
ep_pad_35:
	.space 70
	.section ".text.bar_skip_stale!","ax"
	.globl ep_pad_36
ep_pad_36:
	.space 192
	.section ".text.bar_tip!","ax"
	.globl ep_pad_37
ep_pad_37:
	.space 176
	.section ".text.bar_vol_at!","ax"
	.globl ep_pad_38
ep_pad_38:
	.space 252
	.section ".text.box!","ax"
	.globl ep_pad_39
ep_pad_39:
	.space 186
	.section ".text.box_run!","ax"
	.globl ep_pad_40
ep_pad_40:
	.space 114
	.section ".text.button!","ax"
	.globl ep_pad_41
ep_pad_41:
	.space 172
	.section ".text.button.constprop.0!","ax"
	.globl ep_pad_42
ep_pad_42:
	.space 152
	.section ".text.cf_cookie!","ax"
	.globl ep_pad_43
ep_pad_43:
	.space 42
	.section ".text.cmp3_cpu!","ax"
	.globl ep_pad_44
ep_pad_44:
	.space 18
	.section ".text.cmp3_kind!","ax"
	.globl ep_pad_45
ep_pad_45:
	.space 66
	.section ".text.cmp3_load!","ax"
	.globl ep_pad_46
ep_pad_46:
	.space 132
	.section ".text.command.part.0!","ax"
	.globl ep_pad_47
ep_pad_47:
	.space 118
	.section ".text.conn_log!","ax"
	.globl ep_pad_48
ep_pad_48:
	.space 186
	.section ".text.conn_log_append!","ax"
	.globl ep_pad_49
ep_pad_49:
	.space 230
	.section ".text.conn_log_flush!","ax"
	.globl ep_pad_50
ep_pad_50:
	.space 198
	.section ".text.conn_log_idle!","ax"
	.globl ep_pad_51
ep_pad_51:
	.space 84
	.section ".text.conn_open!","ax"
	.globl ep_pad_52
ep_pad_52:
	.space 156
	.section ".text.cpu_is080!","ax"
	.globl ep_pad_53
ep_pad_53:
	.space 148
	.section ".text.crash_vbl_on!","ax"
	.globl ep_pad_54
ep_pad_54:
	.space 150
	.section ".text.cut!","ax"
	.globl ep_pad_55
ep_pad_55:
	.space 96
	.section ".text.dither_init!","ax"
	.globl ep_pad_56
ep_pad_56:
	.space 100
	.section ".text.dlg_area!","ax"
	.globl ep_pad_57
ep_pad_57:
	.space 4
	.section ".text.dlg_close!","ax"
	.globl ep_pad_58
ep_pad_58:
	.space 214
	.section ".text.dlg_cut!","ax"
	.globl ep_pad_59
ep_pad_59:
	.space 4
	.section ".text.dlg_do!","ax"
	.globl ep_pad_60
ep_pad_60:
	.space 38
	.section ".text.dlg_draw!","ax"
	.globl ep_pad_61
ep_pad_61:
	.space 2
	.section ".text.dlg_draw_range!","ax"
	.globl ep_pad_62
ep_pad_62:
	.space 248
	.section ".text.dlg_evnt_!","ax"
	.globl ep_pad_63
ep_pad_63:
	.space 154
	.section ".text.dlg_find!","ax"
	.globl ep_pad_64
ep_pad_64:
	.space 236
	.section ".text.dlg_in_window!","ax"
	.globl ep_pad_65
ep_pad_65:
	.space 248
	.section ".text.dlg_more!","ax"
	.globl ep_pad_66
ep_pad_66:
	.space 248
	.section ".text.dlg_press_waiting!","ax"
	.globl ep_pad_67
ep_pad_67:
	.space 210
	.section ".text.dns_query!","ax"
	.globl ep_pad_68
ep_pad_68:
	.space 226
	.section ".text.draw_changed!","ax"
	.globl ep_pad_69
ep_pad_69:
	.space 20
	.section ".text.draw_obj!","ax"
	.globl ep_pad_70
ep_pad_70:
	.space 30
	.section ".text.draw_some!","ax"
	.globl ep_pad_71
ep_pad_71:
	.space 2
	.section ".text.draw_vol!","ax"
	.globl ep_pad_72
ep_pad_72:
	.space 72
	.section ".text.dspmp3_stall_test!","ax"
	.globl ep_pad_73
ep_pad_73:
	.space 2
	.section ".text.dspmp3_stream_aligns!","ax"
	.globl ep_pad_74
ep_pad_74:
	.space 212
	.section ".text.dspmp3_stream_misses!","ax"
	.globl ep_pad_75
ep_pad_75:
	.space 248
	.section ".text.dspmp3_stream_stalled!","ax"
	.globl ep_pad_76
ep_pad_76:
	.space 4
	.section ".text.dspmp3_tick_stream!","ax"
	.globl ep_pad_77
ep_pad_77:
	.space 250
	.section ".text.edge_rows!","ax"
	.globl ep_pad_78
ep_pad_78:
	.space 64
	.section ".text.fb_run!","ax"
	.globl ep_pad_79
ep_pad_79:
	.space 164
	.section ".text.feed_poll!","ax"
	.globl ep_pad_80
ep_pad_80:
	.space 240
	.section ".text.field_from_row!","ax"
	.globl ep_pad_81
ep_pad_81:
	.space 10
	.section ".text.filepick_save!","ax"
	.globl ep_pad_82
ep_pad_82:
	.space 48
	.section ".text.fill!","ax"
	.globl ep_pad_83
ep_pad_83:
	.space 44
	.section ".text.find_in!","ax"
	.globl ep_pad_84
ep_pad_84:
	.space 178
	.section ".text.find_server!","ax"
	.globl ep_pad_85
ep_pad_85:
	.space 174
	.section ".text.flush_super!","ax"
	.globl ep_pad_86
ep_pad_86:
	.space 250
	.section ".text.folder_shown!","ax"
	.globl ep_pad_87
ep_pad_87:
	.space 124
	.section ".text.fp_go!","ax"
	.globl ep_pad_88
ep_pad_88:
	.space 214
	.section ".text.fsel_exinput!","ax"
	.globl ep_pad_89
ep_pad_89:
	.space 140
	.section ".text.fx_off!","ax"
	.globl ep_pad_90
ep_pad_90:
	.space 54
	.section ".text.fx_poll!","ax"
	.globl ep_pad_91
ep_pad_91:
	.space 216
	.section ".text.fx_t0!","ax"
	.globl ep_pad_92
ep_pad_92:
	.space 12
	.section ".text.fx_term!","ax"
	.globl ep_pad_93
ep_pad_93:
	.space 54
	.section ".text.fx_us!","ax"
	.globl ep_pad_94
ep_pad_94:
	.space 114
	.section ".text.give!","ax"
	.globl ep_pad_95
ep_pad_95:
	.space 160
	.section ".text.handshake!","ax"
	.globl ep_pad_96
ep_pad_96:
	.space 168
	.section ".text.header!","ax"
	.globl ep_pad_97
ep_pad_97:
	.space 98
	.section ".text.held_msg!","ax"
	.globl ep_pad_98
ep_pad_98:
	.space 244
	.section ".text.hosts_save!","ax"
	.globl ep_pad_99
ep_pad_99:
	.space 62
	.section ".text.icon256_pens.constprop.0!","ax"
	.globl ep_pad_100
ep_pad_100:
	.space 74
	.section ".text.icon_pens!","ax"
	.globl ep_pad_101
ep_pad_101:
	.space 226
	.section ".text.info!","ax"
	.globl ep_pad_102
ep_pad_102:
	.space 228
	.section ".text.jpeg_on_dsp!","ax"
	.globl ep_pad_103
ep_pad_103:
	.space 160
	.section ".text.lf_forget!","ax"
	.globl ep_pad_104
ep_pad_104:
	.space 236
	.section ".text.lf_init!","ax"
	.globl ep_pad_105
ep_pad_105:
	.space 234
	.section ".text.list_press!","ax"
	.globl ep_pad_106
ep_pad_106:
	.space 14
	.section ".text.load!","ax"
	.globl ep_pad_107
ep_pad_107:
	.space 136
	.section ".text.log_add!","ax"
	.globl ep_pad_108
ep_pad_108:
	.space 24
	.section ".text.log_write!","ax"
	.globl ep_pad_109
ep_pad_109:
	.space 192
	.section ".text.make!","ax"
	.globl ep_pad_110
ep_pad_110:
	.space 82
	.section ".text.map_see!","ax"
	.globl ep_pad_111
ep_pad_111:
	.space 94
	.section ".text.mask_changed!","ax"
	.globl ep_pad_112
ep_pad_112:
	.space 20
	.section ".text.mask_skip!","ax"
	.globl ep_pad_113
ep_pad_113:
	.space 10
	.section ".text.memchr!","ax"
	.globl ep_pad_114
ep_pad_114:
	.space 178
	.section ".text.message!","ax"
	.globl ep_pad_115
ep_pad_115:
	.space 226
	.section ".text.mint_here!","ax"
	.globl ep_pad_116
ep_pad_116:
	.space 210
	.section ".text.mp_volume!","ax"
	.globl ep_pad_117
ep_pad_117:
	.space 142
	.section ".text.name_at!","ax"
	.globl ep_pad_118
ep_pad_118:
	.space 8
	.section ".text.nearest256!","ax"
	.globl ep_pad_119
ep_pad_119:
	.space 86
	.section ".text.net_open!","ax"
	.globl ep_pad_120
ep_pad_120:
	.space 132
	.section ".text.next_name!","ax"
	.globl ep_pad_121
ep_pad_121:
	.space 52
	.section ".text.note!","ax"
	.globl ep_pad_122
ep_pad_122:
	.space 212
	.section ".text.obj!","ax"
	.globl ep_pad_123
ep_pad_123:
	.space 124
	.section ".text.off.part.0!","ax"
	.globl ep_pad_124
ep_pad_124:
	.space 132
	.section ".text.off_begin!","ax"
	.globl ep_pad_125
ep_pad_125:
	.space 160
	.section ".text.open_file!","ax"
	.globl ep_pad_126
ep_pad_126:
	.space 76
	.section ".text.open_row!","ax"
	.globl ep_pad_127
ep_pad_127:
	.space 36
	.section ".text.open_window!","ax"
	.globl ep_pad_128
ep_pad_128:
	.space 80
	.section ".text.ovl_free!","ax"
	.globl ep_pad_129
ep_pad_129:
	.space 234
	.section ".text.ovl_load_aligned!","ax"
	.globl ep_pad_130
ep_pad_130:
	.space 10
	.section ".text.pic_begin.part.0!","ax"
	.globl ep_pad_131
ep_pad_131:
	.space 208
	.section ".text.pic_dither_row!","ax"
	.globl ep_pad_132
ep_pad_132:
	.space 174
	.section ".text.pic_pal_soft!","ax"
	.globl ep_pad_133
ep_pad_133:
	.space 226
	.section ".text.play_chain!","ax"
	.globl ep_pad_134
ep_pad_134:
	.space 222
	.section ".text.play_index!","ax"
	.globl ep_pad_135
ep_pad_135:
	.space 4
	.section ".text.play_keep_up!","ax"
	.globl ep_pad_136
ep_pad_136:
	.space 78
	.section ".text.play_log_stop.part.0!","ax"
	.globl ep_pad_137
ep_pad_137:
	.space 154
	.section ".text.play_ms!","ax"
	.globl ep_pad_138
ep_pad_138:
	.space 232
	.section ".text.play_net_low!","ax"
	.globl ep_pad_139
ep_pad_139:
	.space 184
	.section ".text.play_now!","ax"
	.globl ep_pad_140
ep_pad_140:
	.space 234
	.section ".text.play_open!","ax"
	.globl ep_pad_141
ep_pad_141:
	.space 234
	.section ".text.play_pause!","ax"
	.globl ep_pad_142
ep_pad_142:
	.space 244
	.section ".text.play_start!","ax"
	.globl ep_pad_143
ep_pad_143:
	.space 214
	.section ".text.play_step!","ax"
	.globl ep_pad_144
ep_pad_144:
	.space 148
	.section ".text.play_stop!","ax"
	.globl ep_pad_145
ep_pad_145:
	.space 222
	.section ".text.play_stream_note!","ax"
	.globl ep_pad_146
ep_pad_146:
	.space 188
	.section ".text.play_vol_att!","ax"
	.globl ep_pad_147
ep_pad_147:
	.space 218
	.section ".text.play_wait_ms!","ax"
	.globl ep_pad_148
ep_pad_148:
	.space 232
	.section ".text.playing_drawn!","ax"
	.globl ep_pad_149
ep_pad_149:
	.space 220
	.section ".text.pop_label!","ax"
	.globl ep_pad_150
ep_pad_150:
	.space 98
	.section ".text.pop_over!","ax"
	.globl ep_pad_151
ep_pad_151:
	.space 30
	.section ".text.probe!","ax"
	.globl ep_pad_152
ep_pad_152:
	.space 38
	.section ".text.read_all!","ax"
	.globl ep_pad_153
ep_pad_153:
	.space 248
	.section ".text.read_folder!","ax"
	.globl ep_pad_154
ep_pad_154:
	.space 250
	.section ".text.redraw_nav!","ax"
	.globl ep_pad_155
ep_pad_155:
	.space 44
	.section ".text.redraw_under!","ax"
	.globl ep_pad_156
ep_pad_156:
	.space 248
	.section ".text.remember_place!","ax"
	.globl ep_pad_157
ep_pad_157:
	.space 226
	.section ".text.row_at.isra.0!","ax"
	.globl ep_pad_158
ep_pad_158:
	.space 214
	.section ".text.row_few!","ax"
	.globl ep_pad_159
ep_pad_159:
	.space 116
	.section ".text.row_many_soft!","ax"
	.globl ep_pad_160
ep_pad_160:
	.space 170
	.section ".text.row_near!","ax"
	.globl ep_pad_161
ep_pad_161:
	.space 178
	.section ".text.row_y!","ax"
	.globl ep_pad_162
ep_pad_162:
	.space 6
	.section ".text.rows_redraw!","ax"
	.globl ep_pad_163
ep_pad_163:
	.space 250
	.section ".text.run!","ax"
	.globl ep_pad_164
ep_pad_164:
	.space 200
	.section ".text.save_in!","ax"
	.globl ep_pad_165
ep_pad_165:
	.space 118
	.section ".text.save_playlist!","ax"
	.globl ep_pad_166
ep_pad_166:
	.space 188
	.section ".text.scroll_to!","ax"
	.globl ep_pad_167
ep_pad_167:
	.space 248
	.section ".text.select_to!","ax"
	.globl ep_pad_168
ep_pad_168:
	.space 66
	.section ".text.set_labels!","ax"
	.globl ep_pad_169
ep_pad_169:
	.space 112
	.section ".text.set_radio!","ax"
	.globl ep_pad_170
ep_pad_170:
	.space 28
	.section ".text.settings_dialog!","ax"
	.globl ep_pad_171
ep_pad_171:
	.space 198
	.section ".text.settings_flush!","ax"
	.globl ep_pad_172
ep_pad_172:
	.space 218
	.section ".text.settings_key!","ax"
	.globl ep_pad_173
ep_pad_173:
	.space 116
	.section ".text.settings_load!","ax"
	.globl ep_pad_174
ep_pad_174:
	.space 226
	.section ".text.settings_miniwindow!","ax"
	.globl ep_pad_175
ep_pad_175:
	.space 220
	.section ".text.settings_set_miniwindow!","ax"
	.globl ep_pad_176
ep_pad_176:
	.space 226
	.section ".text.sf_geom_setup!","ax"
	.globl ep_pad_177
ep_pad_177:
	.space 38
	.section ".text.show_info!","ax"
	.globl ep_pad_178
ep_pad_178:
	.space 236
	.section ".text.show_page!","ax"
	.globl ep_pad_179
ep_pad_179:
	.space 178
	.section ".text.show_path!","ax"
	.globl ep_pad_180
ep_pad_180:
	.space 86
	.section ".text.skip_by!","ax"
	.globl ep_pad_181
ep_pad_181:
	.space 144
	.section ".text.snap!","ax"
	.globl ep_pad_182
ep_pad_182:
	.space 38
	.section ".text.sncmd_reamp!","ax"
	.globl ep_pad_183
ep_pad_183:
	.space 180
	.section ".text.snd_dac_volume!","ax"
	.globl ep_pad_184
ep_pad_184:
	.space 32
	.section ".text.snd_init!","ax"
	.globl ep_pad_185
ep_pad_185:
	.space 12
	.section ".text.state!","ax"
	.globl ep_pad_186
ep_pad_186:
	.space 208
	.section ".text.step!","ax"
	.globl ep_pad_187
ep_pad_187:
	.space 194
	.section ".text.strpbrk!","ax"
	.globl ep_pad_188
ep_pad_188:
	.space 192
	.section ".text.strtol!","ax"
	.globl ep_pad_189
ep_pad_189:
	.space 142
	.section ".text.strtoul!","ax"
	.globl ep_pad_190
ep_pad_190:
	.space 224
	.section ".text.sup_install!","ax"
	.globl ep_pad_191
ep_pad_191:
	.space 220
	.section ".text.sup_nested!","ax"
	.globl ep_pad_192
ep_pad_192:
	.space 14
	.section ".text.sx_dac_volume!","ax"
	.globl ep_pad_193
ep_pad_193:
	.space 240
	.section ".text.sx_open!","ax"
	.globl ep_pad_194
ep_pad_194:
	.space 248
	.section ".text.sx_play!","ax"
	.globl ep_pad_195
ep_pad_195:
	.space 254
	.section ".text.sx_poll!","ax"
	.globl ep_pad_196
ep_pad_196:
	.space 102
	.section ".text.sx_room!","ax"
	.globl ep_pad_197
ep_pad_197:
	.space 238
	.section ".text.sx_update!","ax"
	.globl ep_pad_198
ep_pad_198:
	.space 54
	.section ".text.sys_form!","ax"
	.globl ep_pad_199
ep_pad_199:
	.space 140
	.section ".text.ted!","ax"
	.globl ep_pad_200
ep_pad_200:
	.space 148
	.section ".text.tell!","ax"
	.globl ep_pad_201
ep_pad_201:
	.space 90
	.section ".text.term_c!","ax"
	.globl ep_pad_202
ep_pad_202:
	.space 246
	.section ".text.text_font!","ax"
	.globl ep_pad_203
ep_pad_203:
	.space 24
	.section ".text.text_plain!","ax"
	.globl ep_pad_204
ep_pad_204:
	.space 194
	.section ".text.tls_ovl_load!","ax"
	.globl ep_pad_205
ep_pad_205:
	.space 230
	.section ".text.track!","ax"
	.globl ep_pad_206
ep_pad_206:
	.space 228
	.section ".text.turn!","ax"
	.globl ep_pad_207
ep_pad_207:
	.space 82
	.section ".text.vp_cpu!","ax"
	.globl ep_pad_208
ep_pad_208:
	.space 52
	.section ".text.vp_halt.part.0!","ax"
	.globl ep_pad_209
ep_pad_209:
	.space 180
	.section ".text.vp_open!","ax"
	.globl ep_pad_210
ep_pad_210:
	.space 230
	.section ".text.vp_step!","ax"
	.globl ep_pad_211
ep_pad_211:
	.space 186
	.section ".text.vp_volume!","ax"
	.globl ep_pad_212
ep_pad_212:
	.space 232
	.section ".text.walk!","ax"
	.globl ep_pad_213
ep_pad_213:
	.space 176
	.section ".text.win_paint_some!","ax"
	.globl ep_pad_214
ep_pad_214:
	.space 168
	.section ".rodata.CSWTCH.1368!","a"
	.globl ep_pad_215
ep_pad_215:
	.space 56
	.section ".rodata.CSWTCH.577!","a"
	.globl ep_pad_216
ep_pad_216:
	.space 200
	.section ".rodata.add_types!","a"
	.globl ep_pad_217
ep_pad_217:
	.space 248
	.section ".rodata.c6_of!","a"
	.globl ep_pad_218
ep_pad_218:
	.space 64
	.section ".rodata.cpu_cf_alert!","a"
	.globl ep_pad_219
ep_pad_219:
	.space 154
	.section ".rodata.dsp_mp3_prog_ext!","a"
	.globl ep_pad_220
ep_pad_220:
	.space 72
	.section ".rodata.gain_tab!","a"
	.globl ep_pad_221
ep_pad_221:
	.space 240
	.section ".rodata.gm_on.4!","a"
	.globl ep_pad_222
ep_pad_222:
	.space 250
	.section ".rodata.m3u_add.str1.1!","a"
	.globl ep_pad_223
ep_pad_223:
	.space 96
	.section ".rodata.mwnote.8!","a"
	.globl ep_pad_224
ep_pad_224:
	.space 182
	.section ".rodata.pages.14!","a"
	.globl ep_pad_225
ep_pad_225:
	.space 11
	.section ".rodata.pcm_probe.str1.1!","a"
	.globl ep_pad_226
ep_pad_226:
	.space 1
	.section ".rodata.pen.1!","a"
	.globl ep_pad_227
ep_pad_227:
	.space 249
	.section ".rodata.pls_read.str1.1!","a"
	.globl ep_pad_228
ep_pad_228:
	.space 3
	.section ".rodata.save_types!","a"
	.globl ep_pad_229
ep_pad_229:
	.space 216
	.section ".rodata.skip_big!","a"
	.globl ep_pad_230
ep_pad_230:
	.space 72
	.section ".rodata.skip_small!","a"
	.globl ep_pad_231
ep_pad_231:
	.space 8
	.section ".rodata.std_pal!","a"
	.globl ep_pad_232
ep_pad_232:
	.space 144
	.section ".rodata.v16.3!","a"
	.globl ep_pad_233
ep_pad_233:
	.space 240
	.section ".rodata.vol_att!","a"
	.globl ep_pad_234
ep_pad_234:
	.space 232
	.section ".rodata.xb_rates!","a"
	.globl ep_pad_235
ep_pad_235:
	.space 252
	.section ".data.!","aw"
	.globl ep_pad_236
ep_pad_236:
	.space 252
	.section ".data.cf.8!","aw"
	.globl ep_pad_237
ep_pad_237:
	.space 252
	.section ".data.fails.0!","aw"
	.globl ep_pad_238
ep_pad_238:
	.space 252
	.section ".data.fp_cur!","aw"
	.globl ep_pad_239
ep_pad_239:
	.space 252
	.section ".data.k.1!","aw"
	.globl ep_pad_240
ep_pad_240:
	.space 254
	.section ".data.k.2!","aw"
	.globl ep_pad_241
ep_pad_241:
	.space 2
	.section ".data.keep_under!","aw"
	.globl ep_pad_242
ep_pad_242:
	.space 254
	.section ".data.kept_size!","aw"
	.globl ep_pad_243
ep_pad_243:
	.space 252
	.section ".data.kind!","aw"
	.globl ep_pad_244
ep_pad_244:
	.space 252
	.section ".data.log_h!","aw"
	.globl ep_pad_245
ep_pad_245:
	.space 252
	.section ".data.margin_least!","aw"
	.globl ep_pad_246
ep_pad_246:
	.space 252
	.section ".data.name.5!","aw"
	.globl ep_pad_247
ep_pad_247:
	.space 32
	.section ".data.net_sendbuf!","aw"
	.globl ep_pad_248
ep_pad_248:
	.space 4
	.section ".data.s_aln!","aw"
	.globl ep_pad_249
ep_pad_249:
	.space 252
	.section ".data.skip_drawn!","aw"
	.globl ep_pad_250
ep_pad_250:
	.space 252
	.section ".data.vol_due!","aw"
	.globl ep_pad_251
ep_pad_251:
	.space 252
	.section ".data.xs_end_at!","aw"
	.globl ep_pad_252
ep_pad_252:
	.space 66
	.section ".bss.aes_hosts!","aw",@nobits
	.globl ep_pad_253
ep_pad_253:
	.space 252
	.section ".bss.b.0!","aw",@nobits
	.globl ep_pad_254
ep_pad_254:
	.space 144
	.section ".bss.b.8!","aw",@nobits
	.globl ep_pad_255
ep_pad_255:
	.space 32
	.section ".bss.b_btn!","aw",@nobits
	.globl ep_pad_256
ep_pad_256:
	.space 248
	.section ".bss.bar_pal!","aw",@nobits
	.globl ep_pad_257
ep_pad_257:
	.space 252
	.section ".bss.base_ms!","aw",@nobits
	.globl ep_pad_258
ep_pad_258:
	.space 252
	.section ".bss.bend.1!","aw",@nobits
	.globl ep_pad_259
ep_pad_259:
	.space 224
	.section ".bss.blk!","aw",@nobits
	.globl ep_pad_260
ep_pad_260:
	.space 252
	.section ".bss.budget_us!","aw",@nobits
	.globl ep_pad_261
ep_pad_261:
	.space 252
	.section ".bss.buf.0!","aw",@nobits
	.globl ep_pad_262
ep_pad_262:
	.space 8
	.section ".bss.cmp_hz!","aw",@nobits
	.globl ep_pad_263
ep_pad_263:
	.space 248
	.section ".bss.cmp_snd!","aw",@nobits
	.globl ep_pad_264
ep_pad_264:
	.space 252
	.section ".bss.conn_hosts_path!","aw",@nobits
	.globl ep_pad_265
ep_pad_265:
	.space 56
	.section ".bss.cpu_is080!","aw",@nobits
	.globl ep_pad_266
ep_pad_266:
	.space 242
	.section ".bss.crash_slot!","aw",@nobits
	.globl ep_pad_267
ep_pad_267:
	.space 252
	.section ".bss.ctab!","aw",@nobits
	.globl ep_pad_268
ep_pad_268:
	.space 254
	.section ".bss.dlg_badge!","aw",@nobits
	.globl ep_pad_269
ep_pad_269:
	.space 252
	.section ".bss.done.5!","aw",@nobits
	.globl ep_pad_270
ep_pad_270:
	.space 248
	.section ".bss.dw!","aw",@nobits
	.globl ep_pad_271
ep_pad_271:
	.space 250
	.section ".bss.el!","aw",@nobits
	.globl ep_pad_272
ep_pad_272:
	.space 248
	.section ".bss.ended!","aw",@nobits
	.globl ep_pad_273
ep_pad_273:
	.space 252
	.section ".bss.f_r!","aw",@nobits
	.globl ep_pad_274
ep_pad_274:
	.space 254
	.section ".bss.f_w!","aw",@nobits
	.globl ep_pad_275
ep_pad_275:
	.space 254
	.section ".bss.fbuf!","aw",@nobits
	.globl ep_pad_276
ep_pad_276:
	.space 252
	.section ".bss.fifo!","aw",@nobits
	.globl ep_pad_277
ep_pad_277:
	.space 252
	.section ".bss.find_near!","aw",@nobits
	.globl ep_pad_278
ep_pad_278:
	.space 254
	.section ".bss.fn!","aw",@nobits
	.globl ep_pad_279
ep_pad_279:
	.space 128
	.section ".bss.frac!","aw",@nobits
	.globl ep_pad_280
ep_pad_280:
	.space 252
	.section ".bss.fs!","aw",@nobits
	.globl ep_pad_281
ep_pad_281:
	.space 126
	.section ".bss.g3_asked!","aw",@nobits
	.globl ep_pad_282
ep_pad_282:
	.space 254
	.section ".bss.gem_screen_held!","aw",@nobits
	.globl ep_pad_283
ep_pad_283:
	.space 252
	.section ".bss.hosts_read!","aw",@nobits
	.globl ep_pad_284
ep_pad_284:
	.space 252
	.section ".bss.inf!","aw",@nobits
	.globl ep_pad_285
ep_pad_285:
	.space 132
	.section ".bss.is080!","aw",@nobits
	.globl ep_pad_286
ep_pad_286:
	.space 252
	.section ".bss.k_fwd!","aw",@nobits
	.globl ep_pad_287
ep_pad_287:
	.space 236
	.section ".bss.kept_name!","aw",@nobits
	.globl ep_pad_288
ep_pad_288:
	.space 192
	.section ".bss.l.5!","aw",@nobits
	.globl ep_pad_289
ep_pad_289:
	.space 228
	.section ".bss.late_max!","aw",@nobits
	.globl ep_pad_290
ep_pad_290:
	.space 252
	.section ".bss.len_ms!","aw",@nobits
	.globl ep_pad_291
ep_pad_291:
	.space 252
	.section ".bss.lf.0!","aw",@nobits
	.globl ep_pad_292
ep_pad_292:
	.space 248
	.section ".bss.list_keys!","aw",@nobits
	.globl ep_pad_293
ep_pad_293:
	.space 252
	.section ".bss.log_h_for!","aw",@nobits
	.globl ep_pad_294
ep_pad_294:
	.space 244
	.section ".bss.making!","aw",@nobits
	.globl ep_pad_295
ep_pad_295:
	.space 248
	.section ".bss.map_rgb!","aw",@nobits
	.globl ep_pad_296
ep_pad_296:
	.space 250
	.section ".bss.mem!","aw",@nobits
	.globl ep_pad_297
ep_pad_297:
	.space 252
	.section ".bss.mem_cap!","aw",@nobits
	.globl ep_pad_298
ep_pad_298:
	.space 252
	.section ".bss.mem_n!","aw",@nobits
	.globl ep_pad_299
ep_pad_299:
	.space 252
	.section ".bss.minf!","aw",@nobits
	.globl ep_pad_300
ep_pad_300:
	.space 128
	.section ".bss.msg.0!","aw",@nobits
	.globl ep_pad_301
ep_pad_301:
	.space 240
	.section ".bss.mw_cmd!","aw",@nobits
	.globl ep_pad_302
ep_pad_302:
	.space 2
	.section ".bss.mwin_saved!","aw",@nobits
	.globl ep_pad_303
ep_pad_303:
	.space 242
	.section ".bss.named!","aw",@nobits
	.globl ep_pad_304
ep_pad_304:
	.space 252
	.section ".bss.on!","aw",@nobits
	.globl ep_pad_305
ep_pad_305:
	.space 252
	.section ".bss.ov!","aw",@nobits
	.globl ep_pad_306
ep_pad_306:
	.space 252
	.section ".bss.p!","aw",@nobits
	.globl ep_pad_307
ep_pad_307:
	.space 130
	.section ".bss.p.3!","aw",@nobits
	.globl ep_pad_308
ep_pad_308:
	.space 56
	.section ".bss.pal!","aw",@nobits
	.globl ep_pad_309
ep_pad_309:
	.space 254
	.section ".bss.path!","aw",@nobits
	.globl ep_pad_310
ep_pad_310:
	.space 76
	.section ".bss.pcmbuf!","aw",@nobits
	.globl ep_pad_311
ep_pad_311:
	.space 254
	.section ".bss.pend!","aw",@nobits
	.globl ep_pad_312
ep_pad_312:
	.space 246
	.section ".bss.play_busy_turn!","aw",@nobits
	.globl ep_pad_313
ep_pad_313:
	.space 252
	.section ".bss.pool_used!","aw",@nobits
	.globl ep_pad_314
ep_pad_314:
	.space 2
	.section ".bss.pre_reset!","aw",@nobits
	.globl ep_pad_315
ep_pad_315:
	.space 240
	.section ".bss.prog.2!","aw",@nobits
	.globl ep_pad_316
ep_pad_316:
	.space 240
	.section ".bss.pst!","aw",@nobits
	.globl ep_pad_317
ep_pad_317:
	.space 252
	.section ".bss.rp!","aw",@nobits
	.globl ep_pad_318
ep_pad_318:
	.space 252
	.section ".bss.s_m2!","aw",@nobits
	.globl ep_pad_319
ep_pad_319:
	.space 252
	.section ".bss.s_mask!","aw",@nobits
	.globl ep_pad_320
ep_pad_320:
	.space 236
	.section ".bss.s_miss!","aw",@nobits
	.globl ep_pad_321
ep_pad_321:
	.space 252
	.section ".bss.saving!","aw",@nobits
	.globl ep_pad_322
ep_pad_322:
	.space 252
	.section ".bss.sdir.6!","aw",@nobits
	.globl ep_pad_323
ep_pad_323:
	.space 128
	.section ".bss.seen!","aw",@nobits
	.globl ep_pad_324
ep_pad_324:
	.space 252
	.section ".bss.sent!","aw",@nobits
	.globl ep_pad_325
ep_pad_325:
	.space 248
	.section ".bss.settings_changed!","aw",@nobits
	.globl ep_pad_326
ep_pad_326:
	.space 252
	.section ".bss.sf_geom!","aw",@nobits
	.globl ep_pad_327
ep_pad_327:
	.space 252
	.section ".bss.sl_tree!","aw",@nobits
	.globl ep_pad_328
ep_pad_328:
	.space 240
	.section ".bss.st!","aw",@nobits
	.globl ep_pad_329
ep_pad_329:
	.space 252
	.section ".bss.std_pens!","aw",@nobits
	.globl ep_pad_330
ep_pad_330:
	.space 252
	.section ".bss.t_mask!","aw",@nobits
	.globl ep_pad_331
ep_pad_331:
	.space 236
	.section ".bss.v_mask!","aw",@nobits
	.globl ep_pad_332
ep_pad_332:
	.space 236
	.section ".bss.was_reset!","aw",@nobits
	.globl ep_pad_333
ep_pad_333:
	.space 252
