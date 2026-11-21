/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fbint.c - EPlay's own MP3 decoder in integers (src/mp3/mp3dsp.c, the
 * DSP's C model: ISO 11172-4 full accuracy) on the FireBee's ColdFire,
 * under qemu-m68k -cpu cfv4e (common/tests/coldfire's runtime), or on the
 * host (HOST): an MP3's frames packed (mp3frame.c) and decoded, each
 * sample's 24-bit word written as 32 bits big-endian to stdout. Integer
 * arithmetic: the ColdFire's must be the host's, bit for bit (with its
 * multiply-accumulates on the EMAC too, where EPLAY_EMAC builds them).
 *
 *	fbint FILE.MP3 > OUT.RAW
 */
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include "rt.h"
#endif
#include "mp3frame.h"
#include "mp3dsp.h"

static unsigned char buf[8 << 20];
static unsigned long pk[PK_MAX];
static MP3PARSE ps;
static MP3DSP d;
static dw pcm[2][1152];
static unsigned char outb[2 * 1152 * 4];

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
	long n, off = 0, frames = 0;
	if (argc < 2 || (n = read_file(argv[1], buf, sizeof(buf))) <= 0)
		return 2;
	if (n > 10 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3')
		off = 10 + ((long)(buf[6] & 127) << 21 | (long)(buf[7] & 127) << 14 |
			    (long)(buf[8] & 127) << 7 | (buf[9] & 127));
	mp3_reset(&ps);
	mp3dsp_reset(&d);
	while (off + 4 <= n) {
		MP3HDR h;
		int nch, ns, ch, i, k = 0;
		if (!mp3_header(buf + off, &h) || off + h.length > n) {
			off++;
			continue;
		}
		if (mp3_packet(&ps, &h, buf + off, pk) < 0) {
			off += h.length;
			continue;
		}
		off += h.length;
		nch = mp3dsp_frame(&d, pk, pcm, &ns);
		for (i = 0; i < ns; i++)
			for (ch = 0; ch < nch; ch++) {
				unsigned long w = (unsigned long)pcm[ch][i];
				outb[k++] = (unsigned char)(w >> 24);
				outb[k++] = (unsigned char)(w >> 16);
				outb[k++] = (unsigned char)(w >> 8);
				outb[k++] = (unsigned char)w;
			}
		if (k)
			put(outb, k);
		frames++;
	}
	return frames > 0 ? 0 : 1;
}
