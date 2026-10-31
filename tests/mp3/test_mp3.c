/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_mp3.c - the model of the DSP's MP3 decode (src/mp3/mp3dsp.c), fed
 * by the 68030's packets (src/mp3/mp3frame.c), against minimp3 (CC0,
 * third_party/) as the reference, frame by frame, with ISO/IEC 11172-4's
 * limits for a decoder of full accuracy: the difference's RMS under
 * 2^-15 / sqrt(12) of full scale, and no sample more than 2^-14 off.
 *
 *     test_mp3 [-q] FILE.MP3...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#include "third_party/minimp3.h"
#include "mp3frame.h"
#include "mp3dsp.h"

long dsp_limited;

static int one(const char *name, int quiet)
{
	static unsigned char buf[16 << 20];
	static unsigned long pk[PK_MAX];
	static dw pcm[2][1152];
	static float ref[1152 * 2];
	static MP3PARSE ps;
	static MP3DSP d;
	mp3dec_t dec;
	mp3dec_frame_info_t info;
	FILE *f = fopen(name, "rb");
	long n, off = 0, frames = 0, compared = 0, count = 0, bad = 0;
	double sum = 0, worst = 0, rms, lim_rms = pow(2, -15) / sqrt(12), lim_max = pow(2, -14);
	long worst_frame = -1, ms = 0, is = 0, gshort = 0, gmixed = 0, gswitch = 0, glin = 0;

	if (!f) {
		perror(name);
		return 1;
	}
	n = (long)fread(buf, 1, sizeof(buf), f);
	fclose(f);
	if (n > 10 && !memcmp(buf, "ID3", 3))
		off = 10 + ((long)(buf[6] & 127) << 21 | (long)(buf[7] & 127) << 14 |
			    (long)(buf[8] & 127) << 7 | (buf[9] & 127));
	mp3dec_init(&dec);
	mp3_reset(&ps);
	mp3dsp_reset(&d);
	while (off + 4 <= n) {
		MP3HDR h;
		int np, nch, ns, nm, ch, i;
		if (!mp3_header(buf + off, &h) || off + h.length > n) {
			off++;
			continue;
		}
		ns = mp3dec_decode_frame(&dec, buf + off, (int)(n - off), ref, &info);
		if (info.frame_bytes != h.length) {
			fprintf(stderr, "%s: frame %ld at %ld: minimp3 took %d bytes, we %d\n",
				name, frames, off, info.frame_bytes, h.length);
			return 1;
		}
		np = mp3_packet(&ps, &h, buf + off, pk);
		off += h.length;
		frames++;
		if (np < 0) {
			bad++;
			continue;
		}
		if (!(pk[0] & PK_MUTE)) {
			int g;
			ms += (pk[0] & PK_MS) != 0;
			is += (pk[0] & PK_IS) != 0;
			for (g = 0; g < (h.version == 1 ? 2 : 1) * h.channels; g++) {
				const unsigned long *gw = pk + 2 + g * PK_GC;
				int bt = (int)(gw[4] & 3), t;
				gshort += bt == 2 && !(gw[4] & 4);
				gmixed += bt == 2 && (gw[4] & 4);
				gswitch += bt == 1 || bt == 3;
				for (t = 0; t < 3; t++)
					if (((gw[5] >> (5 * t)) & 31) >= 16) {
						glin++;
						break;
					}
			}
		}
		nch = mp3dsp_frame(&d, pk, pcm, &nm);
		if (ns && nm != ns) {
			fprintf(stderr, "%s: frame %ld: minimp3 gave %d samples, we %d\n", name, frames - 1, ns, nm);
			return 1;
		}
		if (!ns || (pk[0] & PK_MUTE))
			continue;
		compared++;
		for (i = 0; i < ns; i++)
			for (ch = 0; ch < nch; ch++) {
				double r = ref[i * info.channels + ch], m = pcm[ch][i] / 8388608.0, e;
				if (fabs(r) >= 0.9999)
					continue;	/* the model clips at full scale */
				e = fabs(m - r);
				sum += e * e;
				count++;
				if (e > worst) {
					worst = e;
					worst_frame = frames - 1;
				}
			}
	}
	rms = count ? sqrt(sum / count) : 0;
	printf("%-28s %5ld frames, %5ld compared, %ld bad: rms %.2e (%.2f of the limit), "
	       "max %.2e (%.2f, frame %ld), %ld limited\n", name, frames, compared, bad,
	       rms, rms / lim_rms, worst, worst / lim_max, worst_frame, d.sat + dsp_limited);
	dsp_limited = 0;
	if (!quiet)
		printf("%28s frames with mid/side %ld, intensity %ld; granules short %ld, mixed %ld, "
		       "start/stop %ld, with linbits %ld\n", "", ms, is, gshort, gmixed, gswitch, glin);
	return !compared || bad || rms > lim_rms || worst > lim_max;
}

int main(int argc, char **argv)
{
	int i, q = 0, fail = 0;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-q")) {
			q = 1;
			continue;
		}
		fail |= one(argv[i], q);
	}
	return fail;
}
