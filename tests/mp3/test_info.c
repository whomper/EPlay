/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_info.c - mp3_info_frames (src/mp3/mp3frame.c) on the test files:
 * where a file has a Xing, Info or VBRI frame, the count it gives is the
 * file's frames after it (counted here, frame by frame); where it has
 * none, 0. The player's length comes from it (a VBR file's first frame's
 * bit rate made an 8-minute file 22:10, Erez's Falcon, 31 Oct 2026).
 * And a Xing frame's table (mp3_info_toc), which going to a place in a VBR
 * file follows: each per cent's place, from the Xing frame on, within
 * 2/256 and a frame of where its frame begins.
 */
#include <stdio.h>
#include <string.h>
#include "mp3frame.h"

static unsigned char b[1 << 24];
static long at[1 << 18];		/* each frame's place */

int main(int argc, char **argv)
{
	int a, bad = 0;
	for (a = 1; a < argc; a++) {
		FILE *f = fopen(argv[a], "rb");
		long n, off = 0, frames = 0, info = -1;
		int vbr = 0, has, toc_ok = -1;
		unsigned char toc[100];
		MP3HDR h, h0;
		if (!f) {
			perror(argv[a]);
			return 1;
		}
		n = (long)fread(b, 1, sizeof(b), f);
		fclose(f);
		if (n > 10 && !memcmp(b, "ID3", 3))
			off = 10 + ((long)(b[6] & 127) << 21 | (long)(b[7] & 127) << 14 | (long)(b[8] & 127) << 7 | (b[9] & 127));
		while (off + 4 <= n) {
			if (mp3_header(b + off, &h) && off + h.length <= n) {
				if (info < 0) {
					info = mp3_info_frames(b + off, &h, &vbr);
					h0 = h;
					toc_ok = mp3_info_toc(b + off, &h, toc);
				}
				if (frames < (long)(sizeof(at) / sizeof(at[0])))
					at[frames] = off;
				frames++;
				off += h.length;
			} else {
				off++;
			}
		}
		/* LAME's files made with -t have no header frame; the others do */
		has = !strstr(argv[a], "/mix") && !strstr(argv[a], "/lsf") && !strstr(argv[a], "/mono") &&
		      !strstr(argv[a], "/square") && !strstr(argv[a], "/noise") && !strstr(argv[a], "/sweep") &&
		      !strstr(argv[a], "/clicks") && !strstr(argv[a], "/tones") && !strstr(argv[a], "/loud") &&
		      !strstr(argv[a], "/hot");
		if (toc_ok > 0) {
			/* the frames after the Xing frame: the table's per cents */
			long nf = frames - 1, total = n - at[0], i, worst = 0;
			for (i = 0; i < 100; i++) {
				long fi = 1 + i * nf / 100, real = (at[fi] - at[0]) * 256 / total, d = toc[i] - real;
				if (d < 0)
					d = -d;
				if (d > worst)
					worst = d;
			}
			/* (within 2/256, and a frame: a short file's per cent is a
			   frame or two) */
			if (worst > 2 + 256 * 1441 / total) {
				printf("%-32s its table is %ld/256 off\n", argv[a], worst);
				bad++;
			} else
				printf("%-32s its table within %ld/256\n", argv[a], worst);
		} else if (vbr && !strstr(argv[a], "vbri")) {
			printf("%-32s a Xing VBR file without its table\n", argv[a]);
			bad++;
		}
		(void)h0;
		if (has ? info != frames - 1 : info != 0) {
			printf("%-32s %ld frames, the header says %ld: wrong\n", argv[a], frames, info);
			bad++;
		} else {
			printf("%-32s %ld frames, the header says %ld%s\n", argv[a], frames, info,
			       has ? (vbr ? " (VBR)" : " (CBR)") : "");
		}
	}
	printf(bad ? "%d wrong\n" : "all as they should be\n", bad);
	return bad != 0;
}
