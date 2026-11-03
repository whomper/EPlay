/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * pcm.c - WAV and AIFF (pcm.h). A WAV is RIFF: little-endian chunks, "fmt "
 * (format 1, PCM, or 0xFFFE, WAVE_FORMAT_EXTENSIBLE with PCM inside) and
 * "data". An AIFF is big-endian: "COMM" (channels, frames, bits, the
 * rate as an 80-bit float) and "SSND" (an offset, then the samples);
 * AIFC with "NONE" or "twos" too.
 */
#include <string.h>
#include "pcm.h"

static unsigned long le32(const unsigned char *p)
{
	return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 |
	       (unsigned long)p[3] << 24;
}

static unsigned le16(const unsigned char *p)
{
	return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static unsigned long be32(const unsigned char *p)
{
	return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 |
	       (unsigned long)p[3];
}

static unsigned be16(const unsigned char *p)
{
	return (unsigned)p[0] << 8 | (unsigned)p[1];
}

/* the 80-bit IEEE float of COMM's rate, as a whole number of Hz */
static long ext80(const unsigned char *p)
{
	int e = (int)((p[0] & 0x7f) << 8 | p[1]) - 16383 - 31;
	unsigned long m = be32(p + 2);
	if (p[0] & 0x80)
		return 0;
	while (e > 0 && !(m & 0x80000000UL)) {
		m <<= 1;
		e--;
	}
	if (e > 0)
		return 0;
	while (e < 0) {
		m >>= 1;
		e++;
	}
	return (long)m;
}

static int fmt_ok(PCMFMT *f, const char **why)
{
	if (f->channels < 1 || f->channels > 2) {
		*why = "it has more than two channels";
		return 0;
	}
	if (f->bits != 8 && f->bits != 16 && f->bits != 24 && f->bits != 32) {
		*why = "its samples aren't 8, 16, 24 or 32 bits";
		return 0;
	}
	if (f->rate < 4000 || f->rate > 200000) {
		*why = "its sample rate isn't one EPlay knows";
		return 0;
	}
	f->bytes = f->channels * f->bits / 8;
	return 1;
}

int pcm_probe(const unsigned char *p, long n, PCMFMT *f, const char **why)
{
	long at, size;
	int have_fmt = 0;

	memset(f, 0, sizeof(*f));
	*why = "it isn't a WAV or AIFF file";
	if (n > 4096)
		n = 4096;
	if (n < 12)
		return 0;
	if (!memcmp(p, "RIFF", 4) && !memcmp(p + 8, "WAVE", 4)) {
		f->kind = PCM_WAV;
		for (at = 12; at + 8 <= n; at += 8 + size + (size & 1)) {
			size = (long)le32(p + at + 4);
			if (size < 0)
				break;
			if (!memcmp(p + at, "fmt ", 4) && at + 8 + 16 <= n) {
				unsigned tag = le16(p + at + 8);
				if (tag == 0xfffe && size >= 40 && at + 8 + 26 <= n)
					tag = le16(p + at + 8 + 24);	/* the subformat's GUID starts with it */
				if (tag != 1) {
					*why = "it is a compressed WAV (EPlay plays PCM)";
					return 0;
				}
				f->channels = (int)le16(p + at + 10);
				f->rate = (long)le32(p + at + 12);
				f->bits = (int)le16(p + at + 22);
				have_fmt = 1;
			} else if (!memcmp(p + at, "data", 4)) {
				if (!have_fmt || !fmt_ok(f, why))
					return 0;
				f->data = at + 8;
				f->frames = size / f->bytes;
				f->sign8 = 0;
				return 1;
			}
		}
		*why = "its format or its samples weren't found";
		return 0;
	}
	if (!memcmp(p, "FORM", 4) && (!memcmp(p + 8, "AIFF", 4) || !memcmp(p + 8, "AIFC", 4))) {
		int aifc = !memcmp(p + 8, "AIFC", 4);
		f->kind = PCM_AIFF;
		f->big = 1;
		f->sign8 = 1;
		for (at = 12; at + 8 <= n; at += 8 + size + (size & 1)) {
			size = (long)be32(p + at + 4);
			if (size < 0)
				break;
			if (!memcmp(p + at, "COMM", 4) && at + 8 + 18 <= n) {
				f->channels = (int)be16(p + at + 8);
				f->frames = (long)be32(p + at + 10);
				f->bits = (int)be16(p + at + 14);
				f->rate = ext80(p + at + 16);
				if (aifc && size >= 22 && at + 8 + 22 <= n &&
				    memcmp(p + at + 26, "NONE", 4) && memcmp(p + at + 26, "twos", 4)) {
					*why = "it is a compressed AIFF (EPlay plays PCM)";
					return 0;
				}
				have_fmt = 1;
			} else if (!memcmp(p + at, "SSND", 4) && at + 16 <= n) {
				if (!have_fmt || !fmt_ok(f, why))
					return 0;
				f->data = at + 16 + (long)be32(p + at + 8);
				return 1;
			}
		}
		*why = "its format or its samples weren't found";
		return 0;
	}
	return 0;
}

/* one sample, rounded to 16 bits (clamped at full scale) */
static short sample(const PCMFMT *f, const unsigned char *s)
{
	long v;
	switch (f->bits) {
	case 8:
		return (short)(f->sign8 ? (signed char)s[0] * 256 : (s[0] - 128) * 256);
	case 16:
		return (short)(f->big ? s[0] << 8 | s[1] : s[1] << 8 | s[0]);
	case 24:
		v = f->big ? (long)(signed char)s[0] * 65536 + ((long)s[1] << 8 | s[2])
			   : (long)(signed char)s[2] * 65536 + ((long)s[1] << 8 | s[0]);
		v = (v + 128) >> 8;
		break;
	default:
		v = f->big ? (long)(signed char)s[0] * 256 + s[1] : (long)(signed char)s[3] * 256 + s[2];
		v = v * 256 + (f->big ? s[2] : s[1]);	/* the top 24 bits are enough to round */
		v = (v + 128) >> 8;
		break;
	}
	return (short)(v > 32767 ? 32767 : v);
}

void pcm_convert(const PCMFMT *f, const unsigned char *src, short *dst, long n)
{
	int b = f->bits / 8;
	long i;

	static const unsigned short one = 1;
	if (f->channels == 2 && f->bits == 16 && f->big && !*(const unsigned char *)&one) {
		memcpy(dst, src, (size_t)n * 4);	/* (big-endian, as the Atari: as it is) */
		return;
	}
	/* 16 bits, the most there is: a loop of its own each, no call a sample
	   (the Atari's tick converts 49170 frames a second) */
	if (f->bits == 16) {
#if defined(__m68k__)
		if (f->channels == 2 && !f->big) {	/* a WAV on the Atari: a frame a long */
			const unsigned long *s = (const unsigned long *)(const void *)src;
			unsigned long *d = (unsigned long *)(void *)dst;
			long k = n;
			__asm__ volatile("\tbra.s\t2f\n"
					 "1:\tmove.l\t(%0)+,%%d0\n"
					 "\tror.w\t#8,%%d0\n"	/* right */
					 "\tswap\t%%d0\n"
					 "\tror.w\t#8,%%d0\n"	/* left */
					 "\tswap\t%%d0\n"
					 "\tmove.l\t%%d0,(%1)+\n"
					 "2:\tsubq.l\t#1,%2\n"
					 "\tbpl.s\t1b"
					 : "+a"(s), "+a"(d), "+d"(k) : : "d0", "cc", "memory");
			return;
		}
#endif
		if (f->channels == 2 && !f->big) {
			for (i = 0; i < n; i++, src += 4) {
				*dst++ = (short)(src[1] << 8 | src[0]);
				*dst++ = (short)(src[3] << 8 | src[2]);
			}
		} else if (f->channels == 2) {
			for (i = 0; i < n; i++, src += 4) {
				*dst++ = (short)(src[0] << 8 | src[1]);
				*dst++ = (short)(src[2] << 8 | src[3]);
			}
		} else {
			int lo = !f->big, hi = f->big;
			for (i = 0; i < n; i++, src += 2) {
				short v = (short)(src[lo] << 8 | src[hi]);
				*dst++ = v;
				*dst++ = v;
			}
		}
		return;
	}
	/* 24 bits stereo (a studio's files): its own loop too, as sample()
	   rounds them, the top 16 bits and one more where the lowest byte is
	   128 or more. Through sample(), a call each, a 24-bit AIFF at 44.1 kHz
	   crackled on Erez's Falcon (3 Nov 2026: 330 underruns in 6 s) */
	if (f->bits == 24 && f->channels == 2) {
		int hi = f->big ? 0 : 2, lo = f->big ? 2 : 0;
		for (i = 0; i < n; i++, src += 6) {
			long l = (long)(signed char)src[hi] * 256 + src[1] + (src[lo] >> 7);
			long r = (long)(signed char)src[3 + hi] * 256 + src[4] + (src[3 + lo] >> 7);
			*dst++ = (short)(l > 32767 ? 32767 : l);
			*dst++ = (short)(r > 32767 ? 32767 : r);
		}
		return;
	}
	for (i = 0; i < n; i++, src += f->bytes) {
		short l = sample(f, src);
		*dst++ = l;
		*dst++ = f->channels == 2 ? sample(f, src + b) : l;
	}
}
