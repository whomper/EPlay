/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sncpu.c - the SN76489 on the CPU (sncpu.h): dsp/sn.a56's sample and
 * commands in C.
 */
#include "sncpu.h"
#include "tickcode.h"

void sncpu_init(SNCPU *s)
{
	int i;
	for (i = 0; i < 4; i++)
		s->ph[i] = s->inc[i] = s->amp[i] = s->sg[i] = 0;
	s->topb = s->lfsr = 0x8000;
	s->taps = 9;
	s->white = 1;
	s->cmd = -1;
}

void sncpu_word(SNCPU *s, long w)
{
	long c = s->cmd;
	w &= 0xffffff;
	if (c < 0) {
		s->cmd = w;
		return;
	}
	s->cmd = -1;
	if (c < 4)
		s->inc[c] = w;
	else if (c < 8) {
		/* an amplitude, its tone's sign kept */
		c -= 4;
		s->amp[c] = w;
		s->sg[c] = s->sg[c] < 0 ? -w : w;
	} else if (c == 8) {
		s->white = w;
		s->lfsr = s->topb;
	} else if (c == 9)
		s->taps = w;
	else if (c == 10)
		s->topb = w;
	else if (c == 11 && w < 4)
		s->sg[w] = s->amp[w];	/* (held up: the volume plays it) */
}

/* (sn.a56's shift: white, the taps' two bits xor; periodic, the bit out;
   in at the top) */
static void shift(SNCPU *s)
{
	long a = s->lfsr, fb;
	if (s->white & 1) {
		long t = a & s->taps;
		fb = t != 0 && t != s->taps;
	} else
		fb = a & 1;
	s->lfsr = fb ? (a >> 1) | s->topb : a >> 1;
}

TICK_CODE("8Asncpu_sample")
long sncpu_sample(SNCPU *s)
{
	long b = 0;
	int i;
	for (i = 0; i < 3; i++) {
		long a = s->ph[i] + s->inc[i];
		if (a & 0x800000L) {
			a &= ~0x800000L;
			s->sg[i] = -s->sg[i];
		}
		s->ph[i] = a;
		b += s->sg[i];
	}
	{
		long a = s->ph[3] + s->inc[3];
		if (a & 0x800000L) {
			s->ph[3] = a & ~0x800000L;
			shift(s);
		} else
			s->ph[3] = a;
	}
	return b + (s->lfsr & 1 ? s->amp[3] : -s->amp[3]);
}
