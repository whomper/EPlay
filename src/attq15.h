/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * attq15.h - the volume's attenuation as a Q15 gain: att steps of 1.5 dB
 * down, 0 (full) to 40 (60 dB down, the bar's lowest before off: Erez,
 * 17 Nov 2026, "the lower volume is not low enough just before full
 * off"). The first 16 as they always were (sncmd.c's, dspopn.c's and
 * dspopl.c's tables); past them each 4 steps a halving (6.02 dB), so 40
 * is 32, 60.2 dB down.
 */
#ifndef ATTQ15_H
#define ATTQ15_H

static const long att_q15_tab[16] = { 32768, 27571, 23198, 19519, 16423, 13818, 11627, 9783,
				      8231,  6925,  5827,  4903,  4125,  3471,  2920,  2457 };

static inline long att_q15(int att)
{
	if (att <= 0)
		return 32768;
	if (att < 16)
		return att_q15_tab[att];
	if (att >= 72)
		return 0;
	return att_q15_tab[12 + (att & 3)] >> ((att - 12) >> 2);
}

#endif
