/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * rgain.c - ReplayGain's values and the steps to play at (rgain.h). No
 * floating point: the 68000's EPLAYST.PRG has none to spare.
 */
#include "rgain.h"

void rg_clear(RGAIN *g)
{
	g->track = g->album = RG_NONE;
	g->tpeak = g->apeak = 0;
}

static const char *skip_sp(const char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

/* a decimal number as value * 10^places, rounded; 0 if none (*ok 0) */
static long fixed(const char *s, int places, int *ok)
{
	long v = 0, frac = 0;
	int neg = 0, digits = 0, d = 0, round = 0;
	s = skip_sp(s);
	if (*s == '-' || *s == '+')
		neg = *s++ == '-';
	while (*s >= '0' && *s <= '9') {
		if (v < 100000L)
			v = v * 10 + (*s - '0');
		s++;
		digits++;
	}
	if (*s == '.' || *s == ',') {
		s++;
		while (*s >= '0' && *s <= '9') {
			if (d < places) {
				frac = frac * 10 + (*s - '0');
				d++;
			} else if (d == places) {
				round = *s >= '5';
				d++;
			}
			s++;
			digits++;
		}
	}
	*ok = digits > 0 && v <= (places > 2 ? 99 : 9999);	/* (no tag means more: none) */
	if (!*ok)
		return 0;
	if (d > places)
		d = places;
	while (d < places) {
		frac *= 10;
		d++;
	}
	while (places-- > 0)
		v *= 10;
	v += frac + round;
	return neg ? -v : v;
}

int rg_parse_db(const char *s)
{
	int ok;
	long v = fixed(s, 2, &ok);
	if (!ok || v < -9999 || v > 9999)
		return RG_NONE;		/* (more than 99.99 dB is no gain a tag means) */
	return (int)v;
}

long rg_parse_peak(const char *s)
{
	int ok;
	long v = fixed(s, 6, &ok);	/* millionths */
	if (!ok || v <= 0 || v > 16000000L)
		return 0;
	/* v / 10^6 * 65536 exactly (1.0 is 65536: a peak of 1.0 lets 0 dB
	   be), without overflow: 65536 / 10^6 = 4096 / 62500 */
	return v / 62500 * 4096 + (v % 62500 * 4096 + 31250) / 62500;
}

long rg_float_q16(unsigned long b)
{
	int e = (int)((b >> 23) & 0xff);
	unsigned long m = (b & 0x7fffffUL) | 0x800000UL;
	if (b & 0x80000000UL || e == 0 || e == 0xff)
		return 0;		/* (negative, zero or denormal, infinite: none) */
	/* value = m * 2^(e - 127 - 23); in Q16: m * 2^(e - 134) */
	e -= 134;
	if (e >= 0)
		return e > 4 ? 0 : (long)(m << e);	/* (above 16: none) */
	if (e < -24)
		return 0;
	return (long)((m + (1UL << (-e - 1))) >> -e);
}

int rg_lame(int field, int name)
{
	int v = field & 0x1ff;
	if (((field >> 13) & 7) != name)
		return RG_NONE;		/* (another kind, or not set: 0) */
	return (field & 0x200) ? -v * 10 : v * 10;
}

/* 65536 * 2^(-s/4), s from -RG_CUT to RG_BOOST: the highest peak a gain of
   s steps keeps at 1.0 or below */
static const long lim[RG_CUT + 1 + RG_BOOST] = {
	1048576, 881744, 741455, 623487, 524288, 440872, 370728, 311744, 262144,
	220436, 185364, 155872, 131072, 110218, 92682, 77936, 65536,
	55109, 46341, 38968, 32768, 27554, 23170, 19484, 16384
};

int rg_steps(int cdb, long peak)
{
	/* the nearest step of 150.5 hundredths: (cdb * 2 + 150.5) / 301, floored */
	long t = (long)cdb * 4 + 301;
	int s = (int)(t >= 0 ? t / 602 : -((-t + 601) / 602));
	if (s > RG_BOOST)
		s = RG_BOOST;
	if (s < -RG_CUT)
		s = -RG_CUT;
	if (peak <= 0) {
		if (s > 0)
			s = 0;		/* (louder only where the peak says it may) */
	} else
		while (s > -RG_CUT && peak > lim[s + RG_CUT])
			s--;
	return s;
}

int rg_pick(const RGAIN *g, int mode, int shuffled)
{
	int album;
	if (mode == RG_OFF)
		return 0;
	album = mode == RG_ALBUM || (mode == RG_AUTO && !shuffled);
	if (album && g->album != RG_NONE)
		return rg_steps(g->album, g->apeak ? g->apeak : g->tpeak);
	if (g->track != RG_NONE)
		return rg_steps(g->track, g->tpeak);
	if (g->album != RG_NONE)
		return rg_steps(g->album, g->apeak ? g->apeak : g->tpeak);
	return 0;
}
