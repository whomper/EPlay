/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * gmscpu.c - GMSCPU.PRG: the internal synth's engine (src/gmsynth.c) timed
 * on the CPU it runs on, as EPLAY.PRG builds it: a second at 24,585 Hz
 * with 1, 4, 8 and 16 voices sounding (an organ's held notes, looped),
 * C:\SYNTH.DAT its set. A measurement for the voices a CPU carries
 * (DESIGN.md, "The synth"), not a test.
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "gmsynth.h"

long nf_on = 1;

#define RATE 24585L

static GMSET g;
static GMSYN s;
static short out[2 * 512];
static unsigned char set[460000];

static long run(int voices)
{
	int k;
	long i;
	unsigned long t0;
	gms_init(&s, &g, RATE, 32);
	for (k = 0; k < voices; k++) {
		unsigned char m[3];
		m[0] = 0xc0 | (k & 7);
		m[1] = 16;		/* (the drawbar organ: held, looped) */
		gms_midi(&s, m, 2);
		m[0] = 0x90 | (k & 7);
		m[1] = (unsigned char)(48 + 2 * k);
		m[2] = 100;
		gms_midi(&s, m, 3);
	}
	t0 = tos_ms();
	for (i = 0; i < RATE; i += 512)
		gms_render(&s, out, 512);
	return (long)(tos_ms() - t0);
}

int main(void)
{
	char line[200];
	long fh, n, t1, t4, t8, t16;
	const char *why;
	fh = Fopen("C:\\SYNTH.DAT", 0);
	if (fh < 0)
		return 1;
	n = Fread((short)fh, sizeof(set) - 16, set);
	Fclose((short)fh);
	if (n <= 0 || !gm_open(&g, set, n, &why)) {
		nf_debug("GMSCPU the set refused\n");
		return 1;
	}
	gms_prepare(&g, set);
	t1 = run(1);
	t4 = run(4);
	t8 = run(8);
	t16 = run(16);
	snprintf(line, sizeof(line), "GMSCPU the synth, a second at %ld Hz: 1 voice %ld ms, 4 voices %ld ms, 8 voices %ld ms, "
		 "16 voices %ld ms\n", RATE, t1, t4, t8, t16);
	nf_debug(line);
	return 0;
}
