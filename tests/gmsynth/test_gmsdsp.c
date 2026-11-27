/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_gmsdsp.c GM16: the synth on the DSP (src/gmsdsp.c): the blocks the
 * 68030 writes and the DSP's C model on them, against the CPU's engine on
 * the same notes: a held flute note's pitch and level, a short piece's
 * likeness (8-bit points and the control a block at a time: alike, not
 * the same), every block within the DMA's words, and random MIDI bytes
 * with nothing broken (ASan/UBSan).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gmsdsp.h"

#define RATE 24585
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL  " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static GMSET g;
static GMSYN cpu, dsp;
static GMSD_DSP model;
static unsigned char *pool;
static signed char *pts8;
static unsigned short blk[GMSD_WORDS];
static short a[2 * RATE * 4], b[2 * RATE * 4];
static int most_words;

static void both(const unsigned char *m, int n)
{
	gms_midi(&cpu, m, n);
	gms_midi(&dsp, m, n);
}

static void m3(int x, int y, int z)
{
	unsigned char m[3];
	m[0] = (unsigned char)x;
	m[1] = (unsigned char)y;
	m[2] = (unsigned char)z;
	both(m, 3);
}

static void m2(int x, int y)
{
	unsigned char m[2];
	m[0] = (unsigned char)x;
	m[1] = (unsigned char)y;
	both(m, 2);
}

/* n samples (a multiple of GMSD_N) from both: the CPU's and the model's */
static void run(long at, long n)
{
	long k, lr[2 * GMSD_N];
	gms_render(&cpu, a + 2 * at, (int)n);
	for (k = 0; k < n; k += GMSD_N) {
		int i, nw = gmsd_write(&dsp, pool, pts8, blk);
		if (nw > most_words)
			most_words = nw;
		gmsd_dsp(&model, blk, lr);
		for (i = 0; i < 2 * GMSD_N; i++) {
			long x = lr[i] >> 7;	/* (the DSP's half scale, its top 16 bits twice) */
			b[2 * (at + k) + i] = (short)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
		}
	}
}

static double tone(const short *x, int n, double *rms)
{
	/* (the zero crossings' rate, the left channel) */
	int k, c = 0;
	double t = 0;
	for (k = 1; k < n; k++) {
		if (x[2 * (k - 1)] < 0 && x[2 * k] >= 0)
			c++;
		t += (double)x[2 * k] * x[2 * k];
	}
	*rms = sqrt(t / n);
	return (double)c * RATE / n;
}

int main(int argc, char **argv)
{
	FILE *f;
	long n;
	const char *why;
	double fa, fb, ra, rb, dot = 0, aa = 0, bb = 0;
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
	gms_init(&cpu, &g, RATE, GMSD_VOICES);
	gms_init(&dsp, &g, RATE, GMSD_VOICES);
	gmsd_dsp_reset(&model);

	/* a flute's C6 held: the same pitch, the level within 1 dB */
	m2(0xc0, 73);
	m3(0x90, 84, 100);
	run(0, RATE / 64 * 64);
	fa = tone(a + RATE, RATE / 2, &ra);
	fb = tone(b + RATE, RATE / 2, &rb);
	printf("the flute: CPU %.1f Hz rms %.0f, the DSP's model %.1f Hz rms %.0f\n", fa, ra, fb, rb);
	CHECK(fabs(fa - fb) < 2 && fb > 1000, "the flute's pitch: %.1f against %.1f Hz", fb, fa);
	CHECK(fabs(20 * log10(rb / ra)) < 1, "the flute's level: %.1f dB from the CPU's", 20 * log10(rb / ra));
	m3(0x80, 84, 0);

	/* a piece: a piano chord, strings, a melody, the drums */
	gms_reset(&cpu);
	gms_reset(&dsp);
	m2(0xc0, 0);
	m2(0xc1, 48);
	m3(0x90, 48, 90);
	m3(0x90, 55, 90);
	m3(0x90, 64, 90);
	for (k = 0; k < 8; k++) {
		static const int mel[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
		m3(0x91, mel[k], 100);
		m3(0x99, k % 2 ? 38 : 36, 110);
		m3(0x99, 42, 80);
		run((long)k * 6144, 6144);
		m3(0x81, mel[k], 0);
	}
	for (k = 0; k < 2 * 8 * 6144; k++) {
		dot += (double)a[k] * b[k];
		aa += (double)a[k] * a[k];
		bb += (double)b[k] * b[k];
	}
	printf("the piece: the DSP's model against the CPU's, correlation %.3f, level %.1f dB; a block at most %d words of %d\n",
	       dot / sqrt(aa * bb + 1), 10 * log10((bb + 1) / (aa + 1)), most_words, GMSD_WORDS);
	CHECK(dot / sqrt(aa * bb + 1) > 0.9, "the piece: correlation %.3f", dot / sqrt(aa * bb + 1));
	CHECK(most_words <= GMSD_WORDS, "a block of %d words", most_words);

	/* random MIDI bytes: nothing broken */
	{
		unsigned long x = 99;
		long j, lr[2 * GMSD_N];
		for (j = 0; j < 100000; j++) {
			unsigned char m[3];
			x = x * 1103515245UL + 12345UL;
			m[0] = (unsigned char)(0x80 | (x >> 16 & 0x7f));
			m[1] = (unsigned char)(x >> 8 & 0x7f);
			m[2] = (unsigned char)(x >> 24 & 0x7f);
			gms_midi(&dsp, m, 3);
			if (j % 50 == 0) {
				int nw = gmsd_write(&dsp, pool, pts8, blk);
				CHECK(nw <= GMSD_WORDS, "random: %d words", nw);
				gmsd_dsp(&model, blk, lr);
			}
		}
	}
	printf("%s\n", fails ? "gmsdsp: FAILED" : "gmsdsp: all passed");
	return fails != 0;
}
