/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sim.c GM16 - dsp/gms.a56, the internal synth's render on the DSP, on
 * Hatari's DSP core: blocks the 68030's side writes (src/gmsdsp.c's
 * gmsd_write, from a piece played on gmsynth.c's control: a chord, a
 * melody, the drums, bends, pan, volume, then random messages) loaded
 * into its X memory and rendered (RENDER), each sample against
 * gmsd_dsp's, the C model, bit for bit; and the cycles a block takes for
 * its voices. Then the stream (PLAY): the SSI driven a word a slot as
 * Hatari's crossbar does (two stereo tracks, 4 words a frame, at 49,170
 * Hz on a 32 MHz DSP), the blocks played into its receive some words
 * off the frame, as a DMA that started late gives them; every frame it
 * sends (slots 0 and 1) against the model's (gmsd_dsp, gmsd_up), bit for
 * bit, and none late.
 */
#include <string.h>
#include "gms_prog.h"
#include "../dspsim.h"
#include "gmsdsp.h"

#define RATE 24585
#define BLKX 0x2000		/* (RXB, the stream's ring) */
#define NSB 60			/* the stream's blocks */

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
	load(1, BLKX, w24, nw);
	put(7);
	put(BLKX);
	t = sim_job();
	if (t > worst_cyc) {
		worst_cyc = t;
		worst_v_cyc = blk[1];
	}
	if (blk[1] > most_v)
		most_v = blk[1];
	for (k = 0; k < 2 * GMSD_N; k++) {
		long got = get();
		if (got != lr[k] && bad++ < 8)
			printf("block %ld word %d: DSP %06lx, model %06lx (%d voices)\n", blocks, k, got & 0xffffff,
			       lr[k] & 0xffffff, blk[1]);
	}
	blocks++;
}

/* the stream: NSB blocks of a piece as the DMA plays them, off by off
   words; the frames sent against the model's */
static unsigned short sblk[NSB][GMSD_WORDS];
static long sout[NSB * GMSD_FRAMES * 2];
static long stx[(NSB + 12) * GMSD_FRAMES * 2 + 8192];

static int stream(int off)
{
	long nw = 8192L + off + (long)NSB * GMSD_WORDS + 6L * GMSD_WORDS, i, nf = 0, first = -1, j;
	long prev[2] = { 0, 0 }, lr[2 * GMSD_N], f;
	double at = 0, per = (getenv("SIMMHZ") ? atof(getenv("SIMMHZ")) * 1e6 : 32000000.0) / (49170.0 * 4);
	int k, sbad = 0;
	unsigned long nb, late, skip;
	gms_init(&syn, &g, RATE, GMSD_VOICES);
	gmsd_dsp_reset(&model);
	m3(0xc0, 0, 0);
	for (k = 0; k < 4; k++)
		m3(0x90, 48 + 4 * k, 100);
	for (k = 0; k < NSB; k++) {
		if (k == 20)
			m3(0x90, 72, 120);
		if (k == 40)
			m3(0x80, 48, 0);
		memset(sblk[k], 0, sizeof(sblk[k]));
		gmsd_write(&syn, pool, pts8, sblk[k]);
		gmsd_dsp(&model, sblk[k], lr);
		gmsd_up(prev, lr, sout + (long)k * GMSD_FRAMES * 2);
	}
	put(8);
	at = (double)cycles;	/* (the slots' clock from now: the cycles count on from the tests before) */
	profiling = getenv("SIMPROF_STREAM") != NULL;
	for (i = 0; i < nw; i++) {
		long b = i - 8192 - off;
		unsigned long w = b >= 0 && b < (long)NSB * GMSD_WORDS ? sblk[b / GMSD_WORDS][b % GMSD_WORDS] : 0;
		int slot = (int)(i & 3);
		at += per;
		while (cycles < (unsigned long long)at) {
			if (!step())
				break;
		}
		/* the transmit's slot, then the receive's, as the crossbar */
		dsp_core_ssi_Receive_SC2(slot == 0);
		dsp_core_ssi_Receive_SCK();
		if (slot < 2 && nf < (long)(sizeof(stx) / sizeof(stx[0])))
			stx[nf++] = (long)(dsp_core.ssi.transmit_value & 0xffff);
		dsp_core.ssi.received_value = w;
		dsp_core_ssi_Receive_SC1(slot == 0);
		dsp_core_ssi_Receive_SC0();
	}
	profiling = 0;
	put(9);
	nb = (unsigned long)get();
	late = (unsigned long)get();
	skip = (unsigned long)get();
	for (k = 0; k < 13; k++)
		(void)get();	/* (the receive's place; the first block's first words) */
	/* the first frame the model's first matches, then all of them */
	for (f = 0; f + 2L * 256 < nf && first < 0; f += 2) {
		for (j = 0; j < 2L * 256; j++)
			if (stx[f + j] != ((sout[j] >> 8) & 0xffff))
				break;
		if (j == 2L * 256)
			first = f;
	}
	if (first < 0) {
		printf("stream (%d words off): the model's frames never found in what the DSP sent (%lu blocks, %lu late, "
		       "%lu passed over)\n", off, nb, late, skip);
		return 1;
	}
	for (j = 0; j < (long)NSB * GMSD_FRAMES * 2; j++)
		if (first + j >= nf || stx[first + j] != ((sout[j] >> 8) & 0xffff)) {
			if (sbad++ < 6)
				printf("stream (%d off): frame %ld %s: DSP %04lx, model %04lx\n", off, j / 2, j & 1 ? "R" : "L",
				       first + j < nf ? stx[first + j] : -1L, (sout[j] >> 8) & 0xffff);
		}
	printf("stream, %d words off: %lu blocks rendered, %lu late, %lu words passed over; the first frame %ld; "
	       "%d frames differing\n", off, nb, late, skip, first / 2, sbad);
	return sbad || late || nb < NSB;
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
	load(6, DSP_GMS_PROG_EXT_AT, dsp_gms_prog_ext, DSP_GMS_PROG_EXT_N);
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
	{
		int sb = stream(0) | stream(3) | stream(1021);
		if (sim_done())
			return 1;
		if (sb)
			bad++;
	}
	printf(bad ? "dspgms: FAILED\n" : "dspgms: all passed\n");
	return bad != 0;
}
