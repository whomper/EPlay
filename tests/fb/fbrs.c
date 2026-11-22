/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fbrs.c - the resampler a sample at a time (src/mp3/mp3dsp.c's mp3rss,
 * the FireBee's cmp_put) against the block one (mp3rs_run, the DSP's C
 * model), and on the ColdFire under qemu-m68k -cpu cfv4e, its sums on the
 * EMAC where EPLAY_EMAC builds them, against the host. For each pair of
 * rates: a test signal (tones and noise, 16 bits, stereo) in pieces of
 * odd sizes, its outputs written as 16-bit big-endian to stdout; on the
 * host, each also held to mp3rs_run's, bit for bit (HOST).
 *
 *	fbrs > OUT.RAW
 */
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define put(p, n) fwrite((p), 1, (size_t)(n), stdout)
#else
#include "rt.h"
#define put(p, n) sys_write(1, (p), (n))
#endif
#include "mp3dsp.h"

#define NS (1152 * 24)
static short in[NS * 2], res[(NS * 7) * 2], piece[4096 * 2];
static unsigned char ob[4096 * 2 * 2];
#ifdef HOST
static dw blk[2][1152], bout[2][MP3RS_MAX];
static short ref[(NS * 7) * 2];
#endif

static const long rates[][2] = {
	{ 44100, 49170 }, { 48000, 49170 }, { 32000, 49170 }, { 22050, 49170 }, { 11025, 49170 },
	{ 8000, 49170 }, { 48000, 44100 }, { 44100, 32780 }, { 48000, 32780 },
};

static unsigned long seed = 12345;
static long rnd(void)
{
	seed = seed * 1103515245UL + 12345UL;
	return (long)((seed >> 8) & 0xffff) - 32768;
}

int main(void)
{
	int t, k, bad = 0;
	for (k = 0; k < NS; k++) {
		/* (two tones and some noise, near full scale, its peaks clipped) */
		long a = ((k * 37) % 400 - 200) * 120 + rnd() / 8, b = ((k * 53) % 300 - 150) * 160 + rnd() / 8;
		in[2 * k] = (short)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
		in[2 * k + 1] = (short)(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
	}
	for (t = 0; t < (int)(sizeof(rates) / sizeof(rates[0])); t++) {
		MP3RSS r;
		long n = 0, at = 0, sz = 7;
		mp3rss_set(&r, rates[t][0], rates[t][1]);
		while (at < NS) {
			long m = sz < NS - at ? sz : NS - at;
			memcpy(piece, in + 2 * at, (size_t)m * 4);
			n += mp3rss_run(&r, piece, (int)m, 2, res + 2 * n, (int)(NS * 7 - n));
			at += m;
			sz = sz * 5 % 4093 + 1;	/* (pieces of all sizes, 1 to 4093) */
		}
		for (k = 0; k < 2 * n; k++) {
			unsigned short w = (unsigned short)res[k];
			ob[(2 * k) % sizeof(ob)] = (unsigned char)(w >> 8);
			ob[(2 * k) % sizeof(ob) + 1] = (unsigned char)w;
			if ((2 * k + 2) % sizeof(ob) == 0 || k == 2 * n - 1)
				put(ob, (2 * k) % sizeof(ob) + 2);
		}
#ifdef HOST
		{
			MP3RS b;
			long m = 0, i, c;
			mp3rs_set(&b, rates[t][0], rates[t][1]);
			/* (in blocks of 32, its smallest: MP3RS_MAX is a DSP's
			   frame's outputs, at its ratios, 1.115 at most) */
			for (at = 0; at < NS; at += 32) {
				int got;
				for (i = 0; i < 32; i++)
					for (c = 0; c < 2; c++)
						blk[c][i] = (dw)in[2 * (at + i) + c] << 8;
				got = mp3rs_run(&b, blk, 32, 2, bout);
				for (i = 0; i < got; i++)
					for (c = 0; c < 2; c++) {
						long w = ((long)bout[c][i] + 128) >> 8;
						ref[2 * (m + i) + c] = (short)(w > 32767 ? 32767 : w);
					}
				m += got;
			}
			/* (the block one stops 4 inputs short of the end, as the
			   sample one does, or a block's worth sooner) */
			if (n < m || n > m + 7 * 32 || memcmp(res, ref, (size_t)(n < m ? n : m) * 4)) {
				fprintf(stderr, "FAIL %ld to %ld Hz: %ld outputs a sample at a time, %ld in blocks%s\n",
					rates[t][0], rates[t][1], n, m,
					memcmp(res, ref, (size_t)(n < m ? n : m) * 4) ? ", and they differ" : "");
				bad = 1;
			} else {
				fprintf(stderr, "ok   %ld to %ld Hz: %ld outputs, bit for bit with mp3rs_run's %ld\n",
					rates[t][0], rates[t][1], n, m);
			}
		}
#endif
	}
	return bad;
}
