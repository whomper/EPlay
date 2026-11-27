/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * ringcpy.c - RINGCPY.PRG: what the Falcon's DSP synth (DESIGN.md, "The
 * synth on the DSP: Cubase's route") asks of the 68030, timed: a second of
 * music at 49,170 Hz in blocks of 128 frames, each block's points for V
 * voices (8-bit, a voice's stretch as its pitch takes it: 128 x 22,050 /
 * 49,170 x a pitch ratio, from 0.5 to 2) copied from the set (in ST RAM, a
 * stock Falcon's only RAM) into a ring in ST RAM, with a header of four
 * words a voice; a stretch that crosses its loop's end copied in two. The
 * ring's other words (the DMA's frame has 8 a frame) aren't written. A
 * measurement, not a test.
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"

long nf_on = 1;

#define RATE 49170L
#define BLK 128
#define RING (RATE / 2 * 16)	/* (half a second of the DMA's 16 bytes a frame) */

static unsigned char set[212180];

/* (a long at a time, the 68030's own: it reads a long at any address) */
static void cpy(unsigned char *d, const unsigned char *s, long n)
{
	unsigned long *dl = (unsigned long *)(void *)d;
	const unsigned long *sl = (const unsigned long *)(const void *)s;
	long k = n >> 4;
	while (k-- > 0) {
		dl[0] = sl[0];
		dl[1] = sl[1];
		dl[2] = sl[2];
		dl[3] = sl[3];
		dl += 4;
		sl += 4;
	}
	d = (unsigned char *)dl;
	s = (const unsigned char *)sl;
	n &= 15;
	while (n-- > 0)
		*d++ = *s++;
}

static long run(int voices)
{
	unsigned char *ring = (unsigned char *)Mxalloc(RING + 64, 0), *w;
	unsigned long t0, seed = 1;
	long pos[32], blk, wp = 0;
	int v;
	if ((long)ring <= 0)
		return -1;
	for (v = 0; v < voices; v++)
		pos[v] = (long)v * 6000;
	t0 = tos_ms();
	for (blk = 0; blk < RATE / BLK; blk++) {
		for (v = 0; v < voices; v++) {
			long n, cut;
			seed = seed * 1103515245UL + 12345UL;
			/* (the points this block: 57 x 0.5 to 2) */
			n = 29 + (long)((seed >> 16) % 87);
			if (wp + 8 + n + 4 > RING)
				wp = 0;
			w = ring + wp;
			((unsigned short *)w)[0] = (unsigned short)v;
			((unsigned short *)w)[1] = (unsigned short)n;
			((unsigned short *)w)[2] = (unsigned short)(seed >> 8);
			((unsigned short *)w)[3] = (unsigned short)(seed >> 20);
			w += 8;
			cut = (seed >> 24) & 7 ? n : n / 2;	/* (one in eight crosses its loop's end) */
			cpy(w, set + pos[v], cut);
			if (cut < n)
				cpy(w + cut, set + pos[v] / 2, n - cut);
			wp += 8 + ((n + 1) & ~1L);
			pos[v] += n;
			if (pos[v] > 200000)
				pos[v] = 0;
		}
	}
	t0 = tos_ms() - t0;
	Mfree(ring);
	return (long)t0;
}

int main(void)
{
	char line[200];
	long a = run(8), b = run(16), c = run(24), d = run(32);
	snprintf(line, sizeof(line), "RINGCPY a second at %ld Hz, 128-frame blocks: 8 voices %ld ms, 16 voices %ld ms, "
		 "24 voices %ld ms, 32 voices %ld ms\n", RATE, a, b, c, d);
	nf_debug(line);
	return 0;
}
