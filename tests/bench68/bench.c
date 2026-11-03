/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * bench.c - what a 68030 can do of MP3's synthesis window (bench.py runs
 * it in Hatari): 32 outputs of 16 multiply-adds each (one slot of one
 * channel), MULS.W of 16-bit samples by 16-bit window factors, timed by
 * the 200 Hz clock; the share of a second that 44.1 kHz stereo (2756
 * slots a second) would take, and half the rate, mono.
 */
#include "tos.h"

static short v[1024], d[512];
static volatile long sink;

#pragma GCC diagnostic ignored "-Warray-bounds"
static long hz200(void)
{
	return *(volatile long *)0x4baL;
}

/* one slot: 32 x 16 multiply-adds, V strided as the real filter reads it
   (64 words apart), D in order */
static void slot(const short *vp, const short *dp)
{
	long acc;
	int j;
	for (j = 0; j < 32; j++) {
		__asm__ volatile(
		    "moveq #0,%0\n\t"
		    ".rept 16\n\t"
		    "move.w (%1),%%d0\n\t"
		    "muls.w (%2)+,%%d0\n\t"
		    "add.l %%d0,%0\n\t"
		    "lea 128(%1),%1\n\t"
		    ".endr\n\t"
		    : "=&d"(acc), "+a"(vp), "+a"(dp) : : "d0", "cc");
		vp -= 16 * 64 - 1;
		sink += acc;
	}
}

static long run(void)
{
	long t0, t1, n = 0;
	char line[80];
	int i;
	for (i = 0; i < 1024; i++)
		v[i] = (short)(i * 37);
	for (i = 0; i < 512; i++)
		d[i] = (short)(i * 11 - 2000);
	t0 = hz200();
	while (hz200() == t0)
		;
	t0 = hz200();
	do {
		for (i = 0; i < 100; i++)
			slot(v, d);
		n += 100;
		t1 = hz200();
	} while (t1 - t0 < 600);	/* 3 s */
	/* slots a second, and 44.1 kHz stereo's share (2756 a second), x 10 */
	{
		long sps = n * 200 / (t1 - t0);
		long share = 27560L * 100 / sps;	/* % x 10 */
		long len = 0;
		const char *p = "BENCH slots a second: ";
		while (p[len]) line[len] = p[len], len++;
		{
			char num[12];
			int k = 0;
			long x = sps;
			do num[k++] = (char)('0' + x % 10); while (x /= 10);
			while (k) line[len++] = num[--k];
		}
		p = "; 44.1 kHz stereo needs x10 %: ";
		while (*p) line[len++] = *p++;
		{
			char num[12];
			int k = 0;
			long x = share;
			do num[k++] = (char)('0' + x % 10); while (x /= 10);
			while (k) line[len++] = num[--k];
		}
		line[len++] = '\n';
		line[len] = 0;
		nf_debug(line);
	}
	return 0;
}

int main(void)
{
	Supexec(run);
	return 0;
}
