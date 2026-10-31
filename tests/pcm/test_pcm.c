/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_pcm.c - src/pcm.c against sox: each FILE is probed (its rate,
 * channels and bits must be RATE CH BITS) and made 16-bit stereo, and
 * that must equal RAW, sox's own 16-bit stereo of it (no dither).
 *
 *     test_pcm FILE RAW RATE CH BITS
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pcm.h"

int main(int argc, char **argv)
{
	static unsigned char buf[8 << 20], raw[8 << 20];
	static short out[4 << 20];
	PCMFMT f;
	const char *why;
	FILE *fp;
	long n, nr, i, bad = 0;

	if (argc != 6)
		return 2;
	fp = fopen(argv[1], "rb");
	n = (long)fread(buf, 1, sizeof(buf), fp);
	fclose(fp);
	fp = fopen(argv[2], "rb");
	nr = (long)fread(raw, 1, sizeof(raw), fp);
	fclose(fp);
	if (!pcm_probe(buf, n, &f, &why)) {
		printf("%s: not taken: %s\n", argv[1], why);
		return 1;
	}
	if (f.rate != atol(argv[3]) || f.channels != atoi(argv[4]) || f.bits != atoi(argv[5])) {
		printf("%s: %ld Hz %d ch %d bits, not %s %s %s\n", argv[1], f.rate, f.channels, f.bits,
		       argv[3], argv[4], argv[5]);
		return 1;
	}
	if (f.frames * 4 != nr) {
		printf("%s: %ld frames, sox has %ld\n", argv[1], f.frames, nr / 4);
		return 1;
	}
	pcm_convert(&f, buf + f.data, out, f.frames);
	for (i = 0; i < f.frames * 2; i++) {
		short s = (short)(raw[2 * i] | raw[2 * i + 1] << 8);	/* sox: little-endian */
		if (s != out[i] && !bad++)
			printf("%s: sample %ld: %d, sox %d\n", argv[1], i, out[i], s);
	}
	printf("%-24s %s %6ld Hz %d ch %2d bits %6ld frames: %s\n", strrchr(argv[1], '/') + 1,
	       f.kind == PCM_WAV ? "WAV " : "AIFF", f.rate, f.channels, f.bits, f.frames,
	       bad ? "DIFFERENT" : "the same");
	return bad != 0;
}
