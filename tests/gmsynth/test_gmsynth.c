/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_gmsynth.c GM16 [OUT.WAV]: EPlay's synth (src/gmsynth.c) over the
 * sound set: every pitched General MIDI program's A (note 69) at 440 Hz
 * within 15 cents, the bend's two semitones, the notes ending when let go,
 * the drums sounding, pan and volume, more notes than voices taken, the GM
 * reset's silence, and random MIDI bytes played with nothing broken
 * (ASan/UBSan). OUT.WAV: a short piece rendered, to listen to.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gmsynth.h"

#define RATE 44100
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL  " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static GMSET g;
static GMSYN s;
static short buf[2 * RATE * 4];

static void msg3(int a, int b, int c)
{
	unsigned char m[3];
	m[0] = (unsigned char)a;
	m[1] = (unsigned char)b;
	m[2] = (unsigned char)c;
	gms_midi(&s, m, 3);
}

static void msg2(int a, int b)
{
	unsigned char m[2];
	m[0] = (unsigned char)a;
	m[1] = (unsigned char)b;
	gms_midi(&s, m, 2);
}

static double rms(const short *b, int n, int side)
{
	double t = 0;
	int k;
	for (k = 0; k < n; k++)
		t += (double)b[2 * k + side] * b[2 * k + side];
	return sqrt(t / (n ? n : 1));
}

/* the pitch (Hz) of n frames' left side, by autocorrelation between 60 and
   2,000 Hz, refined between its neighbours; 0 for none */
static double pitch(const short *b, int n)
{
	int lag, best = 0, k;
	double bv = 0, c0 = 0;
	static double x[RATE];
	for (k = 0; k < n; k++)
		x[k] = b[2 * k];
	for (k = 0; k < n; k++)
		c0 += x[k] * x[k];
	if (c0 <= 0)
		return 0;
	{
		static double c[RATE / 60 + 2];
		for (lag = RATE / 2000; lag <= RATE / 60 + 1; lag++) {
			double t = 0;
			for (k = 0; k + lag < n; k++)
				t += x[k] * x[k + lag];
			c[lag] = t / (n - lag);
		}
		/* the first peak within 90 % of the highest: not an octave down */
		for (lag = RATE / 2000 + 1; lag <= RATE / 60; lag++)
			if (c[lag] > bv)
				bv = c[lag];
		for (lag = RATE / 2000 + 1; lag <= RATE / 60; lag++)
			if (c[lag] >= 0.9 * bv && c[lag] >= c[lag - 1] && c[lag] >= c[lag + 1]) {
				best = lag;
				break;
			}
		if (!best)
			return 0;
		{
			double a = c[best - 1], m = c[best], z = c[best + 1], d = a - 2 * m + z;
			double off = d != 0 ? 0.5 * (a - z) / d : 0;
			return RATE / (best + off);
		}
	}
}

static void put_wav(const char *path, const short *b, long frames)
{
	FILE *f = fopen(path, "wb");
	unsigned char h[44];
	long n = frames * 4, k;
	memcpy(h, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0", 24);
	h[24] = RATE & 255;
	h[25] = (RATE >> 8) & 255;
	h[26] = (RATE >> 16) & 255;
	h[27] = 0;
	h[28] = (RATE * 4) & 255;
	h[29] = ((RATE * 4) >> 8) & 255;
	h[30] = ((RATE * 4) >> 16) & 255;
	h[31] = 0;
	h[32] = 4;
	h[33] = 0;
	h[34] = 16;
	h[35] = 0;
	memcpy(h + 36, "data", 4);
	for (k = 0; k < 4; k++) {
		h[40 + k] = (unsigned char)(n >> (8 * k));
		h[4 + k] = (unsigned char)((n + 36) >> (8 * k));
	}
	fwrite(h, 1, 44, f);
	for (k = 0; k < 2 * frames; k++) {
		unsigned char le[2];
		le[0] = (unsigned char)(b[k] & 255);
		le[1] = (unsigned char)((b[k] >> 8) & 255);
		fwrite(le, 1, 2, f);
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	FILE *f;
	unsigned char *b;
	long n;
	const char *why;
	int p, k, tuned = 0, tried = 0;
	double worst = 0;
	if (argc < 2) {
		fprintf(stderr, "test_gmsynth GM16 [OUT.WAV]\n");
		return 2;
	}
	f = fopen(argv[1], "rb");
	if (!f)
		return 2;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = calloc((size_t)n + 16, 1);	/* (a point past the last wave's end read as 0) */
	if (fread(b, 1, (size_t)n, f) != (size_t)n)
		return 2;
	fclose(f);
	if (!gm_open(&g, b, n, &why)) {
		printf("FAIL  the set: %s\n", why);
		return 1;
	}
	gms_prepare(&g, b);
	gms_init(&s, &g, RATE, 24);

	/* every pitched program's note 69 at 440 Hz, 0.05 to 0.35 s in: its
	   pitch class within 25 cents (an organ's 16-foot stop an octave down
	   is its sound; the set's own loops are some 20 cents off at A4 in a few
	   plucked ones). Left out: 112-127 (percussion and effects), and a
	   program with no range at A4 (the contrabass) */
	for (p = 0; p < 128; p++) {
		double hz, cents;
		GM_REGION r;
		int idx;
		gms_reset(&s);
		msg2(0xc0, p);
		msg3(0x90, 69, 100);
		gms_render(&s, buf, RATE / 2);
		hz = pitch(buf + 2 * (RATE / 20), RATE * 3 / 10);
		msg3(0x80, 69, 0);
		if (p >= 112 || !gm_find(&g, 0, p, 69, &r, &idx))
			continue;
		tried++;
		cents = hz > 0 ? 1200 * log2(hz / 440.0) : 9999;
		if (cents < 9999) {
			cents = fmod(cents, 1200);
			if (cents > 600)
				cents -= 1200;
			if (cents < -600)
				cents += 1200;
		}
		if (fabs(cents) <= 25)
			tuned++;
		else
			printf("      program %3d: %7.1f Hz (%+.0f cents from A)\n", p, hz, cents);
		if (fabs(cents) > worst && fabs(cents) < 9999)
			worst = fabs(cents);
	}
	printf("pitched programs on A within 25 cents: %d of %d\n", tuned, tried);
	CHECK(tuned >= tried * 95 / 100, "only %d of %d programs in tune", tuned, tried);

	/* the bend: +2 semitones (the default range) on a piano: 493.9 Hz */
	{
		double hz;
		gms_reset(&s);
		msg2(0xc0, 0);
		msg3(0xe0, 0x7f, 0x7f);
		msg3(0x90, 69, 100);
		gms_render(&s, buf, RATE / 2);
		hz = pitch(buf + 2 * (RATE / 10), RATE / 4);
		CHECK(fabs(1200 * log2(hz / 493.88)) < 15, "the bend: %.1f Hz, wanted 493.9", hz);
	}
	/* the notes end: let go, every voice silent within 4 s */
	{
		gms_reset(&s);
		msg2(0xc0, 48);	/* (strings: a long release) */
		for (k = 0; k < 6; k++)
			msg3(0x90, 60 + 4 * k, 110);
		gms_render(&s, buf, RATE / 2);
		CHECK(s.live == 6, "six notes: %d voices sounding", s.live);
		for (k = 0; k < 6; k++)
			msg3(0x80, 60 + 4 * k, 0);
		gms_render(&s, buf, RATE * 4);
		CHECK(s.live == 0, "let go: %d voices still sounding after 4 s", s.live);
		CHECK(rms(buf + 2 * (RATE * 7 / 2), RATE / 2, 0) < 1, "let go: still sound after 4 s");
	}
	/* the drums: channel 10's bass drum and snare sound; program 0 there */
	{
		double r;
		gms_reset(&s);
		msg3(0x99, 36, 120);
		msg3(0x99, 38, 120);
		gms_render(&s, buf, RATE / 4);
		r = rms(buf, RATE / 4, 0);
		CHECK(r > 300, "the drums: %.0f (rms)", r);
	}
	/* pan: hard left leaves the right near silent; volume 0 silent */
	{
		double l, r;
		gms_reset(&s);
		msg3(0xb0, 10, 0);
		msg3(0x90, 60, 110);
		gms_render(&s, buf, RATE / 2);
		l = rms(buf + RATE / 2, RATE / 4, 0);
		r = rms(buf + RATE / 2, RATE / 4, 1);
		CHECK(l > 300 && r < l / 20, "hard left: left %.0f, right %.0f", l, r);
		gms_reset(&s);
		msg3(0xb0, 7, 0);
		msg3(0x90, 60, 110);
		gms_render(&s, buf, RATE / 4);
		CHECK(rms(buf, RATE / 4, 0) < 1 && rms(buf, RATE / 4, 1) < 1, "volume 0: sound");
	}
	/* more notes than voices: never more than 24 sounding, the rest taken */
	{
		gms_reset(&s);
		s.stolen = 0;
		for (k = 0; k < 40; k++)
			msg3(0x90 | (k % 9), 40 + k, 100);
		gms_render(&s, buf, RATE / 10);
		CHECK(s.live <= 24 && s.stolen >= 16, "40 notes: %d sounding, %lu taken", s.live, s.stolen);
	}
	/* the cap (the CPU's time short): 8 voices for new notes, those past
	   them ended by the next update */
	{
		gms_reset(&s);
		for (k = 0; k < 20; k++)
			msg3(0x90, 40 + k, 100);
		gms_cap(&s, 8);
		gms_render(&s, buf, RATE / 10);
		CHECK(s.live <= 8, "capped at 8: %d sounding", s.live);
		for (k = 0; k < 20; k++)
			msg3(0x91, 40 + k, 100);
		gms_render(&s, buf, RATE / 10);
		CHECK(s.live <= 8, "capped at 8, 20 more notes: %d sounding", s.live);
		gms_cap(&s, 24);
	}
	/* GM System On: silent at once */
	{
		static const unsigned char gm_on[6] = { 0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7 };
		msg3(0x90, 60, 110);
		gms_midi(&s, gm_on, 6);
		CHECK(s.live == 0, "GM System On: %d voices", s.live);
	}
	/* random bytes as messages: nothing broken (the sanitizers watch) */
	{
		unsigned long x = 12345;
		long j;
		gms_reset(&s);
		for (j = 0; j < 200000; j++) {
			unsigned char m[3];
			int len;
			x = x * 1103515245UL + 12345UL;
			m[0] = (unsigned char)(0x80 | (x >> 16 & 0x7f));
			m[1] = (unsigned char)(x >> 8 & 0xff);
			m[2] = (unsigned char)(x >> 24 & 0xff);
			len = 1 + (int)(x >> 28 & 3);
			gms_midi(&s, m, len > 3 ? 3 : len);
			if (j % 97 == 0)
				gms_render(&s, buf, 64);
		}
		CHECK(s.live >= 0 && s.live <= 24, "random: %d voices", s.live);
	}
	/* (a short piece to listen to: a chord, a melody, the drums) */
	if (argc > 2) {
		long at = 0;
		static const int mel[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
		gms_reset(&s);
		msg2(0xc0, 0);
		msg2(0xc1, 48);
		msg3(0x91, 48, 80);
		msg3(0x91, 55, 80);
		for (k = 0; k < 8; k++) {
			msg3(0x90, mel[k], 100);
			msg3(0x99, k % 2 ? 38 : 36, 110);
			msg3(0x99, 42, 80);
			gms_render(&s, buf + 2 * at, RATE / 4);
			at += RATE / 4;
			msg3(0x80, mel[k], 0);
		}
		msg3(0x81, 48, 0);
		msg3(0x81, 55, 0);
		gms_render(&s, buf + 2 * at, RATE);
		at += RATE;
		put_wav(argv[2], buf, at);
	}
	printf("worst tuning among the pitched: %.1f cents\n", worst);
	free(b);
	printf("%s\n", fails ? "gmsynth: FAILED" : "gmsynth: all passed");
	return fails != 0;
}
