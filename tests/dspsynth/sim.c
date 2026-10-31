/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sim.c - dsp/synth.a56 on Hatari's DSP core against the model's synth()
 * (src/mp3), bit for bit: every granule of every channel of the files
 * given, its subband samples from the model through the DSP, the 576
 * samples compared. Prints the DSP's cycles for the filter and what they
 * come to on a 32 MHz DSP.
 *
 *     sim FILE.MP3...
 */
#include <string.h>
#include "../dspsim.h"
#include "mp3frame.h"
#include "mp3dsp.h"
#include "mp3tab.h"
#include "synth_prog.h"

static long gcs, bad;
static unsigned long long syn_cycles;

static unsigned long w24(dw v)
{
	return (unsigned long)v & 0xffffff;
}

void mp3dsp_synth_hook(int ch, dw sub[32][18], const dw *pcm)
{
	int k, t, wrong = 0;
	put(3);
	put((unsigned long)ch);
	for (k = 0; k < 32; k++)
		for (t = 0; t < 18; t++)
			put(w24(sub[k][t]));
	syn_cycles += sim_job();
	for (k = 0; k < 576 && !dead; k++) {
		long v = get();
		if (v != pcm[k] && !wrong++)
			fprintf(stderr, "granule %ld (channel %d): sample %d: the DSP %ld, the model %ld\n",
				gcs, ch, k, v, (long)pcm[k]);
	}
	if (wrong && getenv("SIMDEBUG"))
		exit(1);
	gcs++;
	bad += wrong != 0;
}

int main(int argc, char **argv)
{
	static unsigned long pk[PK_MAX], tab[512];
	static dw pcm[2][1152];
	static unsigned char buf[16 << 20];
	int a, i, j, t, fail = 0;

	sim_start(dsp_synth_prog);
	load(4, 0x2000, t_synth, T_SYNTH_N);
	for (j = 1, i = 0; j < 16; j++)		/* D[j + 32t], t = 0..15 */
		for (t = 0; t < 16; t++)
			tab[i++] = t_dwin[j + 32 * t];
	load(4, 0x0000, tab, i);
	for (i = 0, j = 0; j <= 16; j += 16) {
		for (t = 0; t < 16; t++)
			tab[i++] = t_dwin[j + 32 * t];
		tab[i++] = 0;
	}
	load(4, 0x2200, tab, i);

	for (a = 1; a < argc && !dead; a++) {
		static MP3PARSE ps;
		static MP3DSP d;
		FILE *f = fopen(argv[a], "rb");
		long n, off = 0, samples = 0, rate = 0, g0 = gcs, b0 = bad;
		unsigned long long c0 = syn_cycles;
		double secs;
		int nch = 1;
		if (!f) {
			perror(argv[a]);
			return 1;
		}
		n = (long)fread(buf, 1, sizeof(buf), f);
		fclose(f);
		mp3_reset(&ps);
		mp3dsp_reset(&d);
		put(5);
		while (off + 4 <= n && !dead) {
			MP3HDR h;
			int ns;
			if (!mp3_header(buf + off, &h) || off + h.length > n) {
				off++;
				continue;
			}
			if (mp3_packet(&ps, &h, buf + off, pk) > 0) {
				if (pk[0] & PK_MUTE)
					put(5);
				mp3dsp_frame(&d, pk, pcm, &ns);
				samples += h.samples;
				nch = h.channels;
			}
			rate = h.rate;
			off += h.length;
		}
		secs = rate ? (double)samples / rate : 1;
		printf("%-14s %d ch %5ld kHz %5ld granules, %ld wrong: %6.0f cycles a granule, %4.1f%% of the DSP\n",
		       strrchr(argv[a], '/') ? strrchr(argv[a], '/') + 1 : argv[a], nch, rate / 1000,
		       gcs - g0, bad - b0, (double)(syn_cycles - c0) / (gcs - g0 ? gcs - g0 : 1),
		       100.0 * (syn_cycles - c0) / secs / 32e6);
		fail |= bad != b0;
	}
	return sim_done() | fail;
}
