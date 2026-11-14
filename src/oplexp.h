/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * oplexp.h - the YM3812's and the YMF262's eight waveforms, word for word
 * as src/oplfast.c makes them, from what the program carries: the sine's
 * first quarter (opl_sinq, 256 words) and 2^(-k/32) in 40 bits (opl_rq,
 * 32): the 68030 makes the 8,192 words as it loads the DSP (atari/
 * dspopl.c), rather than carrying them (16 KB and 2 KB less of program:
 * the start, 14 Nov 2026). tools/mkopltab.c makes those tables and checks
 * each of the 8,192 against oplfast.c's, or stops the build.
 */
#ifndef EPLAY_OPLEXP_H
#define EPLAY_OPLEXP_H

#include "oplfast.h"

/* waveform 0 at k (0-1023), signed: its quarters by symmetry */
static inline long oplx_sin(const unsigned long *sq, int k)
{
	int q = k & 255;
	long s = (long)sq[k & 256 ? 255 - q : q];
	return k & 512 ? -s : s;
}

/* the logarithmic saw's p (0-511): 2^(-p/32) of the full scale, rounded */
static inline long oplx_ramp(const unsigned long long *rq, int p)
{
	int sh = 40 + (p >> 5);
	return (long)(((unsigned long long)OPLF_FULL * rq[p & 31] + (1ULL << (sh - 1))) >> sh);
}

/* waveform w's (0-7) word at k (0-1023), 24 bits */
static inline unsigned long oplx_wave(const unsigned long *sq, const unsigned long long *rq, int w, int k)
{
	long s = oplx_sin(sq, k), v, q;
	if (w == 0)
		v = s;
	else if (w == 1)
		v = k < 512 ? s : 0;
	else if (w == 2)
		v = k < 512 ? s : -s;
	else if (w == 3)
		v = (k & 256) ? 0 : k < 512 ? s : -s;
	else if (w == 6)
		v = k & 0x200 ? -OPLF_FULL : OPLF_FULL;
	else if (w == 7) {
		v = oplx_ramp(rq, k & 0x200 ? (k & 0x1ff) ^ 0x1ff : k);
		if (k & 0x200)
			v = -v;
	} else {
		/* (4, 5: the sine at twice its rate, its mirrored quarters' index
		   ^ $FE, as the chip) */
		q = oplx_sin(sq, (k & 0x80 ? (k ^ 0xff) << 1 : k << 1) & 0xff);
		v = k & 0x200 ? 0 : w == 4 && (k & 0x300) == 0x100 ? -q : q;
	}
	return (unsigned long)v & 0xffffff;
}

#define OPL_GAIN_N 832		/* the gain table's words not 0 (of OPLF_GAINS) */

#endif
