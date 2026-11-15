/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * aycpu.c - the YM2149 made on the CPU (aycpu.h).
 */
#include <string.h>
#include "aycpu.h"

#define AY_CLOCK 2000000L	/* the Atari's YM2149 (vgm.c turns an AY's periods to it) */

/* the 32 levels, 1.5 dB apart (2^(i - 31)/4 of 8,000; three at the top
   24,000) */
static const short vol[32] = {
	0, 44, 53, 62, 74, 88, 105, 125, 149, 177, 210, 250, 297, 354, 420, 500,
	595, 707, 841, 1000, 1189, 1414, 1682, 2000, 2378, 2828, 3364, 4000, 4757, 5657, 6727, 8000
};

void aycpu_init(AYCPU *a, long rate)
{
	memset(a, 0, sizeof(*a));
	a->step = (unsigned long)((AY_CLOCK * 256L + rate / 2) / rate);
	a->lfsr = 1;
	a->reg[7] = 0x3f;		/* (all off, as at a reset) */
	a->edir = -1;
	a->ehold = 1;
}

/* the envelope from its start: shape 0-3 and 9 fall, 4-7 and 13 rise
   (ATT, bit 2), each from its end */
static void env_start(AYCPU *a)
{
	int att = (a->reg[13] & 4) != 0;
	a->estep = att ? 0 : 31;
	a->edir = att ? 1 : -1;
	a->ehold = 0;
	a->ecnt = 0;
}

/* a cycle's end: CONT (bit 3) off, silent and held; HOLD (bit 0), held at
   its end or, with ALT (bit 1), at its start; else ALT turns it round,
   or it begins again */
static void env_end(AYCPU *a)
{
	int s = a->reg[13];
	if (!(s & 8)) {
		a->estep = 0;
		a->ehold = 1;
	} else if (s & 1) {
		if (s & 2)
			a->estep = a->edir > 0 ? 0 : 31;
		else
			a->estep = a->edir > 0 ? 31 : 0;
		a->ehold = 1;
	} else if (s & 2) {
		a->edir = -a->edir;
		a->estep = a->edir > 0 ? 0 : 31;
	} else
		a->estep = a->edir > 0 ? 0 : 31;
}

void aycpu_write(AYCPU *a, int reg, int val)
{
	if (reg < 0 || reg > 13)
		return;
	a->reg[reg] = (unsigned char)val;
	if (reg == 13)
		env_start(a);
}

void aycpu_run(AYCPU *a, short *lr, int n)
{
	int k, c;
	unsigned long half[3], np, ep;
	int lev[3], env[3];
	for (c = 0; c < 3; c++) {
		unsigned long tp = a->reg[2 * c] | (unsigned long)(a->reg[2 * c + 1] & 15) << 8;
		half[c] = (tp ? tp : 1) * 8UL << 8;
		lev[c] = a->reg[8 + c] & 15;
		env[c] = (a->reg[8 + c] & 16) != 0;
		if (half[c] <= a->step)
			a->tout[c] = 2;
		else if (a->tout[c] == 2)
			a->tout[c] = 1;
	}
	np = ((a->reg[6] & 31) ? (a->reg[6] & 31) : 1) * 16UL << 8;
	ep = (a->reg[11] | (unsigned long)a->reg[12] << 8);
	ep = (ep ? ep : 1) * 8UL << 8;
	for (k = 0; k < n; k++) {
		long sum = 0, v;
		/* the tones */
		for (c = 0; c < 3; c++)
			if (a->tout[c] != 2) {
				a->tcnt[c] += a->step;
				while (a->tcnt[c] >= half[c]) {
					a->tcnt[c] -= half[c];
					a->tout[c] ^= 1;
				}
			}
		/* the noise */
		a->ncnt += a->step;
		while (a->ncnt >= np) {
			a->ncnt -= np;
			a->lfsr = (a->lfsr >> 1) | (((a->lfsr ^ (a->lfsr >> 3)) & 1) << 16);
		}
		a->nout = (int)(a->lfsr & 1);
		/* the envelope */
		if (!a->ehold) {
			a->ecnt += a->step;
			while (a->ecnt >= ep && !a->ehold) {
				a->ecnt -= ep;
				a->estep += a->edir;
				if (a->estep < 0 || a->estep > 31)
					env_end(a);
			}
		}
		/* the mixer: a tone or the noise switched off (register 7's bit
		   set) passes; the levels */
		for (c = 0; c < 3; c++) {
			int tone_off = (a->reg[7] >> c) & 1, noise_off = (a->reg[7] >> (c + 3)) & 1;
			int t = tone_off ? 1 : a->tout[c];
			int i = env[c] ? a->estep : (lev[c] ? 2 * lev[c] + 1 : 0);
			if (!noise_off && !a->nout)
				continue;
			if (t == 1)
				sum += vol[i];
			else if (t == 2)
				sum += vol[i] >> 1;
		}
		/* the DC out: the mean followed with a time of 1,024 samples (some 8 Hz at 49 kHz) */
		a->dc += ((sum << 8) - a->dc) >> 10;
		v = sum - (a->dc >> 8);
		for (c = 0; c < 2; c++) {
			long o = lr[2 * k + c] + v;
			lr[2 * k + c] = (short)(o > 32767 ? 32767 : o < -32768 ? -32768 : o);
		}
	}
}
