/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_mp3int.c - MP3INT.OVL's decoder (mp3ovl/madhead.c, libmad with
 * fixed.h's FPM_68060 and OPT_SPEED, as the Makefile builds it) on the
 * host, fed as play.c's fill_cmp feeds it: a 16 KB buffer topped up from
 * the file, each call's frame_bytes taken. Its samples against minimp3's
 * (third_party/minimp3, the tests' reference) with ISO/IEC 11172-4's
 * limit for a decoder of limited accuracy (the difference's RMS under
 * 2^-11 / sqrt(12)); the full-accuracy figures printed beside. FPM_68060's
 * arithmetic is plain C, so the host's samples are the 68060's (MP3MAD's
 * sums, 17 Nov 2026).
 */
#define MP3OVL_HOST
#include "../../mp3ovl/madhead.c"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#include "minimp3.h"

#define MAXS (12L << 20)
static float ref[MAXS];
static short got[MAXS];
static DEC dec;

static long by_ovl(const unsigned char *f, long n, int *ch)
{
	static unsigned char in[16384];
	long at = 0, ng = 0, cin = 0, cat = 0, frames = 0;
	mp3ovl.init(&dec);
	for (;;) {
		MP3OVL_INFO fi;
		int ns;
		if (cin < (long)sizeof(in) / 2 && at < n) {
			long want = (long)sizeof(in) - cin, k;
			memmove(in, in + cat, (size_t)cin);
			cat = 0;
			k = n - at < want ? n - at : want;
			memcpy(in + cin, f + at, (size_t)k);
			at += k;
			cin += k;
		}
		if (cin < 4)
			break;
		ns = mp3ovl.frame(&dec, in + cat, (int)cin, got + ng, &fi);
		if (fi.frame_bytes <= 0) {
			if (at < n && cin < (long)sizeof(in) / 2)
				continue;
			break;
		}
		if (fi.frame_bytes > cin)
			fi.frame_bytes = (int)cin;
		cat += fi.frame_bytes;
		cin -= fi.frame_bytes;
		if (ns > 0) {
			*ch = fi.channels;
			ng += (long)ns * fi.channels;
			frames++;
		}
		if (ng > MAXS - 1152 * 2)
			break;
	}
	return frames ? ng : 0;
}

int main(int argc, char **argv)
{
	int i, bad = 0;
	double lim_lim = pow(2, -11) / sqrt(12), lim_rms = pow(2, -15) / sqrt(12), lim_max = pow(2, -14);
	for (i = 1; i < argc; i++) {
		FILE *fp = fopen(argv[i], "rb");
		unsigned char *f;
		long n, off = 0, nr = 0, ng, k, best = 0, cnt = 0;
		int ch = 2, rch = 2;
		double bsum = 1e300, sum = 0, worst = 0, rms;
		mp3dec_t d;
		const char *name = strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i];
		if (!fp) {
			printf("%s: can't be opened\n", argv[i]);
			return 1;
		}
		fseek(fp, 0, SEEK_END);
		n = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		f = malloc((size_t)n + 64);
		if (fread(f, 1, (size_t)n, fp) != (size_t)n)
			n = 0;
		fclose(fp);
		/* (from where play.c starts the decoder: past an ID3v2 tag) */
		if (n > 10 && !memcmp(f, "ID3", 3)) {
			long t = 10 + ((long)(f[6] & 127) << 21 | (long)(f[7] & 127) << 14 | (long)(f[8] & 127) << 7 | (f[9] & 127)) +
				 (f[5] & 0x10 ? 10 : 0);
			if (t < n) {
				memmove(f, f + t, (size_t)(n - t));
				n -= t;
			}
		}
		mp3dec_init(&d);
		while (off < n && nr < MAXS - 1152 * 2) {
			mp3dec_frame_info_t fi;
			int ns = mp3dec_decode_frame(&d, f + off, (int)(n - off), ref + nr, &fi);
			if (!fi.frame_bytes)
				break;
			off += fi.frame_bytes;
			nr += (long)ns * fi.channels;
			if (ns)
				rch = fi.channels;
		}
		ng = by_ovl(f, n, &ch);
		free(f);
		if (!ng || ch != rch) {
			printf("%-28s FAIL: %ld samples, %d channels (minimp3: %ld, %d)\n", name, ng, ch, nr, rch);
			bad = 1;
			continue;
		}
		/* (where the two line up, in sample frames: libmad's first frame
		   may differ by one from minimp3's) */
		for (k = -2304; k <= 2304; k++) {
			double s = 0;
			long j, c = 0;
			for (j = 4608; j < 4608 + 20000 && j < nr; j++) {
				long g = j + k * ch;
				double e;
				if (g < 0 || g >= ng)
					continue;
				e = got[g] / 32768.0 - ref[j];
				s += e * e;
				c++;
			}
			if (c > 1000 && s < bsum) {
				bsum = s;
				best = k;
			}
		}
		for (k = 0; k < nr; k++) {
			long g = k + best * ch;
			double r = ref[k], e;
			if (g < 0 || g >= ng)
				continue;
			if (r > 32767 / 32768.0)
				r = 32767 / 32768.0;
			if (r < -1)
				r = -1;
			e = fabs(got[g] / 32768.0 - r);
			sum += e * e;
			cnt++;
			if (e > worst)
				worst = e;
		}
		rms = cnt ? sqrt(sum / cnt) : 1;
		printf("%-28s %7ld samples (minimp3 %7ld, from %ld): rms %.2e (%.3f of the limited-accuracy limit, "
		       "%.2f of full's), max %.2e (%.2f of full's)%s\n", name, ng / ch, nr / rch, best, rms,
		       rms / lim_lim, rms / lim_rms, worst, worst / lim_max, rms > lim_lim || labs(ng / ch - nr / rch) > 2304 ? "  FAIL" : "");
		if (rms > lim_lim || labs(ng / ch - nr / rch) > 2304)
			bad = 1;
	}
	return bad;
}
