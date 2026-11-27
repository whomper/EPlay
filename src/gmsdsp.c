/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * gmsdsp.c - the internal synth on the Falcon's DSP (gmsdsp.h): the
 * blocks the 68030 writes into the sound DMA's ring, and the DSP's
 * arithmetic on them, its C model (dsp/gms.a56 is held to it bit for bit).
 * Integers alone, host-testable.
 */
#include "gmsdsp.h"

void gmsd_points8(const GMSET *g, const unsigned char *pool, signed char *pts8)
{
	const short *p = (const short *)(const void *)(pool + g->o_smp);
	long k;
	/* (the points halved by gms_prepare: 15 bits; their top 8) */
	for (k = 0; k < g->points; k++)
		pts8[k] = (signed char)(p[k] >> 7);
}

static long clamp_gain(long g)
{
	return g < 0 ? 0 : g > (32767L << 15) ? 32767L << 15 : g;
}

int gmsd_write(GMSYN *s, const unsigned char *pool, const signed char *pts8, unsigned short *w)
{
	const short *base = (const short *)(const void *)(pool + s->g->o_smp);
	int nw = 2, nv = 0, k;
	gms_block_ctl(s, GMSD_N);
	for (k = 0; k < s->voices; k++) {
		GMS_VOICE *v = &s->v[k];
		unsigned long end, sfrac, sinc;
		long np, gl, gr, first, j, span;
		int looped, dec = 0;
		if (!v->on)
			continue;
		looped = v->le > v->ls;
		span = v->le - v->ls;
		end = (v->frac & 0xffffUL) + v->inc * GMSD_N;
		gl = clamp_gain(v->gl + v->dgl * GMSD_N);
		gr = clamp_gain(v->gr + v->dgr * GMSD_N);
		first = (long)((const short *)(const void *)v->w - base);
		/* the points the block reaches; where more than the DSP holds,
		   every 2^dec-th of them, the step and the place in those */
		sfrac = v->frac & 0xffffUL;
		sinc = v->inc;
		np = (long)(end >> 16) + 2;
		while (np > GMSD_PTS && dec < 3) {
			dec++;
			sinc = v->inc >> dec;
			sfrac = (((unsigned long)(v->pos & ((1L << dec) - 1)) << 16) | (v->frac & 0xffffUL)) >> dec;
			np = (long)((sfrac + sinc * GMSD_N) >> 16) + 2;
		}
		if (np <= GMSD_PTS && nv < GMSD_VOICES && nw + GMSD_HEAD + (np + 1) / 2 <= GMSD_WORDS) {
			unsigned short *h = w + nw;
			long at = v->pos & ~((1L << dec) - 1);
			h[0] = (unsigned short)k;
			h[1] = (unsigned short)(sfrac & 0xffffUL);
			h[2] = (unsigned short)(sinc >> 16);
			h[3] = (unsigned short)(sinc & 0xffffUL);
			h[4] = (unsigned short)(gl >> 15);
			h[5] = (unsigned short)(gr >> 15);
			h[6] = (unsigned short)np;
			/* (the points the block reaches, the loop unrolled; past a
			   sample's end with no loop, silence; two a word, each
			   word written once: w is the DMA's ring, in ST RAM, slow
			   for a 68030 to read back) */
			{
				unsigned short *o = w + nw + GMSD_HEAD;
				const signed char *q = pts8 + first;
				long i = at, st = 1L << dec, le = v->le, len = looped ? le : v->len, todo = np;
				if (looped)
					while (i >= le)
						i -= span;
				if (st == 1 && i + np <= len) {
					/* (most blocks: a point a step, no loop's end or
					   sample's in it: the points as they are, a word
					   at a time where they start on one) */
					const unsigned char *u = (const unsigned char *)q + i;
					long nh = np >> 1;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
					if (!((unsigned long)u & 1)) {	/* (a 68k's words: the first byte high) */
						const unsigned short *u2 = (const unsigned short *)(const void *)u;
						for (j = 0; j < nh; j++)
							*o++ = *u2++;
					} else
#endif
						for (j = 0; j < nh; j++, u += 2)
							*o++ = (unsigned short)((unsigned)u[0] << 8 | u[1]);
					if (np & 1)
						*o = (unsigned short)((unsigned)((const unsigned char *)q)[i + np - 1] << 8);
					todo = 0;	/* (done: the loop below passed over) */
				}
				for (j = 0; j < todo; j += 2) {
					unsigned hi, lo = 0;
					/* (the DMA's words big-endian: the first point high) */
					hi = i < len ? (unsigned char)q[i] : 0;
					i += st;
					if (looped)
						while (i >= le)
							i -= span;
					if (j + 1 < np) {
						lo = i < len ? (unsigned char)q[i] : 0;
						i += st;
						if (looped)
							while (i >= le)
								i -= span;
					}
					*o++ = (unsigned short)(hi << 8 | lo);
				}
			}
			nw += GMSD_HEAD + (int)((np + 1) / 2);
			nv++;
		}
		/* its place and gains as the block leaves them */
		v->pos += (long)(end >> 16);
		v->frac = end & 0xffffUL;
		v->gl = gl;
		v->gr = gr;
		if (v->pos >= v->le) {
			if (!looped) {
				gms_voice_off(s, v);
				continue;
			}
			while (v->pos >= v->le)
				v->pos -= span;
		}
	}
	w[0] = GMSD_MAGIC;
	w[1] = (unsigned short)nv;
	s->to_upd -= GMSD_N;
	return nw;
}

/* (the DSP's model: the tests' and the host's alone, not SYNTH.OVL's,
   whose 68060 code has no 64-bit multiply) */
#ifndef SYNTHOVL_VERSION
void gmsd_dsp_reset(GMSD_DSP *d)
{
	int k;
	for (k = 0; k < GMSD_VOICES; k++) {
		d->gl[k] = d->gr[k] = 0;
		d->seen[k] = 0;
	}
}

static long sat24(long long x)
{
	return x > 8388607 ? 8388607 : x < -8388608 ? -8388608 : (long)x;
}

/* (a >> b of a signed value: the DSP's a1, floor) */
static long long fl(long long a, int b)
{
	return a >= 0 ? a >> b : -((-a + (1LL << b) - 1) >> b);
}

void gmsd_dsp(GMSD_DSP *d, const unsigned short *w, long *lr)
{
	long L[GMSD_N], R[GMSD_N], P[GMSD_PTS];
	unsigned char now[GMSD_VOICES];
	int nv = w[0] == GMSD_MAGIC ? w[1] : 0, p = 2, v, k;
	for (k = 0; k < GMSD_N; k++)
		L[k] = R[k] = 0;
	for (k = 0; k < GMSD_VOICES; k++)
		now[k] = 0;
	if (nv > GMSD_VOICES)
		nv = 0;
	for (v = 0; v < nv; v++) {
		int id = w[p] & (GMSD_VOICES - 1), np = w[p + 6], j;
		long gl = d->seen[id] ? d->gl[id] : 0, gr = d->seen[id] ? d->gr[id] : 0;
		long tl = (long)w[p + 4] << 8, tr = (long)w[p + 5] << 8, sl, sr;
		long long ph = (long long)w[p + 1] << 8, inc = ((long long)w[p + 2] << 24) | ((long long)w[p + 3] << 8);
		if (np > GMSD_PTS)
			np = GMSD_PTS;
		if (np < 2)
			np = 2;		/* (a damaged block's: the DSP's the same) */
		for (j = 0; j < np; j++) {
			unsigned short ww = w[p + GMSD_HEAD + (j >> 1)];
			signed char b = (signed char)((j & 1) ? ww & 0xff : ww >> 8);
			P[j] = (long)b * 32768L;	/* (Q23, halved: two points' difference fits) */
		}
		/* the gains: a step each GMSD_CHUNK samples, from where the last
		   block left them to its end (the CPU's engine's way too) */
		sl = (long)fl((long long)tl - gl, 2);
		sr = (long)fl((long long)tr - gr, 2);
		for (k = 0; k < GMSD_N; k++) {
			long idx = (long)(ph >> 24), f = (long)((ph & 0xffffff) >> 1), s0, s1, x;
			if (idx + 1 >= np)
				idx = np - 2;	/* (gmsd_write's count holds it within) */
			s0 = P[idx];
			s1 = P[idx + 1];
			x = s0 + (long)fl((long long)(s1 - s0) * f, 23);
			L[k] = sat24((long long)L[k] + fl((long long)x * gl, 23));
			R[k] = sat24((long long)R[k] + fl((long long)x * gr, 23));
			if (k % GMSD_CHUNK == GMSD_CHUNK - 1) {
				gl += sl;
				gr += sr;
			}
			ph += inc;
		}
		d->gl[id] = tl;
		d->gr[id] = tr;
		now[id] = 1;
		p += GMSD_HEAD + (np + 1) / 2;
	}
	for (k = 0; k < GMSD_VOICES; k++) {
		d->seen[k] = now[k];
		if (!now[k])
			d->gl[k] = d->gr[k] = 0;
	}
	for (k = 0; k < GMSD_N; k++) {
		lr[2 * k] = L[k];
		lr[2 * k + 1] = R[k];
	}
}

void gmsd_up(long *prev, const long *lr, long *out)
{
	int j, c;
	for (j = 0; j < GMSD_N; j++)
		for (c = 0; c < 2; c++) {
			long a = lr[2 * j + c], m = (long)fl((long long)prev[c] + a, 1);
			out[4 * j + c] = sat24(2LL * m);
			out[4 * j + 2 + c] = sat24(2LL * a);
			prev[c] = a;
		}
}
#endif
