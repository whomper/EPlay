/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pcmtest.c - for pcm_test.py: src/pcm.c's 68030 loops (the tick's: 16-bit
 * WAV, 24-bit AIFF and WAV, stereo) against its C, on the 68030 itself
 * (Hatari), bit for bit: random samples, and the edges (32767 with its
 * lowest byte 128 or more, -32768, 0), at the ring's strides (2: the DSP's
 * packet; 8: the DMA's four tracks). The answer goes to the console
 * (NatFeats) as "PCMTEST ok ..." or "PCMTEST FAIL ...". And pcm_spread
 * (XSTREAM's: the file's frames over the DSP's rate): the 68030's against
 * the C, at the rates the DSP resamples from, the source running out, the
 * phase carried from call to call.
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "pcm.h"

/* src/pcm.c built with its C alone, its names prefixed (pcmtest_c.c) */
void c_pcm_convert_to(const PCMFMT *f, const unsigned char *src, short *dst, long n, int step);
long c_pcm_spread(const short *src, long n, short *dst, long k, int step, long in, long out, long *acc,
		  long *used);

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
	{
		static const long rates[][2] = { { 44100, 49170 }, { 48000, 49170 }, { 22050, 24585 },
						 { 32000, 32780 } };
		static short src2[400 * 2];
		long r;
		for (r = 0; r < 4; r++) {
			long acc1 = 0, acc2 = 0, j, sum = 0;
			for (j = 0; j < 400 * 2; j++)
				src2[j] = (short)(rnd() << 8 | rnd());
			for (j = 0; j < 12; j++) {
				long k = 37 + j * 41, n = j % 3 ? 400 : 20 + j, u1, u2, w1, w2;
				memset(a, 0x55, sizeof(a));
				memset(b, 0x55, sizeof(b));
				w1 = pcm_spread(src2, n, a, k, j & 1 ? 8 : 2, rates[r][0], rates[r][1], &acc1, &u1);
				w2 = c_pcm_spread(src2, n, b, k, j & 1 ? 8 : 2, rates[r][0], rates[r][1], &acc2, &u2);
				runs++;
				sum += u1;
				if (w1 != w2 || u1 != u2 || acc1 != acc2 || memcmp(a, b, sizeof(a))) {
					snprintf(line, sizeof(line),
						 "PCMTEST FAIL spread %ld from %ld, call %ld: wrote %ld (C %ld), used %ld "
						 "(C %ld), phase %ld (C %ld)\n", rates[r][1], rates[r][0], j, w1, w2, u1,
						 u2, acc1, acc2);
					nf_debug(line);
					bad++;
					break;
				}
			}
			(void)sum;
		}
	}
	snprintf(line, sizeof(line), bad ? "PCMTEST FAIL %d of %d\n" : "PCMTEST ok %d runs of %d frames\n",
		 bad ? bad : runs, bad ? runs : N);
	nf_debug(line);
	return 0;
}
