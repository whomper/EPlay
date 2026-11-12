/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * opld.c - the YM3812's registers for the DSP (opld.h). Written from the
 * chip's documented behaviour, its timings measured against the reference
 * (tests/opl): the envelope clocked each of the chip's samples (clock / 72),
 * a rate R's pattern stepped each 2^(12 - R) of them up to R 12, each one
 * above it, at twice and four times the increments (R 15's decay 4, its
 * attack at once); the attack's step the attenuation's eighth times the
 * increment; the key scaling's rate offset the key code (block x 2 and the
 * F-number's top bit, or the next with NTS), a quarter of it without KSR;
 * its level 6 dB an octave at most, halved and quartered. Integers only:
 * it runs on the 68030.
 */
#include <string.h>
#include "opld.h"
#include "egmath.h"

static const unsigned char eg_lo[4][8] = {
	{ 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 } };
static const unsigned char eg_hi[4][8] = {
	{ 1, 1, 1, 1, 1, 1, 1, 1 }, { 1, 1, 1, 2, 1, 1, 1, 2 }, { 1, 2, 1, 2, 1, 2, 1, 2 }, { 1, 2, 2, 2, 1, 2, 2, 2 } };
/* the multiple x 2 (0: a half) */
static const unsigned char mt[16] = { 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30 };
/* the key scaling's level at block 8, envelope steps / 4, by the
   F-number's top four bits (6 dB an octave below it) */
static const unsigned char ksl_rom[16] = { 0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64 };
/* KSL 1: 3 dB an octave (a half), 2: 1.5 (a quarter), 3: 6 */
static const unsigned char ksl_shift[4] = { 0, 1, 2, 0 };
/* the drums' bits in 0xBD by operator (12-17), in the rhythm mode */
static const unsigned char drum_bit[6] = { 0x10, 0x10, 0x01, 0x08, 0x04, 0x02 };

/* each rate's increments over the chip's pattern: their mean an update
   (x OPL_Q) and the attack's factor an update (Q23), for the envelope's
   clocks an update (clock / 72 / rate x OPL_UPD, as egk_n / egk_d) */
static void rate_tables(OPLD *d)
{
	unsigned long long egk_n = (unsigned long long)d->clock * OPL_UPD, egk_d = 72ULL * (unsigned long)d->rate;
	int r, k;
	for (r = 0; r < 64; r++) {
		int hi = r >> 2, period, sum = 0;
		long long l2 = 0;
		if (r < 4) {
			d->r_mean[r] = 0;
			d->r_arf[r] = 8388607L;	/* (no attack: its factor 1) */
			continue;
		}
		if (hi == 15) {
			d->r_mean[r] = (long)((4ULL * egk_n * OPL_Q + egk_d / 2) / egk_d);
			d->r_arf[r] = 0;	/* (at once) */
			continue;
		}
		period = hi <= 12 ? 8 << (12 - hi) : 8;
		for (k = 0; k < 8; k++) {
			int inc = hi <= 12 ? eg_lo[r & 3][k] : eg_hi[r & 3][k] << (hi - 13);
			sum += inc;
			/* (the attack's step: inc / 8 of it, log2(1 - 2 inc / 16)) */
			l2 += eg_log2_att[2 * inc];
		}
		d->r_mean[r] = (long)(((unsigned long long)sum * egk_n * OPL_Q + (unsigned long long)period * egk_d / 2) /
				      ((unsigned long long)period * egk_d));
		d->r_arf[r] = eg_exp2(l2 * (long long)egk_n / ((long long)period * (long long)egk_d));
		if (d->r_arf[r] > 8388607L)
			d->r_arf[r] = 8388607L;
	}
}

static int eff(int r, int rof)
{
	int v;
	if (!r)
		return 0;
	v = 4 * r + rof;
	return v > 63 ? 63 : v;
}

static void op_rates(OPLD *d, OPLD_OP *op)
{
	int r = eff(op->ar, op->rof);
	op->arf = r >= 60 ? 0 : !r ? 8388607L : d->r_arf[r];
	op->d1 = d->r_mean[eff(op->dr, op->rof)];
	op->rr = d->r_mean[eff(op->rrr, op->rof)];
	/* (EGT 0: the sustain decays at the release's rate) */
	op->d2 = op->egt ? 0 : op->rr;
	op->sl = (long)((op->slr == 15 ? 31 : op->slr) << 4) * OPL_Q;
}

static void op_level(const OPLD_CH *ch, OPLD_OP *op)
{
	long k = 0;
	if (op->ksl) {
		k = (long)ksl_rom[ch->fnum >> 6] * 4 - 32L * (8 - ch->block);
		k = k < 0 ? 0 : k >> ksl_shift[op->ksl];
	}
	op->tl = (long)op->tlr * 8 + k * 2;
}

/* a channel's steps, its key scaling (the rates where its offset changed,
   the levels), its vibrato's depth */
static void ch_steps(OPLD *d, int c)
{
	OPLD_CH *ch = &d->ch[c];
	int ksn = ch->block << 1 | ((d->nts ? ch->fnum >> 8 : ch->fnum >> 9) & 1);
	int i;
	for (i = 0; i < 2; i++) {
		OPLD_OP *op = &ch->op[i];
		long f = ((long)ch->fnum << ch->block >> 1) * mt[op->mult];
		int rof = op->ksr ? ksn : ksn >> 2;
		/* (the chip's 2^20 a turn at clock / 72, 2^21 here at the output's
		   rate: x stepk (Q16), two 32-bit multiplies of its 10-bit halves) */
		unsigned long t = (unsigned long)(f >> 10) * d->stepk;
		long tl = op->tl;
		op->step = (long)((t >> 6) + ((((t & 63) << 10) + (unsigned long)(f & 1023) * d->stepk + 32768) >> 16)) &
			   0xffffff;
		op_level(ch, op);
		if (rof != op->rof) {
			op->rof = rof;
			op_rates(d, op);
			d->op_dirty |= 1UL << (c * 2 + i);
		} else if (tl != op->tl)
			d->op_dirty |= 1UL << (c * 2 + i);
		else
			d->step_dirty |= 1UL << (c * 2 + i);	/* (its step alone: STEP's 3 words, not OP's 12) */
	}
	/* (the vibrato's depth follows the F-number) */
	if (ch->op[0].vib || ch->op[1].vib)
		d->ch_dirty |= 1u << c;
}

/* each operator keyed by its channel, or in the rhythm mode by its drum */
static void keys(OPLD *d)
{
	int i;
	for (i = 0; i < 2 * OPL_CH; i++) {
		OPLD_OP *op = &d->ch[i >> 1].op[i & 1];
		int on = d->ch[i >> 1].key;
		if (i >= 12 && i < 18 && (d->bd & 0x20) && (d->bd & drum_bit[i - 12]))
			on = 1;
		if (on != op->key) {
			d->key_event[i] = on ? OPLD_KEYON : OPLD_KEYOFF;
			d->key_dirty |= 1UL << i;
			op->key = on;
		}
	}
}

void opld_init(OPLD *d, unsigned long clock, long rate)
{
	int c, i;
	memset(d, 0, sizeof(*d));
	d->clock = clock;
	d->rate = rate;
	d->outg = OPL_OUTG;
	d->stepk = (unsigned long)(((2ULL * clock << 16) + 36ULL * (unsigned long)rate) / (72ULL * (unsigned long)rate));
	rate_tables(d);
	for (c = 0; c < OPL_CH; c++) {
		for (i = 0; i < 2; i++) {
			d->ch[c].op[i].rof = -1;
			d->ch[c].op[i].egt = 0;
		}
		ch_steps(d, c);
	}
	d->op_dirty = (1UL << (2 * OPL_CH)) - 1;
	d->ch_dirty = (1u << OPL_CH) - 1;
	d->lfo_dirty = d->rhy_dirty = 1;
}

void opld_gain(OPLD *d, long g)
{
	d->outg = g;
	d->ch_dirty = (1u << OPL_CH) - 1;
}

void opld_write(OPLD *d, int reg, int val)
{
	int c, i;
	OPLD_OP *op;
	val &= 255;
	if ((reg >= 0x20 && reg < 0xa0) || (reg >= 0xe0 && reg < 0xf6)) {
		int o = reg & 0x1f;
		if (o > 0x15 || (o & 7) >= 6)
			return;
		c = (o >> 3) * 3 + (o & 7) % 3;
		i = (o & 7) / 3;
		op = &d->ch[c].op[i];
		switch (reg & 0xe0) {
		case 0x20:
			op->am = val >> 7;
			op->vib = (val >> 6) & 1;
			op->egt = (val >> 5) & 1;
			op->ksr = (val >> 4) & 1;
			op->mult = val & 15;
			op->rof = -1;
			ch_steps(d, c);	/* (the step, the rate offset: the rates) */
			d->op_dirty |= 1UL << (c * 2 + i);
			d->ch_dirty |= 1u << c;
			return;
		case 0x40:
			op->ksl = val >> 6;
			op->tlr = val & 63;
			op_level(&d->ch[c], op);
			break;
		case 0x60:
			op->ar = val >> 4;
			op->dr = val & 15;
			op_rates(d, op);
			break;
		case 0x80:
			op->slr = val >> 4;
			op->rrr = val & 15;
			op_rates(d, op);
			break;
		default:
			op->wsr = val & 3;
			op->ws = d->wse ? op->wsr : 0;
			break;
		}
		d->op_dirty |= 1UL << (c * 2 + i);
		return;
	}
	switch (reg) {
	case 0x01:
		if ((val & 0x20) != d->wse) {
			d->wse = val & 0x20;
			for (i = 0; i < 2 * OPL_CH; i++) {
				op = &d->ch[i >> 1].op[i & 1];
				op->ws = d->wse ? op->wsr : 0;
			}
			d->op_dirty = (1UL << (2 * OPL_CH)) - 1;
		}
		return;
	case 0x08:
		if ((val >> 6 & 1) != d->nts) {
			d->nts = val >> 6 & 1;
			for (c = 0; c < OPL_CH; c++)
				ch_steps(d, c);
		}
		return;
	case 0xbd:
		if ((val ^ d->bd) & 0x80)
			d->lfo_dirty = 1;
		if ((val ^ d->bd) & 0x40)
			d->ch_dirty = (1u << OPL_CH) - 1;	/* (the vibrato's depth) */
		if ((val ^ d->bd) & 0x20)
			d->rhy_dirty = 1;
		d->bd = val;
		keys(d);
		return;
	}
	c = reg & 15;
	if (c >= OPL_CH)
		return;
	switch (reg & 0xf0) {
	case 0xa0:
		d->ch[c].fnum = (d->ch[c].fnum & 0x300) | val;
		ch_steps(d, c);
		break;
	case 0xb0:
		if ((val & 0x1f) != (d->ch[c].fnum >> 8 | d->ch[c].block << 2)) {
			d->ch[c].fnum = (d->ch[c].fnum & 0xff) | (val & 3) << 8;
			d->ch[c].block = (val >> 2) & 7;
			ch_steps(d, c);
		}
		d->ch[c].key = (val >> 5) & 1;
		keys(d);
		break;
	case 0xc0:
		d->ch[c].fb = (val >> 1) & 7;
		d->ch[c].cnt = val & 1;
		d->ch_dirty |= 1u << c;
		break;
	}
}

/* (opld_send's words, sent together) */
static long ob[512];
static int on_;
#define EMIT(w)	(ob[on_++] = (w))

void opld_send(OPLD *d, void (*put)(void *u, long w), void *u)
{
	int c, k;
	unsigned long ops = d->op_dirty | d->key_dirty | d->step_dirty;
	if (!ops && !d->ch_dirty && !d->lfo_dirty && !d->rhy_dirty)
		return;
	on_ = 0;
	if (d->lfo_dirty) {
		/* (the chip's samples an update: clock / 72 / rate x OPL_UPD; the
		   tremolo's period 13,440 of them, 210 steps of 64; the vibrato's
		   8,192, 8 of 1,024) */
		unsigned long long n = (unsigned long long)d->clock * OPL_UPD << 24, q = 72ULL * (unsigned long)d->rate;
		EMIT(OPLC_LFO);
		EMIT((long)((n + q * 13440 / 2) / (q * 13440)));
		EMIT((long)((n + q * 8192 / 2) / (q * 8192)));
		EMIT(d->bd >> 7 & 1);
	}
	if (d->rhy_dirty) {
		/* (the noise's steps a sample: the chip's samples one of ours) */
		long ns = (long)((d->clock + 36UL * (unsigned long)d->rate) / (72UL * (unsigned long)d->rate));
		EMIT(OPLC_RHY);
		EMIT(d->bd >> 5 & 1);
		EMIT(ns ? ns : 1);
	}
	for (c = 0; c < OPL_CH; c++) {
		OPLD_CH *ch = &d->ch[c];
		if (!((ops >> (c * 2)) & 3) && !(d->ch_dirty & (1u << c)))
			continue;
		if (d->ch_dirty & (1u << c)) {
			long dw = ch->fnum ? (((long)(ch->fnum >> 7) << 24) / ch->fnum) : 0;
			EMIT(OPLC_CH);
			EMIT(c);
			EMIT(ch->cnt);
			EMIT(ch->fb ? 1L << (ch->fb + 14) : 0);
			EMIT(d->outg);
			EMIT(d->outg);
			EMIT(d->bd & 0x40 ? dw : dw >> 1);
		}
		for (k = 0; k < 2; k++) {
			int i = c * 2 + k;
			const OPLD_OP *op = &ch->op[k];
			if (d->op_dirty & (1UL << i)) {
				EMIT(OPLC_OP);
				EMIT(i);
				EMIT(op->step);
				EMIT(op->tl);
				EMIT(op->arf);
				EMIT(op->d1);
				EMIT(op->d2);
				EMIT(op->rr);
				EMIT(op->sl);
				EMIT(op->am);
				EMIT(op->vib);
				EMIT(op->ws);
			} else if (d->step_dirty & (1UL << i)) {
				EMIT(OPLC_STEP);
				EMIT(i);
				EMIT(op->step);
			}
			if (d->key_event[i]) {
				EMIT(OPLC_KEY);
				EMIT(i);
				EMIT(d->key_event[i]);
				d->key_event[i] = 0;
			}
		}
	}
	d->op_dirty = d->step_dirty = d->key_dirty = 0;
	d->ch_dirty = 0;
	d->lfo_dirty = d->rhy_dirty = 0;
	for (k = 0; k < on_; k++)
		put(u, ob[k]);
}
