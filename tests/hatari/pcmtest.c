/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pcmtest.c - for pcm_test.py: src/pcm.c's 68030 loops (the tick's: 16-bit
 * WAV, 24-bit AIFF and WAV, stereo) against its C, on the 68030 itself
 * (Hatari), bit for bit: random samples, and the edges (32767 with its
 * lowest byte 128 or more, -32768, 0), at the ring's strides (2: the DSP's
 * packet; 8: the DMA's four tracks). The answer goes to the console
 * (NatFeats) as "PCMTEST ok ..." or "PCMTEST FAIL ...".
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "pcm.h"

/* src/pcm.c built with its C alone, its names prefixed (pcmtest_c.c) */
void c_pcm_convert_to(const PCMFMT *f, const unsigned char *src, short *dst, long n, int step);

#define N 1000
static unsigned char src[N * 6 + 6];
static short a[N * 8 + 8], b[N * 8 + 8];
static unsigned long seed = 12345;

static unsigned rnd(void)
{
	seed = seed * 1103515245UL + 12345UL;
	return (unsigned)(seed >> 16) & 0xff;
}

int main(void)
{
	static const struct { int bits, big; } fm[] = { { 16, 0 }, { 24, 1 }, { 24, 0 } };
	static const int steps[] = { 2, 8 };
	char line[120];
	int i, k, s, bad = 0, runs = 0;
	for (k = 0; k < 3; k++)
		for (s = 0; s < 2; s++) {
			PCMFMT f;
			int bytes = fm[k].bits / 8 * 2;
			long j;
			memset(&f, 0, sizeof(f));
			f.bits = fm[k].bits;
			f.big = fm[k].big;
			f.channels = 2;
			f.bytes = bytes;
			for (j = 0; j < (long)N * bytes; j++)
				src[j] = (unsigned char)rnd();
			/* the edges in the first frames: top + lowest byte's top bit
			   (rounds to 32767, not over), the bottom, zero, -1 + 0x80 */
			if (f.bits == 24) {
				static const unsigned char e[4][3] = {
					{ 0x7f, 0xff, 0x80 }, { 0x80, 0x00, 0x00 }, { 0x00, 0x00, 0x00 }, { 0xff, 0xff, 0xc0 } };
				for (i = 0; i < 8; i++) {
					const unsigned char *v = e[i % 4];
					unsigned char *p = src + i * 3;
					p[0] = f.big ? v[0] : v[2];
					p[1] = v[1];
					p[2] = f.big ? v[2] : v[0];
				}
			}
			memset(a, 0x55, sizeof(a));
			memset(b, 0x55, sizeof(b));
			pcm_convert_to(&f, src, a, N, steps[s]);
			c_pcm_convert_to(&f, src, b, N, steps[s]);
			runs++;
			if (memcmp(a, b, sizeof(a))) {
				for (i = 0; i < (int)(sizeof(a) / 2) && a[i] == b[i]; i++)
					;
				snprintf(line, sizeof(line), "PCMTEST FAIL %d-bit %s, step %d: word %d %d (C %d)\n", f.bits,
					 f.big ? "AIFF" : "WAV", steps[s], i, a[i], b[i]);
				nf_debug(line);
				bad++;
			}
		}
	snprintf(line, sizeof(line), bad ? "PCMTEST FAIL %d of %d\n" : "PCMTEST ok %d runs of %d frames\n",
		 bad ? bad : runs, bad ? runs : N);
	nf_debug(line);
	return 0;
}
