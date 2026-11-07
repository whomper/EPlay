/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * bench.c - huff68.c's time on a 68030 (bench.py runs it in Hatari):
 * every granule of C:\HUFF.BIN (check.c -o: a file's granules, their
 * side information and bytes) decoded over and over for 3 seconds,
 * timed by the 200 Hz clock; then the share of the 68030 that playing
 * the file would take, at 4 granules (2 channels) a 1,152 samples of
 * 44.1 kHz. Its line: "BENCH granules a second N; share x10 %: M".
 */
#include <stdio.h>
#include "tos.h"
#include "huff68.h"

#define MAXG 4096
static const unsigned long *gs[MAXG];
static const unsigned char *ds[MAXG];
static long st[MAXG], en[MAXG];
static int ng;
static short is[576];

#pragma GCC diagnostic ignored "-Warray-bounds"
static long hz200(void)
{
	return *(volatile long *)0x4baL;
}

static unsigned long be32(const unsigned char *p)
{
	return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

static long run(void)
{
	long t0, t1, n = 0;
	char line[100];
	int i;
	t0 = hz200();
	while (hz200() == t0)
		;
	t0 = hz200();
	do {
		for (i = 0; i < ng; i++)
			huff68_granule(gs[i], ds[i], st[i], en[i], is);
		n += ng;
		t1 = hz200();
	} while (t1 - t0 < 600);
	{
		long gps = n * 200 / (t1 - t0);
		/* 44.1 kHz stereo: 4 x 44100 / 1152 = 153.1 granules a second */
		long share = 1531L * 1000 / gps;
		snprintf(line, sizeof(line), "BENCH granules a second %ld; share x10 %%: %ld\n", gps, share);
		nf_debug(line);
	}
	return 0;
}

int main(void)
{
	long fh = Fopen("C:\\HUFF.BIN", 0), len;
	unsigned char *buf, *p, *e;
	if (fh < 0) {
		nf_debug("BENCH no HUFF.BIN\n");
		return 1;
	}
	len = Fseek(0, (short)fh, 2);
	Fseek(0, (short)fh, 0);
	buf = (unsigned char *)Malloc(len + 16);
	Fread((short)fh, len, buf);
	Fclose((short)fh);
	/* the granules: 9 words of side information (as longs, in place), the
	   start and end, the byte count, then the bytes */
	for (p = buf, e = buf + len; p + 48 <= e && ng < MAXG; ng++) {
		unsigned long *g = (unsigned long *)p;
		long nb;
		{
			int k;
			for (k = 0; k < 9; k++)
				g[k] = be32(p + 4 * k);
		}
		gs[ng] = g;
		st[ng] = (long)be32(p + 36);
		en[ng] = (long)be32(p + 40);
		nb = (long)be32(p + 44);
		ds[ng] = p + 48;
		p += 48 + nb;
	}
	Supexec(run);
	return 0;
}
