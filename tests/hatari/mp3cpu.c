/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * mp3cpu.c - MP3CPU.PRG: how fast EPlay's C model of its DSP decoder
 * (src/mp3/mp3dsp.c) decodes on the CPU it is built for: C:\TEST.MP3's
 * frames, decoded and timed by the 200 Hz clock, the result to the
 * console (NatFeats): "MP3CPU <frames> frames in <ms> ms; real time
 * <ms of sound> ms (<percent> % of the CPU)". A measurement for the 68060
 * (DESIGN.md, "MP3 on the CPU"), not a test.
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"
#include "mp3frame.h"
#include "mp3dsp.h"

#define MAXF (1024L * 1024)

static unsigned char buf[MAXF];
static unsigned long pk[PK_MAX];
static MP3PARSE ps;
static MP3DSP d;
static dw pcm[2][1152];

int main(void)
{
	char line[160];
	long n, off = 0, frames = 0, sound = 0, hz = 0;
	u32 t0, t;
	int fh = (int)Fopen("C:\\TEST.MP3", 0);
	if (fh < 0) {
		nf_debug("MP3CPU FAIL no C:\\TEST.MP3\n");
		return 1;
	}
	n = Fread(fh, MAXF, buf);
	Fclose(fh);
	if (n > 10 && !memcmp(buf, "ID3", 3))
		off = 10 + ((long)(buf[6] & 127) << 21 | (long)(buf[7] & 127) << 14 | (long)(buf[8] & 127) << 7 |
			    (buf[9] & 127));
	mp3_reset(&ps);
	mp3dsp_reset(&d);
	t0 = tos_ms();
	while (off + 4 <= n) {
		MP3HDR h;
		int ns;
		if (!mp3_header(buf + off, &h) || off + h.length > n) {
			off++;
			continue;
		}
		hz = h.rate;
		if (mp3_packet(&ps, &h, buf + off, pk) >= 0) {
			mp3dsp_frame(&d, pk, pcm, &ns);
			sound += h.version == 1 ? 1152 : 576;
		}
		off += h.length;
		frames++;
	}
	t = tos_ms() - t0;
	{
		long real = hz ? sound * 1000 / hz : 0;
		snprintf(line, sizeof(line), "MP3CPU %ld frames in %lu ms; real time %ld ms (%ld %% of the CPU)\n",
			 frames, (unsigned long)t, real, real ? (long)t * 100 / real : 0);
		nf_debug(line);
	}
	return 0;
}
