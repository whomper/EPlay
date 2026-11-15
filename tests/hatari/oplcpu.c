/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * oplcpu.c - OPLCPU.PRG: the YM3812's and the YMF262's model made on the
 * CPU (src/oplcpu.c, phase 11 step 3) timed on the CPU it is built for, the
 * 68060: the worst case (every channel keyed and loud, both operators on,
 * feedback 7, the tremolo and the vibrato on each, waveform 1 or 4), a
 * second of it: the OPL2's nine channels at 49,170 and 24,585 Hz, the
 * OPL3's eighteen at 24,585 and 16,390. A measurement for the VGM player on
 * a 68060 with no Falcon DSP (DESIGN.md), not a test.
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "opld.h"
#include "oplcpu.h"
#include "oplexp.h"
#include "opltab.h"

static OPLD chip;
static OPLF model;
static short out[2 * 512];

static void put(void *u, long w)
{
	(void)u;
	oplc_word(&model, w);
}

static long run(int opl3, long rate)
{
	static const OPLC_TABS t = { opl_gain, opl_am, opl_pmw, opl_sinq, opl_rq, OPL_GAIN_N };
	static const int slot[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
	int c, k, i, nch = opl3 ? 18 : 9;
	u32 t0;
	oplc_init(&model, &t);
	oplc_word(&model, OPLC_RESET);
	if (opl3) {
		opld_init3(&chip, 14318180UL, rate);
		opld_channels(&chip, 18);
		opld_write(&chip, 0x105, 1);
	} else {
		opld_init(&chip, 3579545UL, rate);
		opld_write(&chip, 0x01, 0x20);
	}
	opld_write(&chip, 0xbd, 0xc0);		/* (the tremolo and the vibrato deep) */
	for (c = 0; c < nch; c++) {
		int base = c < 9 ? 0 : 0x100, ch = c % 9;
		for (k = 0; k < 2; k++) {
			int o = base + slot[ch] + 3 * k;
			opld_write(&chip, 0x20 + o, 0xc1 + k);
			opld_write(&chip, 0x40 + o, k ? 0 : 0x10);
			opld_write(&chip, 0x60 + o, 0xf1);
			opld_write(&chip, 0x80 + o, 0x0f);
			opld_write(&chip, 0xe0 + o, opl3 ? 4 : 1);
		}
		opld_write(&chip, base + 0xc0 + ch, 0x3e);
		opld_write(&chip, base + 0xa0 + ch, 0x40 + 16 * ch);
		opld_write(&chip, base + 0xb0 + ch, 0x31);
	}
	opld_send(&chip, put, 0);
	t0 = tos_ms();
	for (i = 0; i < rate; i += 512)
		oplc_run(&model, out, 512);
	return (long)(tos_ms() - t0);
}

extern int div64_cpu60;		/* atari/div64.c */

int main(void)
{
	char line[200];
	long a, b, c, d;
	div64_cpu60 = 1;		/* (as EPlay sets it on a 68060, from _CPU) */
	a = run(0, 49170);
	b = run(0, 24585);
	c = run(1, 24585);
	d = run(1, 16390);
	snprintf(line, sizeof(line), "OPLCPU the CPU's OPL model (oplcpu.c), a second, the worst case: the OPL2's 9 "
		 "channels at 49170 Hz %ld ms, at 24585 Hz %ld ms; the OPL3's 18 at 24585 Hz %ld ms, at 16390 Hz %ld ms\n",
		 a, b, c, d);
	nf_debug(line);
	return 0;
}
