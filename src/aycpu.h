/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * aycpu.h - the YM2149 (and the AY-3-8910 a VGM has, its periods turned to
 * the Atari's 2 MHz by vgm.c) made on the CPU (phase 11, step 4: a machine
 * with no YM2149, a Milan's or a FireBee's, or the chips made ahead on a
 * 68060 with the file's AY among them). The chip at 2 MHz as the Atari's:
 * three square tones (each a half period of 8 x its 12-bit period in the
 * chip's clocks), a noise of a 17-bit shift register (taps 0 and 3,
 * shifted each 16 x its 5-bit period), the envelope (32 steps of 8 x its
 * 16-bit period, its 16 shapes), the mixer (register 7) and the levels: 32
 * of 1.5 dB, a fixed level L at 2L + 1. A tone faster than the output can
 * show (its half period under a sample) is its average, half its level, as
 * the Atari's output filter makes of it. The sum of the three, mono, its
 * DC taken out (the Atari's capacitor), added into 16-bit stereo. In 32
 * bits alone.
 */
#ifndef EPLAY_AYCPU_H
#define EPLAY_AYCPU_H

typedef struct {
	unsigned char reg[16];
	unsigned long step;		/* the chip's clocks a sample, Q8 */
	unsigned long tcnt[3], ncnt, ecnt;
	int tout[3];			/* 0, 1, or 2: faster than a sample (half) */
	unsigned long lfsr;
	int nout;
	int estep, edir, ehold;		/* the envelope: 0-31, up (1) or down (-1), held */
	long dc;			/* the DC blocker's mean, Q8 */
} AYCPU;

/* at the output's rate (Hz) */
void aycpu_init(AYCPU *a, long rate);
/* register reg (0-13) to val */
void aycpu_write(AYCPU *a, int reg, int val);
/* n samples added into lr (left, right, 2 n shorts) */
void aycpu_run(AYCPU *a, short *lr, int n);

#endif
