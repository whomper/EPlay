/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fbmp3.c - EPLAYFB.PRG's MP3 decoder (ui/cmp3fb.c: minimp3 for the
 * FireBee's ColdFire and its FPU) run under qemu-m68k -cpu cfv4e as a
 * Linux program (common/tests/coldfire's runtime), or on the host (HOST):
 * an MP3's frames decoded one at a time as play.c's fill_cmp takes them,
 * the samples written as 16-bit big-endian words to stdout, for compare.py
 * to hold the ColdFire's against the host's.
 *
 *	fbmp3 FILE.MP3 > OUT.RAW
 */
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#else
#include "rt.h"
#endif
#include "cmp3.h"

static unsigned char buf[8 << 20];
static short pcm[1152 * 2];
static unsigned char outb[1152 * 4];

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

int main(int argc, char **argv)
{
	long n, at = 0, frames = 0;
	char err[80];
	if (argc < 2 || (n = read_file(argv[1], buf, sizeof(buf))) <= 0)
		return 2;
	cmp3_load(err, sizeof(err));
	while (at < n) {
		MP3OVL_INFO fi;
		int ns = cmp3_frame(buf + at, (int)(n - at > 16384 ? 16384 : n - at), pcm, &fi), k, m;
		if (fi.frame_bytes <= 0)
			break;
		at += fi.frame_bytes;
		m = ns * (fi.channels == 2 ? 2 : 1);
		for (k = 0; k < m; k++) {
			outb[2 * k] = (unsigned char)((unsigned short)pcm[k] >> 8);
			outb[2 * k + 1] = (unsigned char)pcm[k];
		}
		if (m)
			put(outb, 2L * m);
		frames++;
	}
	return frames > 0 ? 0 : 1;
}
