/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * check.c - huff68.c (the 68030's Huffman decoding) against the DSP's
 * model, every granule of the files given: the model's own values come
 * through its hook (MP3DSP_HUFF_HOOK), and huff68_granule() decodes the
 * same bits from the main data as bytes. With -o FILE it also writes the
 * granules for bench.c, the 68030's timing in Hatari: each granule its
 * side information, its bits' start and end, and its bytes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mp3frame.h"
#include "mp3dsp.h"
#include "huff68.h"

static long granules, wrong, frames;
static FILE *out;
static unsigned char bytes[PK_MAXDATA * 3 + 16];

static void put32(unsigned long v)
{
	unsigned char b[4] = { v >> 24, v >> 16, v >> 8, v };
	fwrite(b, 1, 4, out);
}

void mp3dsp_huff_hook(const unsigned long *g, const unsigned long *w, long nw, long start, long end, const int *is)
{
	short got[576];
	long k, nb = (nw * 23 + 7) / 8;
	int i;
	/* the 23-bit words as one stream of bits, in bytes */
	memset(bytes, 0, sizeof(bytes));
	for (k = 0; k < nw * 23; k++)
		if ((w[k / 23] >> (22 - k % 23)) & 1)
			bytes[k >> 3] |= 0x80 >> (k & 7);
	huff68_granule(g, bytes, start, end, got);
	granules++;
	for (i = 0; i < 576; i++)
		if (got[i] != is[i]) {
			if (wrong < 5)
				fprintf(stderr, "granule %ld line %d: %d, the model %d\n", granules, i, got[i], is[i]);
			wrong++;
			break;
		}
	if (out) {
		long b0 = start >> 3, b1 = (end + 7) / 8 + 4;
		if (b1 > nb + 4)
			b1 = nb + 4;
		for (i = 0; i < PK_GC; i++)
			put32(g[i]);
		put32((unsigned long)(start - b0 * 8));
		put32((unsigned long)(end - b0 * 8));
		put32((unsigned long)(b1 - b0));
		fwrite(bytes + b0, 1, (size_t)(b1 - b0), out);
	}
}

int main(int argc, char **argv)
{
	static unsigned char buf[16 << 20];
	static unsigned long pk[PK_MAX];
	static MP3PARSE ps;
	static MP3DSP d;
	static dw pcm[2][1152];
	int a = 1;
	if (argc > 2 && !strcmp(argv[1], "-o")) {
		out = fopen(argv[2], "wb");
		a = 3;
	}
	for (; a < argc; a++) {
		FILE *f = fopen(argv[a], "rb");
		long n, off = 0, g0 = granules, w0 = wrong;
		if (!f) {
			perror(argv[a]);
			return 1;
		}
		n = (long)fread(buf, 1, sizeof(buf), f);
		fclose(f);
		mp3_reset(&ps);
		mp3dsp_reset(&d);
		while (off + 4 <= n) {
			MP3HDR h;
			int np, ns;
			if (!mp3_header(buf + off, &h) || off + h.length > n) {
				off++;
				continue;
			}
			np = mp3_packet(&ps, &h, buf + off, pk);
			off += h.length;
			if (np < 0)
				continue;
			frames++;
			mp3dsp_frame(&d, pk, pcm, &ns);
		}
		printf("%-14s %6ld granules, %ld wrong\n", strrchr(argv[a], '/') ? strrchr(argv[a], '/') + 1 : argv[a],
		       granules - g0, wrong - w0);
	}
	if (out)
		fclose(out);
	printf("all: %ld granules, %ld wrong\n", granules, wrong);
	return wrong != 0;
}
