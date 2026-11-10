/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sncmd.c - the SN76489's writes as sn.a56's commands (sncmd.h).
 */
#include "sncmd.h"

/* the amplitudes, 2 dB a step (0.2 of full scale at 0: four channels
   at most 0.8), and the volume's 1.5 dB steps, Q15 */
static const long amp_of[16] = { 1677722, 1332662, 1058571, 840853, 667913, 530542, 421425, 334749,
				 265901,  211213,  167772,  133266, 105857, 84085,  66791,  0 };
static const long att_q15[16] = { 32768, 27571, 23198, 19519, 16423, 13818, 11627, 9783,
				  8231,  6925,  5827,  4903,  4125,  3471,  2920,  2457 };

static void cmd(SNCMD *s, int c, long v)
{
	s->put(s->u, s->base + c);
	s->put(s->u, v);
}

/* the step of a square wave turning over every n counts (Q23 of a
   sample), 0 above a quarter of the rate (a level: held up) */
static long step_of(const SNCMD *s, long n)
{
	long v;
	if (n <= 0)
		return 0;
	v = (s->q11 << 12) / n;
	return v >= (1L << 22) ? 0 : v;
}

static long amp(const SNCMD *s, int ch)
{
	if (s->mute)
		return 0;
	return ((amp_of[s->vol[ch] & 15] >> 8) * att_q15[s->att & 15] >> 7) >> s->shift;	/* (in 32 bits) */
}

static void tone(SNCMD *s, int ch)
{
	long v = step_of(s, s->period[ch]);
	cmd(s, ch, v);
	if (!v)
		cmd(s, 11, ch);		/* (held up: the volume plays it) */
}

static void noise(SNCMD *s)
{
	long v;
	if ((s->nctl & 3) == 3)
		v = step_of(s, 2L * s->period[2]);
	else
		v = step_of(s, 32L << (s->nctl & 3));
	cmd(s, 3, v);
}

void sncmd_write(SNCMD *s, int b)
{
	int ch;
	if (b & 0x80) {
		ch = (b >> 5) & 3;
		s->latch = b & 0x70;
		if (b & 0x10) {
			s->vol[ch] = b & 15;
			cmd(s, 4 + ch, amp(s, ch));
		} else if (ch < 3) {
			s->period[ch] = (s->period[ch] & 0x3f0) | (b & 15);
			tone(s, ch);
			if (ch == 2 && (s->nctl & 3) == 3)
				noise(s);
		} else {
			s->nctl = b & 7;
			cmd(s, 8, (s->nctl >> 2) & 1);
			noise(s);
		}
	} else {
		ch = (s->latch >> 5) & 3;
		if (s->latch & 0x10) {
			s->vol[ch] = b & 15;
			cmd(s, 4 + ch, amp(s, ch));
		} else if (ch < 3) {
			s->period[ch] = (s->period[ch] & 15) | ((b & 0x3f) << 4);
			tone(s, ch);
			if (ch == 2 && (s->nctl & 3) == 3)
				noise(s);
		} else {
			s->nctl = b & 7;
			cmd(s, 8, (s->nctl >> 2) & 1);
			noise(s);
		}
	}
}

void sncmd_reamp(SNCMD *s)
{
	int ch;
	for (ch = 0; ch < 4; ch++)
		cmd(s, 4 + ch, amp(s, ch));
}

void sncmd_init(SNCMD *s, unsigned long clock, long rate, int fb, int width, int base, int shift,
		void (*put)(void *u, long w), void *u)
{
	int ch;
	s->q11 = (long)(((clock ? clock : 3579545UL) / 16) << 11) / rate;
	s->base = base;
	s->shift = shift;
	s->put = put;
	s->u = u;
	for (ch = 0; ch < 3; ch++)
		s->period[ch] = 0;
	for (ch = 0; ch < 4; ch++)
		s->vol[ch] = 15;
	s->latch = s->nctl = 0;
	s->att = s->mute = 0;
	cmd(s, 9, fb ? fb : 9);
	cmd(s, 10, 1L << ((width ? width : 16) - 1));
	cmd(s, 8, 1);
	for (ch = 0; ch < 4; ch++)
		cmd(s, 4 + ch, 0);
}
