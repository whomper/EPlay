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
	f->upd = 1;
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

/* ---- a sample */

/* an operator: its phase on (stored), its modulation added, the index
   (bits 20-11), its waveform (its top 16 bits) times its gain's */
#define OPC(op, m) \
	(op_b = (op)->phase + (op)->step_e, (op)->phase = op_b & 0xffffff, \
	 (f->wave[(op)->ws][((op_b + (m)) >> 11) & 1023] * (op)->gain) >> 7)
/* (a drum's: at a phase index made for it) */
#define OPAT(op, idx) ((f->wave[(op)->ws][idx] * (op)->gain) >> 7)
/* the modulator's feedback: its last two outputs x the factor */
#define FB(ch) (PROD((ch)->o1, (ch)->kf) + PROD((ch)->o1p, (ch)->kf))

static long ch_calc(OPLF *f, OPLF_CH *ch, int bd)
{
	OPLF_OP *o = ch->op;
	long a, p1, op_b;
	p1 = OPC(&o[0], FB(ch));
	ch->o1p = ch->o1;
	ch->o1 = LIMV(p1);
	if (ch->cnt != 1)
		a = OPC(&o[1], ch->o1);
	else if (bd)
		a = OPC(&o[1], 0);	/* (the bass drum's: the carrier alone) */
	else
		a = p1 + OPC(&o[1], 0);
	a >>= bd ? 2 : 3;
	return LIMV(a);
}

/* a four-operator pair (oplfast.c's pair1 and pair2) */
static long pair1(OPLF *f, OPLF_CH *ch, OPLF_CH *pr)
{
	OPLF_OP *a = &ch->op[0], *b = &ch->op[1];
	long pa, x, op_b;
	if (ch->cnt == 3)
		return ch_calc(f, ch, 0);
	pa = OPC(a, FB(ch));
	ch->o1p = ch->o1;
	ch->o1 = LIMV(pa);
	if (ch->cnt == 2) {
		x = OPC(b, ch->o1);
		pr->link = LIMV(x);
		return 0;
	}
	x = OPC(b, 0);
	pr->link = LIMV(x);
	pa >>= 3;
	return LIMV(pa);
}

static long pair2(OPLF *f, OPLF_CH *ch)
{
	OPLF_OP *c = &ch->op[0], *d = &ch->op[1];
	long x, op_b;
	if (ch->cnt == OPL_PAIR2 + 1)
		return ch_calc(f, ch, 0);
	x = OPC(c, ch->link);
	if (ch->cnt == OPL_PAIR2 + 3)
		x += OPC(d, 0);
	else {
		x = LIMV(x);
		x = OPC(d, x);
	}
	x >>= 3;
	return LIMV(x);
}

/* the rhythm mode's channels 7 and 8 (oplfast.c's drums) */
static long drums(OPLF *f)
{
	OPLF_OP *hh = &f->ch[7].op[0], *sd = &f->ch[7].op[1], *tt = &f->ch[8].op[0], *cy = &f->ch[8].op[1];
	long h, y, n = f->noise & 1, x, a, b, op_b;
	hh->phase = W24(hh->phase + hh->step_e);
	h = hh->phase >> 11 & 1023;
	sd->phase = W24(sd->phase + sd->step_e);
	b = OPC(tt, 0);
	cy->phase = W24(cy->phase + cy->step_e);
	y = cy->phase >> 11 & 1023;
	x = (((h >> 2) ^ (h >> 7)) | ((h >> 3) ^ (y >> 5)) | ((y >> 3) ^ (y >> 5))) & 1;
	b += OPAT(cy, x << 9 | 0x80);
	b >>= 2;
	f->v8 = LIMV(b);
	a = OPAT(hh, x << 9 | ((x ^ n) ? 0xd0 : 0x34));
	a += OPAT(sd, (h >> 8 & 1) << 9 | ((h >> 8 ^ n) & 1) << 8);
	a >>= 2;
	return LIMV(a);
}

void oplc_words(OPLF *f, long *lr, int n)
{
	int k, c, j;
	for (k = 0; k < n; k++) {
		long l = 0, r = 0;
		if (!--f->upd)
			update(f);
		if (f->rhy)
			for (j = 0; j < f->nsteps; j++) {
				long b = (f->noise ^ (f->noise >> 14)) & 1;
				f->noise = f->noise >> 1 | b << 22;
			}
		for (c = 0; c < f->nch; c++) {
			OPLF_CH *ch = &f->ch[c];
			long v;
			if (f->rhy && c >= 7 && c < 9)
				v = c == 7 ? drums(f) : f->v8;
			else if (ch->cnt >= 2 && ch->cnt < OPL_PAIR2)
				v = ch->live || f->ch[c + 3].live ? pair1(f, ch, &f->ch[c + 3]) : 0;
			else if (ch->cnt >= OPL_PAIR2)
				v = ch->live || f->ch[c - 3].live ? pair2(f, ch) : 0;
			else if (!ch->live)
				continue;
			else
				v = ch_calc(f, ch, f->rhy && c == 6);
			if (v) {
				l += PROD(v, ch->pl);
				r += PROD(v, ch->pr);
			}
		}
		lr[2 * k] = W24(LIMV(l));
		lr[2 * k + 1] = W24(LIMV(r));
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
