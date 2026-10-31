/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspmp3.h - the MP3 decoder on the Falcon's DSP (dsp/mp3.a56): locked
 * and loaded, packets sent, frames read back as the sound DMA plays them.
 */
#ifndef DSPMP3_H
#define DSPMP3_H

enum { DSPMP3_OK, DSPMP3_BUSY, DSPMP3_NONE, DSPMP3_FAILED };

/* the DSP locked (Dsp_Lock: refused, DSPMP3_BUSY: another program has it)
   and reset; then the decoder and its tables loaded by dspmp3_load, a
   piece at a time from GEM's steps (about ms each; about 21,500 words in
   all): 1 when it is loaded, 0 while more is to come, -1 if the DSP
   stopped answering */
int dspmp3_open(void);
int dspmp3_load(int ms);
void dspmp3_close(void);	/* unlocked: another program may have it */
void dspmp3_reset(void);	/* its overlap and filter state cleared (a new file) */

/* PLAY: a frame's packet (pk, n words) sent; when ready, its samples
   read as frames (two words, left then right, each the sample's top 16
   bits). In supervisor mode: from the tick (atari/feed.c) */
void dspmp3_tick_send(const unsigned long *pk, int n);
int dspmp3_tick_ready(void);
void dspmp3_tick_read(short *to, long frames);
int dspmp3_failed(void);	/* it stopped answering */

#endif
