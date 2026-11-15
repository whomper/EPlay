/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * oplcpu.c - the YM3812 and the YMF262 made on the CPU (oplcpu.h; phase
 * 11, step 3: VGM on a 68060 without the Falcon's DSP). src/oplfast.c (the
 * DSP's arithmetic, opl.a56's word for word), its commands, envelopes,
 * four-operator pairs and drums as they are, but in 32 bits, as opncpu.c
 * does the YM2612: the DSP's 24 x 24-bit products made of each word's top
 * 16 bits, 16 x 16 into 32, its accumulators the products' top words; the
 * factors that are small integers or powers of two (the tremolo's, the
 * attenuation's shift, the waveform's index) exact. Held to Nuked-OPL3 by
 * tests/opl within the limits the DSP's model is.
 */
#include <string.h>
#include "oplcpu.h"
#include "oplexp.h"

enum { M_ATTACK, M_DECAY, M_SUSTAIN, M_RELEASE, M_OFF };

#define W24(v) ((long)(((unsigned long)(v)) & 0xffffff))
#define S24(v) ((long)((((unsigned long)(v)) & 0xffffff) ^ 0x800000) - 0x800000)
#define AK (OPL_Q * 9 / 2)

/* mpy x,y: the product's top word (2^23 = 1.0), from the two words' top
   16 bits */
#define PROD(a, b) ((((long)(a) >> 8) * ((long)(b) >> 8)) >> 7)
#define LIMV(v) ((v) > 8388607L ? 8388607L : (v) < -8388608L ? -8388608L : (v))

void oplc_init(OPLF *f, const OPLC_TABS *t)
{
	int i, c, k;
	memset(f, 0, sizeof(*f));
	/* (the waveforms' and the gains' top 16 bits: the products' operands) */
	for (i = 0; i < 8192; i++)
		f->wave[i >> 10][i & 1023] = S24(oplx_wave(t->sinq, t->rq, i >> 10, i & 1023)) >> 8;
	for (i = 0; i < OPLF_GAINS; i++)
		f->gain_tab[i] = i < t->ngain ? (long)(t->gain[i] >> 8) : 0;
	for (i = 0; i < 256; i++) {
		f->am_tab[0][i] = (long)t->am[i];
		f->am_tab[1][i] = (long)t->am[256 + i];
	}
	for (i = 0; i < 8; i++)
		f->pmw[i] = S24(t->pmw[i]);
	for (c = 0; c < OPL_CH; c++)
		for (k = 0; k < 2; k++) {
			f->ch[c].op[k].att = OPL_ATTMAX;
			f->ch[c].op[k].mode = M_OFF;
		}
	f->upd = 0;
	f->noise = 1;
	f->nsteps = 1;
	f->nch = 9;
}

/* ---- the commands (oplfast.c's) */

static int cmd_len(const OPLF *f)
{
	switch (f->cw[0]) {
	case OPLC_OP:
		return 12;
	case OPLC_CH:
		return 7;
	case OPLC_LFO:
		return 4;
	case OPLC_KEY:
	case OPLC_STEP:
	case OPLC_RHY:
		return 3;
	case OPLC_NCH:
		return 2;
	}
	return 1;
}

static void cmd(OPLF *f, const long *w)
{
	OPLF_CH *ch;
	OPLF_OP *op;
	switch (w[0]) {
	case OPLC_OP:
		op = &f->ch[w[1] / 2].op[w[1] % 2];
		op->step_e = op->step = w[2];
		op->tl = w[3];
		op->arf = w[4];
		op->d1 = w[5];
		op->d2 = w[6];
		op->rr = w[7];
		op->sl = w[8];
		op->am = (int)w[9];
		op->vib = (int)w[10];
		op->ws = (int)w[11] & 7;
		break;
	case OPLC_STEP:
		op = &f->ch[w[1] / 2].op[w[1] % 2];
		op->step_e = op->step = w[2];
		break;
	case OPLC_CH:
		ch = &f->ch[w[1]];
		ch->cnt = (int)w[2];
		ch->kf = S24(w[3]);
		ch->pl = S24(w[4]);
		ch->pr = S24(w[5]);
		ch->dw = S24(w[6]);
		break;
	case OPLC_KEY:
		ch = &f->ch[w[1] / 2];
		op = &ch->op[w[1] % 2];
		if (w[2] == OPLD_KEYON) {
			op->phase = 0;
			if (op->arf)
				op->mode = M_ATTACK;
			else {
				op->att = 0;
				op->mode = M_DECAY;
			}
			ch->live = 1;
		} else if (op->mode != M_OFF)
			op->mode = M_RELEASE;
		break;
	case OPLC_LFO:
		f->am_step = w[1];
		f->pm_step = w[2];
		f->am_deep = (int)w[3] & 1;
		break;
	case OPLC_RHY:
		f->rhy = (int)w[1];
		f->nsteps = (int)w[2];
		break;
	case OPLC_NCH:
		f->nch = w[1] > 9 ? OPL_CH : 9;
		break;
	}
}

void oplc_word(OPLF *f, long w)
{
	f->cw[f->cn++] = W24(w);
	if (f->cn >= cmd_len(f)) {
		cmd(f, f->cw);
		f->cn = 0;
	}
}

/* ---- the envelopes, the gains, the LFOs: each OPL_UPD samples */
static void update(OPLF *f)
{
	int c, k;
	long amt, pmv;
	f->am_phase = W24(f->am_phase + f->am_step);
	amt = f->am_tab[f->am_deep][f->am_phase >> 16];
	f->pm_phase = W24(f->pm_phase + f->pm_step);
	pmv = f->pmw[f->pm_phase >> 21];
	for (c = 0; c < OPL_CH; c++) {
		OPLF_CH *ch = &f->ch[c];
		long vib = PROD(pmv, ch->dw);
		int live = 0;
		for (k = 0; k < 2; k++) {
			OPLF_OP *op = &ch->op[k];
			long v;
			if (op->mode == M_OFF)
				continue;
			live = 1;
			switch (op->mode) {
			case M_ATTACK:
				/* (att + 4.5 steps by the factor: the attenuation's top
				   16 bits, the factor's) */
				v = ((((op->att + AK) >> 7) * (S24(op->arf) >> 8)) >> 8) - AK;
				if (v <= 0) {
					v = 0;
					op->mode = M_DECAY;
				}
				break;
			case M_DECAY:
				v = op->att + op->d1;
				if (v >= op->sl) {
					v = op->sl;
					op->mode = M_SUSTAIN;
				}
				break;
			case M_SUSTAIN:
				v = op->att + op->d2;
				break;
			default:
				v = op->att + op->rr;
				if (v >= OPL_ATTMAX)
					op->mode = M_OFF;
				break;
			}
			op->att = v > OPL_ATTMAX ? OPL_ATTMAX : v;
			/* the gain (its top 16 bits: oplc_init): att x 2^-12 + tl,
			   the tremolo's on it */
			op->gain = f->gain_tab[(op->att >> 12) + op->tl + (op->am ? amt : 0)];
			/* the vibrato: step + step x vib */
			op->step_e = op->vib ? W24(op->step + PROD(op->step, vib)) : op->step;
		}
		ch->live = live;
	}
	f->upd = OPL_UPD;
}

/* ---- a block: each channel's samples to the next update (at most
   OPL_UPD), its state in registers, then the next channel's (the DSP's
   order is a sample at a time, every channel; each channel's arithmetic
   is the same either way: only the four-operator pairs' link and the
   drums' noise go between channels, kept a block's worth here) */

/* (the waveform's word at a phase and its modulation, times the gain:
   the products' top words, 2^23 = 1.0) */
#define WAV(w, p, m, g) (((w)[(((p) + (m)) >> 11) & 1023] * (g)) >> 7)

/* the modulator's feedback: its last two outputs x the factor, which
   opld makes 2^(FB + 14) (a shift: exact), or 0 */
static int fb_shift(long kf)
{
	int e = 0;
	if (kf <= 0)
		return -1;
	while (!(kf & 1)) {
		kf >>= 1;
		e++;
	}
	return kf == 1 && e >= 15 ? e - 15 : -2;
}

#define FBK(o1, o1p, kf, fs) \
	((fs) >= 0 ? (((o1) >> 8) + ((o1p) >> 8)) << (fs) : (fs) == -1 ? 0 : PROD(o1, kf) + PROD(o1p, kf))

/* a two-operator channel (oplfast.c's ch_calc): bd the rhythm mode's bass
   drum (the carrier alone when additive, / 4) */
static void ch_block(OPLF *f, OPLF_CH *ch, int bd, long *v, int m)
{
	OPLF_OP *a = &ch->op[0], *b = &ch->op[1];
	const long *wa = f->wave[a->ws], *wb = f->wave[b->ws];
	long pa = a->phase, sa = a->step_e, ga = a->gain;
	long pb = b->phase, sb = b->step_e, gb = b->gain;
	long o1 = ch->o1, o1p = ch->o1p, kf = ch->kf;
	int k, fs = fb_shift(kf), sh = bd ? 2 : 3;
	if (ch->cnt != 1)
		for (k = 0; k < m; k++) {
			long x, fb = FBK(o1, o1p, kf, fs);
			pa = (pa + sa) & 0xffffff;
			x = WAV(wa, pa, fb, ga);
			o1p = o1;
			o1 = LIMV(x);
			pb = (pb + sb) & 0xffffff;
			x = WAV(wb, pb, o1, gb) >> sh;
			v[k] = LIMV(x);
		}
	else
		for (k = 0; k < m; k++) {
			long x, p1, fb = FBK(o1, o1p, kf, fs);
			pa = (pa + sa) & 0xffffff;
			p1 = WAV(wa, pa, fb, ga);
			o1p = o1;
			o1 = LIMV(p1);
			pb = (pb + sb) & 0xffffff;
			x = WAV(wb, pb, 0, gb);
			if (!bd)
				x += p1;
			x >>= sh;
			v[k] = LIMV(x);
		}
	a->phase = pa;
	b->phase = pb;
	ch->o1 = o1;
	ch->o1p = o1p;
}

/* a four-operator pair's first channel (oplfast.c's pair1): A and B, B's
   output the second's link (lk) */
static void pair1_block(OPLF *f, OPLF_CH *ch, OPLF_CH *pr, long *v, long *lk, int m)
{
	OPLF_OP *a = &ch->op[0], *b = &ch->op[1];
	const long *wa = f->wave[a->ws], *wb = f->wave[b->ws];
	long pa = a->phase, sa = a->step_e, ga = a->gain;
	long pb = b->phase, sb = b->step_e, gb = b->gain;
	long o1 = ch->o1, o1p = ch->o1p, kf = ch->kf;
	int k, fs = fb_shift(kf);
	if (ch->cnt == 3) {
		ch_block(f, ch, 0, v, m);
		return;
	}
	for (k = 0; k < m; k++) {
		long x, p1, fb = FBK(o1, o1p, kf, fs);
		pa = (pa + sa) & 0xffffff;
		p1 = WAV(wa, pa, fb, ga);
		o1p = o1;
		o1 = LIMV(p1);
		pb = (pb + sb) & 0xffffff;
		if (ch->cnt == 2) {
			x = WAV(wb, pb, o1, gb);
			lk[k] = LIMV(x);
			v[k] = 0;
		} else {
			x = WAV(wb, pb, 0, gb);
			lk[k] = LIMV(x);
			p1 >>= 3;
			v[k] = LIMV(p1);
		}
	}
	a->phase = pa;
	b->phase = pb;
	ch->o1 = o1;
	ch->o1p = o1p;
	pr->link = lk[m - 1];
}

/* its second (oplfast.c's pair2): C (modulated by the link) and D */
static void pair2_block(OPLF *f, OPLF_CH *ch, long *v, const long *lk, int m)
{
	OPLF_OP *c = &ch->op[0], *d = &ch->op[1];
	const long *wc = f->wave[c->ws], *wd = f->wave[d->ws];
	long pc = c->phase, sc = c->step_e, gc = c->gain;
	long pd = d->phase, sd = d->step_e, gd = d->gain;
	int k;
	if (ch->cnt == OPL_PAIR2 + 1) {
		ch_block(f, ch, 0, v, m);
		return;
	}
	for (k = 0; k < m; k++) {
		long x;
		pc = (pc + sc) & 0xffffff;
		x = WAV(wc, pc, lk[k], gc);
		pd = (pd + sd) & 0xffffff;
		if (ch->cnt == OPL_PAIR2 + 3)
			x += WAV(wd, pd, 0, gd);
		else {
			x = LIMV(x);
			x = WAV(wd, pd, x, gd);
		}
		x >>= 3;
		v[k] = LIMV(x);
	}
	c->phase = pc;
	d->phase = pd;
}

/* the rhythm mode's channels 7 and 8 (oplfast.c's drums), the noise's bit
   at each sample in nb */
static void drums_block(OPLF *f, const int *nb, long *v7, long *v8, int m)
{
	OPLF_OP *hh = &f->ch[7].op[0], *sd = &f->ch[7].op[1], *tt = &f->ch[8].op[0], *cy = &f->ch[8].op[1];
	const long *whh = f->wave[hh->ws], *wsd = f->wave[sd->ws], *wtt = f->wave[tt->ws], *wcy = f->wave[cy->ws];
	int k;
	for (k = 0; k < m; k++) {
		long h, y, n = nb[k], x, a, b;
		hh->phase = W24(hh->phase + hh->step_e);
		h = hh->phase >> 11 & 1023;
		sd->phase = W24(sd->phase + sd->step_e);
		tt->phase = W24(tt->phase + tt->step_e);
		b = WAV(wtt, tt->phase, 0, tt->gain);
		cy->phase = W24(cy->phase + cy->step_e);
		y = cy->phase >> 11 & 1023;
		x = (((h >> 2) ^ (h >> 7)) | ((h >> 3) ^ (y >> 5)) | ((y >> 3) ^ (y >> 5))) & 1;
		b += (wcy[x << 9 | 0x80] * cy->gain) >> 7;
		b >>= 2;
		v8[k] = LIMV(b);
		a = (whh[x << 9 | ((x ^ n) ? 0xd0 : 0x34)] * hh->gain) >> 7;
		a += (wsd[(h >> 8 & 1) << 9 | ((h >> 8 ^ n) & 1) << 8] * sd->gain) >> 7;
		a >>= 2;
		v7[k] = LIMV(a);
	}
	f->v8 = v8[m - 1];
}

/* a channel's samples into the mix by its gains (the DSP's: each
   channel's product, summed) */
static void mix(const OPLF_CH *ch, const long *v, long *l, long *r, int m)
{
	int k;
	if (ch->pl == ch->pr) {
		long g = ch->pl;
		if (!g)
			return;
		for (k = 0; k < m; k++) {
			long t = PROD(v[k], g);
			l[k] += t;
			r[k] += t;
		}
	} else
		for (k = 0; k < m; k++) {
			l[k] += PROD(v[k], ch->pl);
			r[k] += PROD(v[k], ch->pr);
		}
}

void oplc_words(OPLF *f, long *lr, int n)
{
	static long l[OPL_UPD], r[OPL_UPD], v[OPL_UPD], v8[OPL_UPD], lk[OPL_CH][OPL_UPD];
	static int nb[OPL_UPD];
	while (n > 0) {
		int m, k, c, j;
		/* (upd: the samples to the next update; oplc_init's 0, an update
		   before the first, as the DSP's) */
		if (!f->upd)
			update(f);
		m = n < f->upd ? n : f->upd;
		memset(l, 0, (size_t)m * sizeof(l[0]));
		memset(r, 0, (size_t)m * sizeof(r[0]));
		if (f->rhy)
			for (k = 0; k < m; k++) {
				for (j = 0; j < f->nsteps; j++) {
					long b = (f->noise ^ (f->noise >> 14)) & 1;
					f->noise = f->noise >> 1 | b << 22;
				}
				nb[k] = (int)(f->noise & 1);
			}
		for (c = 0; c < f->nch; c++) {
			OPLF_CH *ch = &f->ch[c];
			if (f->rhy && c == 7) {
				drums_block(f, nb, v, v8, m);
				mix(ch, v, l, r, m);
				mix(&f->ch[8], v8, l, r, m);
				c++;
				continue;
			}
			if (ch->cnt >= 2 && ch->cnt < OPL_PAIR2) {
				if (!ch->live && !f->ch[c + 3].live)
					continue;
				pair1_block(f, ch, &f->ch[c + 3], v, lk[c + 3], m);
			} else if (ch->cnt >= OPL_PAIR2) {
				if (!ch->live && !f->ch[c - 3].live)
					continue;
				pair2_block(f, ch, v, lk[c], m);
			} else if (!ch->live)
				continue;
			else
				ch_block(f, ch, f->rhy && c == 6, v, m);
			mix(ch, v, l, r, m);
		}
		for (k = 0; k < m; k++) {
			lr[2 * k] = W24(LIMV(l[k]));
			lr[2 * k + 1] = W24(LIMV(r[k]));
		}
		f->upd -= m;
		lr += 2 * m;
		n -= m;
	}
}

void oplc_run(OPLF *f, short *out, int n)
{
	long lr[2 * 64];
	while (n > 0) {
		int m = n < 64 ? n : 64, k;
		oplc_words(f, lr, m);
		for (k = 0; k < 2 * m; k++) {
			long v = (S24(lr[k]) >> 8) + out[k];
			out[k] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
		}
		out += 2 * m;
		n -= m;
	}
}
