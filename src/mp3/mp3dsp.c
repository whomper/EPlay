/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * mp3dsp.c - the C model of the DSP's part of an MP3 decode (mp3dsp.h):
 * scalefactors, Huffman codes, dequantising, stereo, reordering, alias
 * reduction, the inverse transform with its windows and overlap, and the
 * synthesis filter, as ISO/IEC 11172-3 has them, in the DSP56001's
 * arithmetic. Every place a value is rounded or limited is one the DSP
 * program has too; between them, sums are exact.
 */
#include <string.h>
#include "mp3dsp.h"
#include "mp3frame.h"
#include "mp3tab.h"

static inline dw sx(unsigned long w)
{
	return (dw)((long)(w ^ 0x800000UL) - 0x800000L);
}

/* ---- the main data, MSB first, from 24-bit words */
typedef struct {
	const unsigned long *w;
	long n, pos;
} BR;

static unsigned long bits(BR *b, int n)
{
	unsigned long v = 0;
	while (n-- > 0) {
		long wi = b->pos / 24;
		unsigned long word = wi < b->n ? b->w[wi] : 0;
		v = v << 1 | ((word >> (23 - b->pos % 24)) & 1);
		b->pos++;
	}
	return v;
}

static unsigned long peek(BR *b, int n)
{
	long at = b->pos;
	unsigned long v = bits(b, n);
	b->pos = at;
	return v;
}

/* one Huffman code from the table sel (t_hsel's word): its value, or -1 */
static int huff(BR *b, unsigned long sel)
{
	int n = (int)(sel >> 16) & 15;
	unsigned long off = sel & 0xffff, e;

	for (;;) {
		e = t_huff[off + peek(b, n)];
		if (e & 0x800000) {
			b->pos += n;
			n = (int)(e >> 16) & 15;
			off = e & 0xffff;
			continue;
		}
		if (!e)
			return -1;
		b->pos += (e >> 16) & 31;
		return (int)(e & 0xff);
	}
}

/* ---- one granule of one channel */
typedef struct {
	const unsigned long *g;		/* its packet words */
	int bt, mixed;
	int nl;				/* a mixed block's long bands: 8, or 6 in MPEG-2 */
	int sfl[22];			/* long scalefactors */
	int sfs[13][3];			/* short ones */
	int pl[22], ps[13][3];		/* as intensity positions (IS_BAD: none) */
	int is[576];
	dw xr[576];
} GC;

#define IS_BAD 99

/* MPEG-2's: four parts of nr_of_sfb scalefactors, slen bits each; in the
 * right channel of intensity stereo the largest value of a part is no
 * position */
static void scalefactors_lsf(BR *b, GC *c)
{
	int row = (int)(c->g[3] >> 16) & 7, blk = c->bt != 2 ? 0 : c->mixed ? 2 : 1;
	const unsigned long *nr = t_nsf + row * 12 + blk * 4;
	int part, k, at = 0, nlong = blk == 0 ? 22 : blk == 2 ? 6 : 0;

	for (part = 0; part < 4; part++) {
		int sl = (int)(c->g[3] >> (4 * part)) & 15, max = (1 << sl) - 1;
		for (k = 0; k < (int)nr[part]; k++, at++) {
			int v = (int)bits(b, sl), p = row >= 3 && sl && v == max ? IS_BAD : v;
			if (at < nlong) {
				c->sfl[at] = v;
				c->pl[at] = p;
			} else {
				int s = (blk == 2 ? 3 : 0) + (at - nlong) / 3, w = (at - nlong) % 3;
				c->sfs[s][w] = v;
				c->ps[s][w] = p;
			}
		}
	}
}

static void scalefactors(MP3DSP *d, BR *b, GC *c, int gr, int ch, int lsf)
{
	int s1 = (int)(c->g[3] & 15), s2 = (int)(c->g[3] >> 4);
	int scfsi = gr ? (int)(c->g[4] >> 8) & 15 : 0;
	int s, w, sfb;

	memset(c->sfl, 0, sizeof(c->sfl));
	memset(c->sfs, 0, sizeof(c->sfs));
	memset(c->pl, 0, sizeof(c->pl));
	memset(c->ps, 0, sizeof(c->ps));
	if (lsf) {
		scalefactors_lsf(b, c);
		return;
	}
	if (c->bt == 2) {
		s = 0;
		if (c->mixed) {
			for (sfb = 0; sfb < 8; sfb++)
				c->sfl[sfb] = (int)bits(b, s1);
			s = 3;
		}
		for (; s < 12; s++)
			for (w = 0; w < 3; w++)
				c->sfs[s][w] = (int)bits(b, s < 6 ? s1 : s2);
	} else {
		static const unsigned char grp[5] = { 0, 6, 11, 16, 21 };
		int k;
		for (k = 0; k < 4; k++)
			for (sfb = grp[k]; sfb < grp[k + 1]; sfb++)
				c->sfl[sfb] = (scfsi & (8 >> k)) ? d->scf[ch][sfb] :
					      (int)bits(b, k < 2 ? s1 : s2);
	}
	if (gr == 0)
		for (sfb = 0; sfb < 22; sfb++)
			d->scf[ch][sfb] = (unsigned char)c->sfl[sfb];
	memcpy(c->pl, c->sfl, sizeof(c->pl));
	memcpy(c->ps, c->sfs, sizeof(c->ps));
}

static void huffman(BR *b, GC *c, long end)
{
	int bv = (int)c->g[1], r1 = (int)c->g[6], r2 = (int)c->g[7];
	int ts[3], i, k, v;
	unsigned long sel;

	ts[0] = (int)(c->g[5] & 31);
	ts[1] = (int)(c->g[5] >> 5) & 31;
	ts[2] = (int)(c->g[5] >> 10) & 31;
	memset(c->is, 0, sizeof(c->is));
	for (i = 0; i < bv; i += 2) {
		int x, y, lin;
		sel = t_hsel[ts[i < r1 ? 0 : i < r2 ? 1 : 2]];
		if (!sel)
			continue;
		lin = (int)(sel >> 20);
		if ((v = huff(b, sel)) < 0)
			break;
		x = v >> 4;
		y = v & 15;
		if (x == 15 && lin)
			x += (int)bits(b, lin);
		if (x && bits(b, 1))
			x = -x;
		if (y == 15 && lin)
			y += (int)bits(b, lin);
		if (y && bits(b, 1))
			y = -y;
		c->is[i] = x;
		c->is[i + 1] = y;
	}
	sel = t_hsel[32 + ((c->g[4] >> 5) & 1)];
	while (i <= 572 && b->pos < end) {
		int q[4];
		if ((v = huff(b, sel)) < 0)
			break;
		for (k = 0; k < 4; k++) {
			q[k] = (v >> (3 - k)) & 1;
			if (q[k] && bits(b, 1))
				q[k] = -1;
		}
		if (b->pos > end)
			break;		/* the last one ran over: it isn't there */
		for (k = 0; k < 4; k++)
			c->is[i + k] = q[k];
		i += 4;
	}
	b->pos = end;
}

/* |v|^(4/3) 2^(q/4), as value / 2^MP3_XS */
static dw dequant(int v, int q, long *sat)
{
	int n = v < 0 ? -v : v, e, k, s;
	dw m, p, x;

	if (!n)
		return 0;
	if (n < 1024) {
		m = sx(t_pow43m[n]);
		e = sx(t_pow43e[n]);
	} else {
		int a = n >> 3, f = n & 7;
		dw b0 = sx(t_pow43b[a - 128]), b1 = sx(t_pow43b[a + 1 - 128]);
		m = d_rw(d_w2a(b0) + d_mpy(b1 - b0, (dw)f << 20));
		e = 18;
	}
	k = q >> 2;
	p = d_rw(d_mpy(m, sx(t_gain[q & 3])));
	e += k + 1 - MP3_XS;
	if (e >= 0) {
		da a = d_asl(d_w2a(p), e > 8 ? 8 : e);
		x = d_a2w(a);
		if ((a >> 24) > DW_MAX)
			(*sat)++;
	} else {
		s = -e;
		x = s > 22 ? 0 : d_rw(d_asl(d_mpy(p, (dw)(0x400000L >> s)), 1));
	}
	return v < 0 ? -x : x;
}

static void requantise(GC *c, int rate, long *sat)
{
	const unsigned long *sl = t_sfbl + rate * 23, *ss = t_sfbs + rate * 14;
	int gg = (int)c->g[2] - 210, sc = 2 * (1 + (int)((c->g[4] >> 3) & 1));
	int pf = (int)(c->g[4] >> 4) & 1, sfb, s, w, j, i;

	if (c->bt != 2 || c->mixed) {
		int last = c->bt == 2 ? c->nl : 22;
		for (sfb = 0; sfb < last; sfb++) {
			int q = gg - sc * (c->sfl[sfb] + pf * (int)t_pretab[sfb]);
			for (i = (int)sl[sfb]; i < (int)sl[sfb + 1]; i++)
				c->xr[i] = dequant(c->is[i], q, sat);
		}
		if (c->bt != 2)
			return;
	}
	for (s = c->mixed ? 3 : 0; s < 13; s++) {
		int wd = (int)(ss[s + 1] - ss[s]), base = 3 * (int)ss[s];
		for (w = 0; w < 3; w++) {
			int q = gg - 8 * (int)((c->g[8] >> (3 * w)) & 7) - sc * c->sfs[s][w];
			for (j = 0; j < wd; j++) {
				i = base + w * wd + j;
				c->xr[i] = dequant(c->is[i], q, sat);
			}
		}
	}
}

/* ---- stereo, in the order the lines are coded */
static void ms(dw *l, dw *r, int from, int to)
{
	dw c = sx(t_misc[0]);
	int i;
	for (i = from; i < to; i++) {
		da m = d_mpy(l[i], c), s = d_mpy(r[i], c);
		l[i] = d_rw(m + s);
		r[i] = d_rw(m - s);
	}
}

static void stereo(GC *c0, GC *c1, unsigned long flags, int rate)
{
	int lsf = (flags & PK_LSF) != 0, sh = (int)(c1->g[4] >> 6) & 1;
	const unsigned long *sl = t_sfbl + rate * 23, *ss = t_sfbs + rate * 14;
	int start[40], width[40], pos[40], nb = 0, ml, mb[3], i, j, nlong = 0;

	if (!(flags & PK_IS)) {
		if (flags & PK_MS)
			ms(c0->xr, c1->xr, 0, 576);
		return;
	}
	/* the bands, as the left channel has them, and the right's positions */
	if (c0->bt != 2 || c0->mixed) {
		nlong = c0->bt == 2 ? c0->nl : 22;
		for (i = 0; i < nlong; i++, nb++) {
			start[nb] = (int)sl[i];
			width[nb] = (int)(sl[i + 1] - sl[i]);
			pos[nb] = c1->pl[i];
		}
	}
	if (c0->bt == 2)
		for (i = c0->mixed ? 3 : 0; i < 13; i++) {
			int wd = (int)(ss[i + 1] - ss[i]), w;
			for (w = 0; w < 3; w++, nb++) {
				start[nb] = 3 * (int)ss[i] + w * wd;
				width[nb] = wd;
				pos[nb] = c1->ps[i][w];
			}
		}
	/* the highest band of each window where the right has a line */
	mb[0] = mb[1] = mb[2] = -1;
	for (i = 0; i < nb; i++)
		for (j = 0; j < width[i]; j++)
			if (c1->xr[start[i] + j]) {
				mb[i % 3] = i;
				break;
			}
	if (nlong) {
		ml = mb[0] > mb[1] ? mb[0] : mb[1];
		ml = ml > mb[2] ? ml : mb[2];
		mb[0] = mb[1] = mb[2] = ml;
	}
	/* the top band has no scalefactor of its own */
	{
		int nw = c0->bt == 2 ? 3 : 1, w;
		for (w = 0; w < nw; w++) {
			int top = nb - nw + w, prev = top - nw;
			pos[top] = mb[w] >= prev ? (lsf ? 0 : 3) : pos[prev];
		}
	}
	for (i = 0; i < nb; i++) {
		if (i > mb[i % 3] && (lsf ? pos[i] != IS_BAD : pos[i] < 7)) {
			dw kl, kr;
			if (lsf) {
				dw k = sx(t_lsfis[((pos[i] + 1) >> 1) << sh]);
				kl = (pos[i] & 1) ? k : DW_MAX;
				kr = (pos[i] & 1) ? DW_MAX : k;
			} else {
				kl = sx(t_istereo[pos[i]]);
				kr = sx(t_istereo[7 + pos[i]]);
			}
			for (j = start[i]; j < start[i] + width[i]; j++) {
				dw x = c0->xr[j];
				c1->xr[j] = d_rw(d_mpy(x, kr));
				c0->xr[j] = d_rw(d_mpy(x, kl));
			}
		} else if (flags & PK_MS)
			ms(c0->xr, c1->xr, start[i], start[i] + width[i]);
	}
}

/* ---- reorder, alias reduction, the transform, into 32 x 18 subband samples */
static void reorder(GC *c, int rate)
{
	const unsigned long *ss = t_sfbs + rate * 14;
	dw t[576];
	int s, w, j;

	if (c->bt != 2)
		return;
	memcpy(t, c->xr, sizeof(t));
	for (s = c->mixed ? 3 : 0; s < 13; s++) {
		int wd = (int)(ss[s + 1] - ss[s]), base = 3 * (int)ss[s];
		for (w = 0; w < 3; w++)
			for (j = 0; j < wd; j++)
				c->xr[base + 3 * j + w] = t[base + w * wd + j];
	}
}

static void alias(GC *c)
{
	int nb = c->bt == 2 ? (c->mixed ? 1 : 0) : 31, sb, i;
	for (sb = 0; sb < nb; sb++)
		for (i = 0; i < 8; i++) {
			dw cs = sx(t_alias[i]), ca = sx(t_alias[8 + i]);
			dw *u = &c->xr[18 * sb + 17 - i], *l = &c->xr[18 * sb + 18 + i];
			dw bu = *u, bd = *l;
			*u = d_rw(d_mpy(bu, cs) - d_mpy(bd, ca));
			*l = d_rw(d_mpy(bd, cs) + d_mpy(bu, ca));
		}
}

/* where each of the 36 outputs comes from among the 18 rows worked out,
 * and its sign; the same for the 12 of a short block */
static const signed char map36[36] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, -9, -8, -7, -6, -5, -4, -3, -2, -1,
	10, 11, 12, 13, 14, 15, 16, 17, 18, 18, 17, 16, 15, 14, 13, 12, 11, 10
};
static const signed char map12[12] = { 1, 2, 3, -3, -2, -1, 4, 5, 6, 6, 5, 4 };

static void imdct(GC *c, dw *ov, dw out[32][18])
{
	int sb, i, k, r, w;
	for (sb = 0; sb < 32; sb++) {
		const dw *x = &c->xr[18 * sb];
		da z[36];
		memset(z, 0, sizeof(z));
		if (c->bt == 2 && !(c->mixed && sb < 2)) {
			for (w = 0; w < 3; w++) {
				dw u[6];
				for (r = 0; r < 6; r++) {
					da a = 0;
					for (k = 0; k < 6; k++)
						a += d_mpy(x[3 * k + w], sx(t_imdct12[r * 6 + k]));
					u[r] = d_rw(a);
				}
				for (i = 0; i < 12; i++) {
					int m = map12[i];
					dw win = sx(t_win[2 * 36 + i]);
					z[6 + 6 * w + i] += d_w2a(d_rw(d_mpy(u[(m < 0 ? -m : m) - 1], m < 0 ? -win : win)));
				}
			}
		} else {
			int bt = c->bt == 2 ? 0 : c->bt;
			dw u[18];
			for (r = 0; r < 18; r++) {
				da a = 0;
				for (k = 0; k < 18; k++)
					a += d_mpy(x[k], sx(t_imdct36[r * 18 + k]));
				u[r] = d_rw(a);
			}
			for (i = 0; i < 36; i++) {
				int m = map36[i];
				dw win = sx(t_win[bt * 36 + i]);
				z[i] = d_w2a(d_rw(d_mpy(u[(m < 0 ? -m : m) - 1], m < 0 ? -win : win)));
			}
		}
		for (i = 0; i < 18; i++) {
			dw o = d_a2w(d_w2a(ov[18 * sb + i]) + z[i]);
			if ((sb & 1) && (i & 1))
				o = d_a2w(-d_w2a(o));	/* frequency inversion */
			out[sb][i] = o;
			ov[18 * sb + i] = d_a2w(z[18 + i]);
		}
	}
}

/* ---- the synthesis filter: 18 slots of 32 subband samples, 576 out */
static void synth(MP3DSP *d, int ch, dw sub[32][18], dw *pcm)
{
	int t, r, k, j, i;
	for (t = 0; t < 18; t++) {
		dw *a;
		d->vpos[ch] = (d->vpos[ch] + 1) & 15;
		a = d->v[ch][d->vpos[ch]];
		for (r = 0; r < 32; r++) {
			da acc = 0;
			for (k = 0; k < 32; k++)
				acc += d_mpy(sub[k][t], sx(t_synth[r * 32 + k]));
			a[r] = d_rw(acc);
		}
		for (j = 0; j < 32; j++) {
			da acc = 0;
			for (i = 0; i < 16; i++) {
				const dw *s = d->v[ch][(d->vpos[ch] - i) & 15];
				dw dh = sx(t_dwin[j + 32 * i]), val;
				int neg = 0;
				if (!(i & 1)) {	/* row j of an even slot */
					if (j < 16)
						val = s[j];
					else if (j == 16)
						continue;
					else
						val = s[32 - j], neg = 1;
				} else {	/* row 32 + j of an odd one */
					if (j == 0)
						val = s[0], neg = 1;
					else if (j <= 16)
						val = s[16 + j - 1];
					else
						val = s[16 + 31 - j];
				}
				acc += d_mpy(val, neg ? -dh : dh);
			}
			pcm[32 * t + j] = d_a2w_out(d_rnd(d_asl(acc, MP3_XS + 1)));
		}
	}
}

void mp3dsp_reset(MP3DSP *d)
{
	memset(d, 0, sizeof(*d));
}

int mp3dsp_frame(MP3DSP *d, const unsigned long *pk, dw pcm[2][1152], int *ns)
{
	static GC gc[2];
	static dw sub[32][18];
	unsigned long flags = pk[0];
	int nch = (flags & PK_STEREO) ? 2 : 1, rate = PK_RATE(flags), gr, ch;
	int lsf = (flags & PK_LSF) != 0, ngr = lsf ? 1 : 2;
	BR b;

	*ns = 576 * ngr;
	if (flags & PK_MUTE) {
		memset(d->ov, 0, sizeof(d->ov));
		memset(d->v, 0, sizeof(d->v));
		memset(pcm, 0, sizeof(dw) * 2 * 1152);
		return nch;
	}
	b.w = pk + 2 + ngr * nch * PK_GC;
	b.n = (long)pk[1];
	b.pos = 0;
	for (gr = 0; gr < ngr; gr++) {
		for (ch = 0; ch < nch; ch++) {
			GC *c = &gc[ch];
			long end;
			c->g = pk + 2 + (gr * nch + ch) * PK_GC;
			c->bt = (int)(c->g[4] & 3);
			c->mixed = (int)(c->g[4] >> 2) & 1;
			c->nl = lsf ? 6 : 8;
			end = b.pos + (long)c->g[0];
			scalefactors(d, &b, c, gr, ch, lsf);
#ifdef MP3DSP_HUFF_HOOK
			{
				long at = b.pos;
				huffman(&b, c, end);
				mp3dsp_huff_hook(c->g, b.w, b.n, at, end, c->is);
			}
#else
			huffman(&b, c, end);
#endif
			requantise(c, rate, &d->sat);
		}
		if (nch == 2)
			stereo(&gc[0], &gc[1], flags, rate);
		for (ch = 0; ch < nch; ch++) {
			reorder(&gc[ch], rate);
			alias(&gc[ch]);
			imdct(&gc[ch], d->ov[ch], sub);
			synth(d, ch, sub, &pcm[ch][576 * gr]);
		}
	}
	return nch;
}
