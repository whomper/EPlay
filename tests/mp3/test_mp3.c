/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_mp3.c - the model of the DSP's MP3 decode (src/mp3/mp3dsp.c), fed
 * by the 68030's packets (src/mp3/mp3frame.c), against minimp3 (CC0,
 * third_party/) as the reference, frame by frame, with ISO/IEC 11172-4's
 * limits for a decoder of full accuracy: the difference's RMS under
 * 2^-15 / sqrt(12) of full scale, and no sample more than 2^-14 off.
 * Then the same through the resampler (mp3rs, to the Falcon's clock
 * above the file's rate, and a 48 kHz file to 44.1 too), against
 * minimp3's samples through the same taps in double precision.
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
#include "mp3tab.h"

long dsp_limited;

/* the resampler's reference: mp3rs_run's steps, in doubles */
typedef struct {
	MP3RS r;
	double hist[2][7];
} REFRS;

static int ref_run(REFRS *q, const double *in, int ns, int nch, double out[2][MP3RS_MAX])
{
	const unsigned long *tab = q->r.tab;
	int b, ch, j, k = 0;
	for (b = 0; b < ns; b += 32) {
		double x[2][39];
		for (ch = 0; ch < nch; ch++) {
			memcpy(x[ch], q->hist[ch], sizeof(q->hist[ch]));
			for (j = 0; j < 32; j++)
				x[ch][7 + j] = in[(b + j) * nch + ch];
			memcpy(q->hist[ch], x[ch] + 32, sizeof(q->hist[ch]));
		}
		while (q->r.n <= 27) {
			const unsigned long *h = &tab[((q->r.f >> 19) + 16) & 31];
			for (ch = 0; ch < nch; ch++) {
				double a = 0;
				for (j = 0; j < 8; j++)
					a += x[ch][q->r.n + 4 + j] *
					     ((long)(h[32 * j] ^ 0x800000UL) - 0x800000L) / 8388608.0;
				out[ch][k] = a;
			}
			k++;
			q->r.f += q->r.sf;
			q->r.n += (int)q->r.si + (int)(q->r.f >> 24);
			q->r.f &= 0xffffff;
		}
		q->r.n -= 32;
	}
	return k;
}

/* a resampled run's differences */
typedef struct {
	long rin, rout, count;
	double sum, worst;
} RSCMP;

/* the Falcon's clock for a file's rate: the lowest of 49170 / k at or above it */
static long falcon_rate(long rin)
{
	static const long k10[] = { 10, 15, 20, 25, 30, 40, 50, 60 };	/* (k x 10) */
	long best = 491700 / 10;
	int i;
	for (i = 0; i < 8; i++)
		if (491700 / k10[i] >= rin)
			best = 491700 / k10[i];
	return best;
}

static int one(const char *name, int quiet)
{
	static unsigned char buf[16 << 20];
	static unsigned long pk[PK_MAX];
	static dw pcm[2][1152];
	static float ref[1152 * 2];
	static MP3PARSE ps;
	static MP3DSP d;
	static MP3RS rs[3];
	static REFRS rr[3];
	static dw rout[2][MP3RS_MAX];
	static double rref[2][MP3RS_MAX];
	RSCMP rc[3];
	int nrs = 0, skip = 1;
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
		if (!nrs && ns) {	/* the resampler's runs, from the file's rate */
			long rin = info.hz;
			memset(rc, 0, sizeof(rc));
			rc[0].rin = rc[1].rin = rin;
			rc[0].rout = falcon_rate(rin);
			nrs = 1;
			if (rin == 48000) {
				rc[1].rout = 44100;
				nrs = 2;
			}
			if (rin > 32780) {	/* Quality Normal: the Falcon's 32,780 Hz */
				rc[nrs].rin = rin;
				rc[nrs].rout = 32780;
				nrs++;
			}
			for (i = 0; i < nrs; i++) {
				mp3rs_set(&rs[i], rin, rc[i].rout);
				memset(&rr[i], 0, sizeof(rr[i]));
				rr[i].r = rs[i];
			}
		}
		if (ns) {
			static double in[1152 * 2];
			int r, k, no;
			for (i = 0; i < ns * nch; i++)	/* (clipped, as the model's are) */
				in[i] = ref[i] >= 1 ? 1 - 1 / 8388608.0 : ref[i] < -1 ? -1 : ref[i];
			for (r = 0; r < nrs; r++) {
				if (pk[0] & PK_MUTE)
					mp3rs_mute(&rs[r]);
				no = mp3rs_run(&rs[r], pcm, ns, nch, rout);
				if (ref_run(&rr[r], in, ns, nch, rref) != no) {
					fprintf(stderr, "%s: frame %ld: the resamplers gave different counts\n",
						name, frames - 1);
					return 1;
				}
				if (skip || (pk[0] & PK_MUTE))
					continue;
				for (k = 0; k < no; k++)
					for (ch = 0; ch < nch; ch++) {
						double e = fabs(rout[ch][k] / 8388608.0 - rref[ch][k]);
						if (fabs(rref[ch][k]) >= 0.9999)
							continue;
						rc[r].sum += e * e;
						rc[r].count++;
						if (e > rc[r].worst)
							rc[r].worst = e;
					}
			}
			skip = (pk[0] & PK_MUTE) != 0;	/* (its taps reach into the frame before) */
		} else {
			skip = 1;
		}
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
	{
		int r;
		for (r = 0; r < nrs; r++) {
			double rr_ = rc[r].count ? sqrt(rc[r].sum / rc[r].count) : 0;
			printf("%28s resampled %ld -> %ld: rms %.2e (%.2f of the limit), max %.2e (%.2f)\n", "",
			       rc[r].rin, rc[r].rout, rr_, rr_ / lim_rms, rc[r].worst, rc[r].worst / lim_max);
			if (!rc[r].count || rr_ > lim_rms || rc[r].worst > lim_max)
				bad++;
		}
	}
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
