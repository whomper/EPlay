/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * mp3mini.c - MP3MINI.PRG: minimp3 (CC0, tests/mp3/third_party) built for
 * the 68060 and its FPU, timed as mp3cpu.c times EPlay's DSP model: C:\TEST.MP3
 * decoded, the result to the console. A measurement for MP3FPU.OVL
 * (DESIGN.md, "MP3 on the CPU"), not a test.
 */
#include <stdio.h>
#include <string.h>
#include "tos.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "minimp3.h"

#define MAXF (1024L * 1024)

static unsigned char buf[MAXF];
static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static mp3dec_t dec;

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
	mp3dec_init(&dec);
	t0 = tos_ms();
	while (off < n) {
		mp3dec_frame_info_t info;
		int ns = mp3dec_decode_frame(&dec, buf + off, (int)(n - off), pcm, &info);
		if (!info.frame_bytes)
			break;
		off += info.frame_bytes;
		if (ns) {
			frames++;
			sound += ns;
			hz = info.hz;
		}
	}
	t = tos_ms() - t0;
	{
		long real = hz ? sound * 1000 / hz : 0;
		snprintf(line, sizeof(line), "MP3CPU minimp3: %ld frames in %lu ms; real time %ld ms (%ld %% of the CPU)\n",
			 frames, (unsigned long)t, real, real ? (long)t * 100 / real : 0);
		nf_debug(line);
	}
	return 0;
}
