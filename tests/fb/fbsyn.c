/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fbsyn.c - EPLAYFB.PRG's internal synth (src/gmsynth.c over SYNTH.DAT's
 * set) on the FireBee's ColdFire, run under qemu-m68k -cpu cfv4e as a
 * Linux program (common/tests/coldfire's runtime), or on the host (HOST):
 * a fixed piece (a chord on the piano, a melody on the strings with the
 * bend and the sustain pedal, the drums, pan and volume) rendered at
 * 49,170 Hz, the samples written as 16-bit big-endian words to stdout.
 * The engine is integer arithmetic, so the ColdFire's must be the host's,
 * bit for bit.
 *
 *	fbsyn GM16.DAT > OUT.RAW
 */
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include "rt.h"
#endif
#include "gmsynth.h"

static unsigned char set[460000];
static GMSET g;
static GMSYN s;
static short lr[2 * 1024];
static unsigned char outb[4 * 1024];

#ifdef HOST
static long read_file(const char *path, unsigned char *b, long cap)
{
	FILE *f = fopen(path, "rb");
	long n;
	if (!f)
		return -1;
	n = (long)fread(b, 1, (size_t)cap, f);
	fclose(f);
	return n;
}
#define put(p, n) fwrite((p), 1, (size_t)(n), stdout)
#else
#define put(p, n) sys_write(1, (p), (n))
#endif

static void m3(int a, int b, int c)
{
	unsigned char m[3];
	m[0] = (unsigned char)a;
	m[1] = (unsigned char)b;
	m[2] = (unsigned char)c;
	gms_midi(&s, m, 3);
}

static void m2(int a, int b)
{
	unsigned char m[2];
	m[0] = (unsigned char)a;
	m[1] = (unsigned char)b;
	gms_midi(&s, m, 2);
}

static void play(long frames)
{
	while (frames > 0) {
		int n = frames > 1024 ? 1024 : (int)frames, k;
		gms_render(&s, lr, n);
		for (k = 0; k < 2 * n; k++) {
			outb[2 * k] = (unsigned char)((unsigned short)lr[k] >> 8);
			outb[2 * k + 1] = (unsigned char)lr[k];
		}
		put(outb, 4 * n);
		frames -= n;
	}
}

int main(int argc, char **argv)
{
	static const int mel[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
	const char *why;
	long n;
	int k;
	if (argc < 2 || (n = read_file(argv[1], set, sizeof(set) - 16)) <= 0)
		return 2;
	if (!gm_open(&g, set, n, &why))
		return 3;
	gms_prepare(&g, set);
	gms_init(&s, &g, 49170, GMS_VOICES);
	m2(0xc0, 0);
	m2(0xc1, 48);
	m3(0xb1, 10, 20);
	m3(0xb0, 10, 100);
	m3(0x90, 48, 90);
	m3(0x90, 55, 90);
	m3(0x90, 64, 90);
	m3(0xb1, 64, 127);
	for (k = 0; k < 8; k++) {
		m3(0x91, mel[k], 100);
		m3(0x99, k % 2 ? 38 : 36, 110);
		m3(0x99, 42, 80);
		m3(0xe1, 0, 64 + 8 * (k & 3));
		play(49170 / 4);
		m3(0x81, mel[k], 0);
	}
	m3(0xb1, 64, 0);
	m3(0xb0, 7, 60);
	m3(0x80, 48, 0);
	m3(0x80, 55, 0);
	m3(0x80, 64, 0);
	play(49170);
	return 0;
}
