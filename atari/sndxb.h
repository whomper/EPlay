/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sndxb.h - snd.h's calls over the XBIOS's sound calls alone, in EPLAY.PRG
 * (sndfb.c built again as sndxb.o: phase 11, step 4), for a machine with
 * no Atari sound hardware but those calls (a Milan's GSXB): snd.c gives
 * each call to these when snd_xb is set, at its start.
 */
#ifndef EPLAY_SNDXB_H
#define EPLAY_SNDXB_H

int sx_init(const char **why);
int sx_take(void);
void sx_give(void);
void sx_exit(void);
void sx_term(void);
long sx_rate_for(long rate, int *ext);
long sx_rate_dsp(long rate, int *ext);
int sx_fdi_set(long rate);
void sx_dsp_out(long rate, int ext);
void sx_dsp_off(void);
void sx_mfp_note(char *b, int n);
long sx_put8_raw(const unsigned char *src, long nin, int bpf, int ol, int orr, unsigned char x);
int sx_open(long rate, int ext);
int sx_open_feed(long rate, int ext);
int sx_open_gms(long rate, int ext);
void sx_dac_volume(int att, int mute);
void sx_close(void);
long sx_ring_frames(void);
void sx_update(void);
void sx_poll(void);
long sx_played(void);
long sx_written(void);
long sx_room(void);
short *sx_ptr(long *n);
void sx_wrote(long n);
void sx_write(const short *frames, long n);
void sx_play(void);
int sx_playing(void);

#endif
