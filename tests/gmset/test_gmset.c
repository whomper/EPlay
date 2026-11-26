/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_gmset.c GM8 GM16: the synth's sound set as tools/mkgmset.py makes
 * it (8-bit and 16-bit samples), read by src/gmset.c: its counts, a region
 * and an articulation against Sonivox's own table, every General MIDI
 * program and drum found, the two sample depths alike, and damaged files
 * refused.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gmset.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL  " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static unsigned char *load(const char *path, long *n)
{
	FILE *f = fopen(path, "rb");
	unsigned char *b;
	if (!f) {
		perror(path);
		exit(2);
	}
	fseek(f, 0, SEEK_END);
	*n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc(*n);
	if (fread(b, 1, *n, f) != (size_t)*n)
		exit(2);
	fclose(f);
	return b;
}

static void check_set(const char *path, int bits, GMSET *g, unsigned char **keep, long *keep_n)
{
	long n;
	unsigned char *b = load(path, &n);
	const char *why;
	GM_REGION r;
	GM_ART a;
	int k, idx, missing = 0;
	CHECK(gm_open(g, b, n, &why), "%s: refused (%s)", path, why);
	CHECK(g->bits == bits, "%s: %d-bit, wanted %d", path, g->bits, bits);
	CHECK(g->rate == 22050 && g->nbank == 1 && g->nprog == 1 && g->nreg == 377 && g->nart == 185 && g->nwave == 150 &&
	      g->points == 212177, "%s: counts %ld Hz %d %d %d %d %d %ld", path, g->rate, g->nbank, g->nprog, g->nreg, g->nart,
	      g->nwave, g->points);
	/* region 0 and articulation 0, as wt_22khz.c has them */
	gm_region(g, 0, &r);
	CHECK(r.flags == 0 && r.lo == 27 && r.hi == 27 && r.tuning == -2868 && r.gain == 16422 && r.loop_start == 0 &&
	      r.loop_end == 0 && r.wave == 81 && r.art == 0, "%s: region 0", path);
	gm_region(g, 376, &r);
	CHECK(r.flags == 32769 && r.lo == 88 && r.hi == 108 && r.tuning == -8473 && r.loop_start == 404 &&
	      r.loop_end == 16698 && r.wave == 1 && r.art == 184, "%s: region 376", path);
	gm_art(g, 0, &a);
	CHECK(a.eg1[0] == 32767 && a.eg1[1] == 30725 && a.eg1[2] == 0 && a.eg1[3] == 30725 && a.eg2[0] == 32767 &&
	      a.eg2[3] == 0 && a.lfo_freq == 951, "%s: articulation 0", path);
	/* every program plays middle C, the kit its 47 GM drums (35-81) */
	for (k = 0; k < 128; k++)
		if (!gm_find(g, 0, k, 60, &r, &idx)) {
			printf("      program %d: no region for key 60\n", k);
			missing++;
		}
	for (k = 35; k <= 81; k++)
		if (!gm_find(g, 1, 0, k, &r, &idx)) {
			printf("      drum %d: no region\n", k);
			missing++;
		}
	CHECK(missing == 0, "%s: %d programs or drums with no region", path, missing);
	CHECK(!gm_find(g, 0, 128, 60, &r, &idx) && !gm_find(g, 0, 0, 128, &r, &idx), "%s: out of range found", path);
	*keep = b;
	*keep_n = n;
}

int main(int argc, char **argv)
{
	GMSET g8, g16, t;
	unsigned char *b8, *b16, *bad;
	long n8, n16, len8, len16, i, worst = 0;
	const char *why;
	int w;
	if (argc != 3) {
		fprintf(stderr, "test_gmset GM8 GM16\n");
		return 2;
	}
	check_set(argv[1], 8, &g8, &b8, &n8);
	check_set(argv[2], 16, &g16, &b16, &n16);
	/* the two depths: the same waves, the 16-bit point's top byte within
	   one step of the 8-bit one */
	for (w = 0; w < g8.nwave; w++) {
		const unsigned char *p8 = gm_wave(&g8, w, &len8), *p16 = gm_wave(&g16, w, &len16);
		CHECK(len8 == len16, "wave %d: lengths %ld %ld", w, len8, len16);
		for (i = 0; i < len8; i++) {
			long d = (long)(signed char)p8[i] - ((short)(p16[2 * i] << 8 | p16[2 * i + 1]) >> 8);
			if (d < 0)
				d = -d;
			if (d > worst)
				worst = d;
		}
	}
	CHECK(worst <= 1, "8-bit and 16-bit points apart by %ld steps", worst);
	/* damaged: cut short, a wrong magic, a region pointing past the waves */
	/* (the file ends on a multiple of 4: a byte less than the pool's end) */
	CHECK(!gm_open(&t, b8, g8.o_smp + g8.points - 1, &why), "cut short: accepted");
	CHECK(!gm_open(&t, b16, g16.o_smp + 2 * g16.points - 1, &why), "16-bit cut short: accepted");
	bad = malloc(n8);
	memcpy(bad, b8, n8);
	bad[0] = 'X';
	CHECK(!gm_open(&t, bad, n8, &why), "wrong magic: accepted");
	memcpy(bad, b8, n8);
	bad[g8.o_reg + 16] = 0xff;	/* region 0's wave */
	CHECK(!gm_open(&t, bad, n8, &why), "a region's wave out of range: accepted");
	memcpy(bad, b8, n8);
	bad[g8.o_bank + 2] = 0x3f;	/* program 0's region */
	CHECK(!gm_open(&t, bad, n8, &why), "a bank's region out of range: accepted");
	for (i = 0; i < 32; i++)
		CHECK(!gm_open(&t, b8, i, &why), "%ld bytes: accepted", i);
	free(bad);
	free(b8);
	free(b16);
	printf("%s\n", fails ? "gmset: FAILED" : "gmset: all passed");
	return fails != 0;
}
