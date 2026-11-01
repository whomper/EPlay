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

#ifdef MP3DSP_DEBUG
dw mp3dsp_dbg_xr[2][576], mp3dsp_dbg_sub[576];
int mp3dsp_dbg_is[576];
long mp3dsp_dbg_at;
dw mp3dsp_dbg_rq[576];
int mp3dsp_dbg_gr, mp3dsp_dbg_ch;	/* the granule and channel the stages are kept of */
dw mp3dsp_dbg_st[2][576], mp3dsp_dbg_ro[576], mp3dsp_dbg_al[576];
dw mp3dsp_dbg_sf[61];		/* SFL then SFS, as the DSP keeps them */
#endif

static inline dw sx(unsigned long w)
{
	return (dw)((long)(w ^ 0x800000UL) - 0x800000L);
}

/* ---- the main data, MSB first, 23 bits a word (mp3frame.h) */
typedef struct {
	const unsigned long *w;
	long n, pos;
} BR;

static unsigned long bits(BR *b, int n)
{
	unsigned long v = 0;
	while (n-- > 0) {
		long wi = b->pos / 23;
		unsigned long word = wi < b->n ? b->w[wi] : 0;
		v = v << 1 | ((word >> (22 - b->pos % 23)) & 1);
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

/* |v|^(4/3) 2^(q/4), as value / 2^MP3_XS; the sign is put on before the
 * value is limited to a word, as the DSP does it */
static dw dequant(int v, int q, long *sat)
{
	int n = v < 0 ? -v : v, e, k, s;
	dw m, p;
	da x;

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
		x = d_asl(d_w2a(p), e > 8 ? 8 : e);
		if ((x >> 24) > DW_MAX)
			(*sat)++;
	} else {
		s = -e;
		x = s > 22 ? 0 : d_rnd(d_asl(d_mpy(p, (dw)(0x400000L >> s)), 1));
	}
	return d_a2w(v < 0 ? -x : x);
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

/* a 9-point DCT, o[m] = sum v[j] cos(pi j (2m+1) / 18). Outputs m and
 * 8 - m share their sums of the even (e) and the odd (o) inputs, as e + o
 * and e - o (on the DSP add and subl, exact); the middle one is
 * v0 - v2 + v4 - v6 + v8 */
static void dct9(const dw *v, dw *o)
{
	int m, j;
	for (m = 0; m < 4; m++) {
		const unsigned long *k = &t_im9[8 * m];
		da a = d_w2a(v[0]), b = 0;
		for (j = 0; j < 4; j++)
			a += d_mpy(v[2 + 2 * j], sx(k[j]));
		for (j = 0; j < 4; j++)
			b += d_mpy(v[1 + 2 * j], sx(k[4 + j]));
		b += a;
		a = d_asl(a, 1) - b;
		o[m] = d_rw(b);
		o[8 - m] = d_rw(a);
	}
	o[4] = d_a2w(d_w2a(v[0]) - d_w2a(v[2]) + d_w2a(v[4]) - d_w2a(v[6]) + d_w2a(v[8]));
}

/* a long block's 18 rows (u[r] = sum x[k] cos(pi/72 (2r+19)(2k+1)), rows
 * 0-8 then 18-26) the fast way: the lines' sums c and differences s, in
 * pairs; a 9-point DCT of each; then a rotation of the two DCTs' outputs
 * 8 - r into rows r and 18 + r. Every factor is at most 1, and nothing on
 * the way gets bigger than the rows themselves (hot.mp3, a clipped master) */
static void rows36(const dw *x, dw u[18])
{
	dw c[9], s[9], cd[9], sd[9];
	int i, r;
	c[0] = x[0];
	for (i = 0; i < 8; i++) {
		da p = d_w2a(x[1 + 2 * i]), q = d_w2a(x[2 + 2 * i]);
		c[1 + i] = d_a2w(p + q);
		s[8 - i] = d_a2w(p - q);
	}
	s[0] = x[17];
	dct9(c, cd);
	dct9(s, sd);
	for (r = 0; r < 9; r++) {
		const unsigned long *t = &t_rot[4 * r];
		u[r] = d_rw(d_mpy(sd[8 - r], sx(t[0])) + d_mpy(cd[8 - r], sx(t[1])));
		u[9 + r] = d_rw(d_mpy(sd[8 - r], sx(t[2])) + d_mpy(cd[8 - r], sx(t[3])));
	}
}

/* a 3-point DCT, o[m] = sum v[j] cos(pi j (2m+1) / 6): v0 + v1 cos(pi/6)
 * + v2/2, v0 - v2, v0 - v1 cos(pi/6) + v2/2 */
static void dct3(const dw *v, dw *o)
{
	da e = d_w2a(v[0]) + d_mpy(v[2], sx(t_im3[0])), b = d_mpy(v[1], sx(t_im3[1])) + e;
	da a = d_asl(e, 1) - b;
	o[0] = d_rw(b);
	o[1] = d_a2w(d_w2a(v[0]) - d_w2a(v[2]));
	o[2] = d_rw(a);
}

/* a short window, the same way as a long block's: its 6 lines' sums and
 * differences, a 3-point DCT of each, and each of its 12 contributions
 * (the 12-point transform's 6 rows u, as u0 u1 u2 -u2 -u1 -u0 u3 u4 u5
 * u5 u4 u3, times the window) from one pair of the DCTs' outputs, in t_sh's order: C[i] for
 * i = 2 - m, 3 + m, then 8 - m, 9 + m (m = 0..2) */
static const signed char sh_at[12] = { 2, 3, 1, 4, 0, 5, 8, 9, 7, 10, 6, 11 };

/* where each of the 36 outputs comes from among the 18 rows worked out,
 * and its sign */
static const signed char map36[36] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, -9, -8, -7, -6, -5, -4, -3, -2, -1,
	10, 11, 12, 13, 14, 15, 16, 17, 18, 18, 17, 16, 15, 14, 13, 12, 11, 10
};

static void imdct(GC *c, dw *ov, dw out[32][18])
{
	int sb, i, w;
	for (sb = 0; sb < 32; sb++) {
		const dw *x = &c->xr[18 * sb];
		da z[36];
		memset(z, 0, sizeof(z));
		if (c->bt == 2 && !(c->mixed && sb < 2)) {
			for (w = 0; w < 3; w++) {
				const dw *l = x + w;	/* its lines, a step of 3 */
				dw cs[6], cd[3], sd[3];
				cs[0] = l[0];
				cs[1] = d_a2w(d_w2a(l[3]) + d_w2a(l[6]));
				cs[2] = d_a2w(d_w2a(l[9]) + d_w2a(l[12]));
				cs[3] = l[15];
				cs[4] = d_a2w(d_w2a(l[9]) - d_w2a(l[12]));
				cs[5] = d_a2w(d_w2a(l[3]) - d_w2a(l[6]));
				dct3(cs, cd);
				dct3(cs + 3, sd);
				for (i = 0; i < 12; i++) {
					const unsigned long *t = &t_sh[(sb & 1) * 24 + 2 * i];
					int m = (i % 6) / 2;
					z[6 + 6 * w + sh_at[i]] += d_w2a(d_rw(d_mpy(cd[m], sx(t[0])) +
									      d_mpy(sd[m], sx(t[1]))));
				}
			}
		} else {
			int bt = c->bt == 2 ? 0 : c->bt;
			dw u[18];
			rows36(x, u);
			/* the first 18 are rounded with the overlap they go to,
			 * as one rounding (the DSP's macr); z holds them minus it */
			for (i = 0; i < 36; i++) {
				int m = map36[i];
				dw win = sx(t_win[(sb & 1) * 144 + bt * 36 + i]);
				da p = d_mpy(u[(m < 0 ? -m : m) - 1], m < 0 ? -win : win);
				if (i < 18)
					z[i] = d_rnd(d_w2a(ov[18 * sb + i]) + p) - d_w2a(ov[18 * sb + i]);
				else
					z[i] = d_w2a(d_rw(p));
			}
		}
		for (i = 0; i < 18; i++) {
			/* (an odd subband's windows have their odd samples
			 * negated: the frequency inversion, which its
			 * overlap then holds too) */
			out[sb][i] = d_a2w(d_w2a(ov[18 * sb + i]) + z[i]);
			ov[18 * sb + i] = d_a2w(z[18 + i]);
		}
	}
}

/* ---- the synthesis filter: 18 slots of 32 subband samples, 576 out.
 * Each slot's V[0..63] (ISO/IEC 11172-3's) comes from the 32-point DCT of
 * its samples, X[n] = sum x[k] cos(pi (2k+1) n / 64): V[i] = X[16 + i]
 * for i < 16, 0 for i = 16, then -X[48 - i] up to 32 and -X[i - 48] from
 * 48, -X[48 - (i - 48)]... as below. The DCT is a butterfly (e, o), a
 * second on e (e2, o2), then a 16 x 16 matrix (odd n, from o) and two 8 x 8
 * ones (n = 4m from e2, 4m + 2 from o2). Then each sample is the window's 16 taps over the
 * last 16 slots, as the standard has them */
static void synth(MP3DSP *d, int ch, dw sub[32][18], dw *pcm)
{
	int t, n, k, j, i;
	for (t = 0; t < 18; t++) {
		dw e[16], o[16], e2[8], o2[8], *v;
		const unsigned long *c;
		for (k = 0; k < 16; k++) {
			da a = d_w2a(sub[k][t]), b = d_w2a(sub[31 - k][t]);
			e[k] = d_a2w(a + b);
			o[k] = d_a2w(a - b);
		}
		d->vpos[ch] = (d->vpos[ch] + 1) & 15;
		v = d->v[ch][d->vpos[ch]];
		for (k = 0; k < 8; k++) {	/* e's own butterfly */
			da a = d_w2a(e[k]), b = d_w2a(e[15 - k]);
			e2[k] = d_a2w(a + b);
			o2[k] = d_a2w(a - b);
		}
		c = t_synth;
		for (n = 0; n < 32; n++) {
			const dw *in = (n & 1) ? o : (n & 2) ? o2 : e2;
			int len = (n & 1) ? 16 : 8;
			da acc = 0, r;
			for (k = 0; k < len; k++)
				acc += d_mpy(in[k], sx(c[k]));
			c += len;
			r = d_rnd(acc);
			if (n < 16) {		/* X[n]: V[48 + n] and V[48 - n], minus */
				v[48 + n] = v[48 - n] = d_a2w(-r);
			} else {		/* V[n - 16], and minus at V[48 - n] */
				v[n - 16] = d_a2w(r);
				v[48 - n] = d_a2w(-r);
			}
		}
		v[16] = 0;
		for (j = 0; j < 32; j++) {
			da acc = 0;
			for (i = 0; i < 16; i++)
				acc += d_mpy(d->v[ch][(d->vpos[ch] - i) & 15][j + 32 * (i & 1)],
					     sx(t_dwin[j + 32 * i]));
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
#ifdef MP3DSP_DEBUG
			if (gr == mp3dsp_dbg_gr && ch == mp3dsp_dbg_ch)
				mp3dsp_dbg_at = b.pos;
#endif
#ifdef MP3DSP_HUFF_HOOK
			{
				long at = b.pos;
				huffman(&b, c, end);
				mp3dsp_huff_hook(c->g, b.w, b.n, at, end, c->is);
			}
#else
			huffman(&b, c, end);
#endif
#ifdef MP3DSP_DEBUG
			if (gr == mp3dsp_dbg_gr && ch == mp3dsp_dbg_ch)
				memcpy(mp3dsp_dbg_is, c->is, sizeof(c->is));
#endif
			requantise(c, rate, &d->sat);
#ifdef MP3DSP_DEBUG
			if (gr == mp3dsp_dbg_gr && ch == mp3dsp_dbg_ch) {
				int q, w;
				memcpy(mp3dsp_dbg_rq, c->xr, sizeof(c->xr));
				for (q = 0; q < 22; q++)
					mp3dsp_dbg_sf[q] = c->sfl[q];
				for (q = 0; q < 13; q++)
					for (w = 0; w < 3; w++)
						mp3dsp_dbg_sf[22 + 3 * q + w] = c->sfs[q][w];
			}
#endif
		}
		if (nch == 2)
			stereo(&gc[0], &gc[1], flags, rate);
#ifdef MP3DSP_DEBUG
		if (gr == mp3dsp_dbg_gr)
			for (ch = 0; ch < nch; ch++)
				memcpy(mp3dsp_dbg_st[ch], gc[ch].xr, sizeof(gc[ch].xr));
#endif
		for (ch = 0; ch < nch; ch++) {
			reorder(&gc[ch], rate);
#ifdef MP3DSP_DEBUG
			if (gr == mp3dsp_dbg_gr && ch == mp3dsp_dbg_ch)
				memcpy(mp3dsp_dbg_ro, gc[ch].xr, sizeof(gc[ch].xr));
#endif
			alias(&gc[ch]);
#ifdef MP3DSP_DEBUG
			if (gr == mp3dsp_dbg_gr && ch == mp3dsp_dbg_ch)
				memcpy(mp3dsp_dbg_al, gc[ch].xr, sizeof(gc[ch].xr));
#endif
#ifdef MP3DSP_DEBUG
			memcpy(mp3dsp_dbg_xr[ch], gc[ch].xr, sizeof(gc[ch].xr));
#endif
			imdct(&gc[ch], d->ov[ch], sub);
#ifdef MP3DSP_DEBUG
			memcpy(mp3dsp_dbg_sub, sub, sizeof(mp3dsp_dbg_sub));
#endif
#ifdef MP3DSP_SYNTH_HOOK
			{
				static dw in[32][18];
				memcpy(in, sub, sizeof(in));
				synth(d, ch, sub, &pcm[ch][576 * gr]);
				mp3dsp_synth_hook(ch, in, &pcm[ch][576 * gr]);
			}
#else
			synth(d, ch, sub, &pcm[ch][576 * gr]);
#endif
		}
	}
	return nch;
}
