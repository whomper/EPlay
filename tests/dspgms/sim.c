/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sim.c GM16 - dsp/gms.a56, the internal synth's render on the DSP, on
 * Hatari's DSP core: blocks the 68030's side writes (src/gmsdsp.c's
 * gmsd_write, from a piece played on gmsynth.c's control: a chord, a
 * melody, the drums, bends, pan, volume, then random messages) loaded
 * into its Y memory and rendered (RENDER), each sample against
 * gmsd_dsp's, the C model, bit for bit; and the cycles a block takes for
 * its voices.
 */
#include <string.h>
#include "gms_prog.h"
#include "../dspsim.h"
#include "gmsdsp.h"

#define RATE 24585
#define BLKY 0x2000

static GMSET g;
static GMSYN syn;
static GMSD_DSP model;
static unsigned char *pool;
static signed char *pts8;
static unsigned short blk[GMSD_WORDS];
static long blocks, bad, most_v;
static unsigned long long worst_cyc, worst_v_cyc;

static void m3(int a, int b, int c)
{
	unsigned char m[3];
	m[0] = (unsigned char)a;
	m[1] = (unsigned char)b;
	m[2] = (unsigned char)c;
	gms_midi(&syn, m, 3);
}

static void one(void)
{
	static unsigned long w24[GMSD_WORDS];
	long lr[2 * GMSD_N];
	int nw = gmsd_write(&syn, pool, pts8, blk), k;
	unsigned long long t;
	for (k = 0; k < nw; k++)
		w24[k] = (unsigned long)blk[k] << 8;
	gmsd_dsp(&model, blk, lr);
	load(4, BLKY, w24, nw);
	put(7);
	put(BLKY);
	t = sim_job();
	if (t > worst_cyc) {
		worst_cyc = t;
		worst_v_cyc = blk[0];
	}
	if (blk[0] > most_v)
		most_v = blk[0];
	for (k = 0; k < 2 * GMSD_N; k++) {
		long got = get();
		if (got != lr[k] && bad++ < 8)
			printf("block %ld word %d: DSP %06lx, model %06lx (%d voices)\n", blocks, k, got & 0xffffff,
			       lr[k] & 0xffffff, blk[0]);
	}
	blocks++;
}

int main(int argc, char **argv)
{
	FILE *f;
	long n, j;
	const char *why;
	int k;
	if (argc < 2 || !(f = fopen(argv[1], "rb")))
		return 2;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	pool = calloc((size_t)n + 16, 1);
	if (fread(pool, 1, (size_t)n, f) != (size_t)n || !gm_open(&g, pool, n, &why))
		return 2;
	fclose(f);
	gms_prepare(&g, pool);
	pts8 = malloc((size_t)g.points + 16);
	gmsd_points8(&g, pool, pts8);
	gms_init(&syn, &g, RATE, GMSD_VOICES);
	gmsd_dsp_reset(&model);
	sim_start(dsp_gms_prog);
	/* a piece */
	m3(0xc0, 0, 0);
	for (k = 0; k < 4; k++)
		m3(0x90, 48 + 4 * k, 90);
	for (k = 0; k < 16; k++) {
		m3(0x91, 60 + k, 100);
		m3(0x99, k % 2 ? 38 : 36, 110);
		m3(0x99, 42, 80);
		m3(0xe1, 0, 40 + 4 * k);
		m3(0xb2, 10, 8 * k);
		for (j = 0; j < 12; j++)
			one();
		m3(0x81, 60 + k, 0);
	}
	/* many at once: 24 notes on 8 channels */
	for (k = 0; k < 24; k++) {
		m3(0xc0 | (k & 7), k * 5 & 127, 0);
		m3(0x90 | (k & 7), 36 + 2 * k, 100);
	}
	for (j = 0; j < 40; j++)
		one();
	/* random messages */
	{
		unsigned long x = 7;
		for (j = 0; j < 3000; j++) {
			x = x * 1103515245UL + 12345UL;
			m3(0x80 | (int)(x >> 16 & 0x7f), (int)(x >> 8 & 0x7f), (int)(x >> 24 & 0x7f));
			if (j % 10 == 0)
				one();
		}
	}
	printf("%ld blocks, %ld samples differing; the most voices in a block %ld; the longest render %llu cycles "
	       "(%llu voices: %.1f %% of a 32 MHz DSP at %d Hz)\n", blocks, bad, most_v, worst_cyc, worst_v_cyc,
	       100.0 * worst_cyc / (32000000.0 * GMSD_N / RATE), RATE);
	if (sim_done())
		return 1;
	printf(bad ? "dspgms: FAILED\n" : "dspgms: all passed\n");
	return bad != 0;
}
