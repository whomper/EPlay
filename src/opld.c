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
 * it runs on the 68030. The OPL3 (opld.h): its rate clock / 288, so its
 * clock / 4 is taken as an OPL2's; a four-operator pair's second channel
 * takes its first's F-number and block, and its operators their key.
 */
#include <string.h>
#include "opld.h"
#include "tickcode.h"
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

TICK_CODE("79opld_eff")
static int eff(int r, int rof)
{
	int v;
	if (!r)
		return 0;
	v = 4 * r + rof;
	return v > 63 ? 63 : v;
}

TICK_CODE("77opld_op_rates")
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

/* the channel whose F-number and block an operator of channel c takes: a
   four-operator pair's second its first's */
TICK_CODE("7Aopld_freq_ch")
static const OPLD_CH *freq_ch(const OPLD *d, int c)
{
	return d->ch[c].type == 2 ? &d->ch[c - 3] : &d->ch[c];
}

TICK_CODE("78opld_op_level")
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
TICK_CODE("75opld_ch_steps")
static void ch_steps(OPLD *d, int c)
{
	OPLD_CH *ch = &d->ch[c];
	const OPLD_CH *fc = freq_ch(d, c);
	int ksn = fc->block << 1 | ((d->nts ? fc->fnum >> 8 : fc->fnum >> 9) & 1);
	int i;
	for (i = 0; i < 2; i++) {
		OPLD_OP *op = &ch->op[i];
		long f = ((long)fc->fnum << fc->block >> 1) * mt[op->mult];
		int rof = op->ksr ? ksn : ksn >> 2;
		/* (the chip's 2^20 a turn at clock / 72, 2^21 here at the output's
		   rate: x stepk (Q16), two 32-bit multiplies of its 10-bit halves) */
		unsigned long t = (unsigned long)(f >> 10) * d->stepk;
		long tl = op->tl;
		op->step = (long)((t >> 6) + ((((t & 63) << 10) + (unsigned long)(f & 1023) * d->stepk + 32768) >> 16)) &
			   0xffffff;
		op_level(fc, op);
		if (rof != op->rof) {
			op->rof = rof;
			op_rates(d, op);
			d->op_dirty |= 1ULL << (c * 2 + i);
		} else if (tl != op->tl)
			d->op_dirty |= 1ULL << (c * 2 + i);
		else
			d->step_dirty |= 1ULL << (c * 2 + i);	/* (its step alone: STEP's 3 words, not OP's 12) */
	}
	/* (the vibrato's depth follows the F-number) */
	if (ch->op[0].vib || ch->op[1].vib)
		d->ch_dirty |= 1UL << c;
	/* (a four-operator pair's second follows its first) */
	if (ch->type == 1)
		ch_steps(d, c + 3);
}

/* each operator keyed by its channel (a four-operator pair's second by its
   first), or in the rhythm mode by its drum */
TICK_CODE("76opld_keys")
static void keys(OPLD *d)
{
	int i;
	for (i = 0; i < 2 * OPL_CH; i++) {
		int c = i >> 1;
		OPLD_OP *op = &d->ch[c].op[i & 1];
		int on = d->ch[d->ch[c].type == 2 ? c - 3 : c].key;
		if (i >= 12 && i < 18 && (d->bd & 0x20) && (d->bd & drum_bit[i - 12]))
			on = 1;
		if (on != op->key) {
			d->key_event[i] = on ? OPLD_KEYON : OPLD_KEYOFF;
			d->key_dirty |= 1ULL << i;
			op->key = on;
		}
	}
}

static void init(OPLD *d, unsigned long clock, long rate, int opl3)
{
	int c, i;
	memset(d, 0, sizeof(*d));
	d->opl3 = opl3;
	d->nch = 9;
	d->nch_dirty = 1;
	d->clock = opl3 ? clock / 4 : clock;
	d->rate = rate;
	d->outg = OPL_OUTG;
	d->stepk = (unsigned long)(((2ULL * d->clock << 16) + 36ULL * (unsigned long)rate) / (72ULL * (unsigned long)rate));
	rate_tables(d);
	for (c = 0; c < OPL_CH; c++) {
		for (i = 0; i < 2; i++) {
			d->ch[c].op[i].rof = -1;
			d->ch[c].op[i].egt = 0;
		}
		d->ch[c].sa = d->ch[c].sb = 1;
		ch_steps(d, c);
	}
	d->op_dirty = (1ULL << (2 * OPL_CH)) - 1;
	d->ch_dirty = (1UL << OPL_CH) - 1;
	d->lfo_dirty = d->rhy_dirty = 1;
}

void opld_init(OPLD *d, unsigned long clock, long rate)
{
	init(d, clock, rate, 0);
}

void opld_init3(OPLD *d, unsigned long clock, long rate)
{
	init(d, clock, rate, 1);
}

void opld_channels(OPLD *d, int n)
{
	d->nch = n > 9 ? OPL_CH : 9;
	d->nch_dirty = 1;
}

/* an operator's waveform: the OPL2's with WSE, an OPL3's its eight with NEW,
   else its first four (register 1's bit 5 not the OPL3's) */
static int wave_of(const OPLD *d, const OPLD_OP *op)
{
	if (d->opl3)
		return d->newm ? op->wsr & 7 : op->wsr & 3;
	return d->wse ? op->wsr & 3 : 0;
}

/* 0x104: the four-operator pairs (each bit: channels 0 and 3, 1 and 4, 2
   and 5, then the second bank's), the pairs' steps and connections again */
static void four_op(OPLD *d, int v)
{
	int bit;
	d->fourop = v & 0x3f;
	for (bit = 0; bit < 6; bit++) {
		int c = bit < 3 ? bit : bit + 6, on = (v >> bit) & 1;
		d->ch[c].type = on;
		d->ch[c + 3].type = on ? 2 : 0;
		d->ch[c].op[0].rof = d->ch[c].op[1].rof = -1;
		d->ch[c + 3].op[0].rof = d->ch[c + 3].op[1].rof = -1;
		ch_steps(d, c);
		if (!on)
			ch_steps(d, c + 3);
		d->ch_dirty |= 1UL << c | 1UL << (c + 3);
		d->op_dirty |= 3ULL << (c * 2) | 3ULL << ((c + 3) * 2);
	}
}

void opld_gain(OPLD *d, long g)
{
	d->outg = g;
	d->ch_dirty = (1UL << OPL_CH) - 1;
}

TICK_CODE("73opld_write")
void opld_write(OPLD *d, int reg, int val)
{
	int c, i, bank = d->opl3 ? (reg >> 8) & 1 : 0;
	OPLD_OP *op;
	val &= 255;
	reg &= 0xff;
	if (bank && (reg == 0x04 || reg == 0x05)) {
		if (reg == 0x04)
			four_op(d, val);
		else if ((val & 1) != d->newm) {
			d->newm = val & 1;
			for (i = 0; i < 2 * OPL_CH; i++) {
				op = &d->ch[i >> 1].op[i & 1];
				op->ws = wave_of(d, op);
			}
			d->op_dirty = (1ULL << (2 * OPL_CH)) - 1;
		}
		return;
	}
	if ((reg >= 0x20 && reg < 0xa0) || (reg >= 0xe0 && reg < 0xf6)) {
		int o = reg & 0x1f;
		if (o > 0x15 || (o & 7) >= 6)
			return;
		c = (o >> 3) * 3 + (o & 7) % 3 + 9 * bank;
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
			d->op_dirty |= 1ULL << (c * 2 + i);
			d->ch_dirty |= 1UL << c;
			if (d->ch[c].type == 2)
				d->ch_dirty |= 1UL << (c - 3);	/* (the pair's vibrato's depth) */
			return;
		case 0x40:
			op->ksl = val >> 6;
			op->tlr = val & 63;
			op_level(freq_ch(d, c), op);
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
			op->wsr = val & 7;
			op->ws = wave_of(d, op);
			break;
		}
		d->op_dirty |= 1ULL << (c * 2 + i);
		return;
	}
	if (bank && reg < 0xa0)
		return;			/* (0x01, 0x08 and 0xBD the first bank's) */
	switch (bank ? -1 : reg) {
	case 0x01:
		if (!d->opl3 && (val & 0x20) != d->wse) {
			d->wse = val & 0x20;
			for (i = 0; i < 2 * OPL_CH; i++) {
				op = &d->ch[i >> 1].op[i & 1];
				op->ws = wave_of(d, op);
			}
			d->op_dirty = (1ULL << (2 * OPL_CH)) - 1;
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
			d->ch_dirty = (1UL << OPL_CH) - 1;	/* (the vibrato's depth) */
		if ((val ^ d->bd) & 0x20)
			d->rhy_dirty = 1;
		d->bd = val;
		keys(d);
		return;
	}
	c = reg & 15;
	if (c >= 9)
		return;
	c += 9 * bank;
	switch (reg & 0xf0) {
	case 0xa0:
		d->ch[c].fnum = (d->ch[c].fnum & 0x300) | val;
		if (d->ch[c].type != 2)
			ch_steps(d, c);
		break;
	case 0xb0:
		if ((val & 0x1f) != (d->ch[c].fnum >> 8 | d->ch[c].block << 2)) {
			d->ch[c].fnum = (d->ch[c].fnum & 0xff) | (val & 3) << 8;
			d->ch[c].block = (val >> 2) & 7;
			if (d->ch[c].type != 2)
				ch_steps(d, c);
		}
		d->ch[c].key = (val >> 5) & 1;
		if (bank && d->ch[c].key && d->nch <= 9)
			d->late2 = 1;	/* (the second bank keyed, nine channels made: opld.h) */
		keys(d);
		break;
	case 0xc0:
		d->ch[c].fb = (val >> 1) & 7;
		d->ch[c].cnt = val & 1;
		/* (an OPL3 with NEW: its outputs; else both, as the chip takes them
		   at this write) */
		d->ch[c].sa = d->opl3 && d->newm ? (val >> 4) & 1 : 1;
		d->ch[c].sb = d->opl3 && d->newm ? (val >> 5) & 1 : 1;
		d->ch_dirty |= 1UL << c;
		if (d->ch[c].type == 2)
			d->ch_dirty |= 1UL << (c - 3);	/* (its pair's connection, outputs) */
		else if (d->ch[c].type == 1)
			d->ch_dirty |= 1UL << (c + 3);	/* (its second's algorithm) */
		break;
	}
}

/* (opld_send's words, sent together) */
static long ob[1024];		/* (an OPL3 all sent: some 680) */
static int on_;
#define EMIT(w)	(ob[on_++] = (w))

TICK_CODE("74opld_send")
void opld_send(OPLD *d, void (*put)(void *u, long w), void *u)
{
	int c, k;
	unsigned long long ops = d->op_dirty | d->key_dirty | d->step_dirty;
	if (!ops && !d->ch_dirty && !d->lfo_dirty && !d->rhy_dirty && !d->nch_dirty)
		return;
	on_ = 0;
	if (d->nch_dirty) {
		EMIT(OPLC_NCH);
		EMIT(d->nch);
		d->nch_dirty = 0;
	}
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
		if (!((ops >> (c * 2)) & 3) && !(d->ch_dirty & (1UL << c)))
			continue;
		if (d->ch_dirty & (1UL << c)) {
			const OPLD_CH *fc = freq_ch(d, c);
			/* (a pair's outputs its second's, both channels'; its algorithm
			   both channels' connections; C no feedback) */
			const OPLD_CH *oc = ch->type == 1 ? &d->ch[c + 3] : ch;
			int alg = ch->type == 1 ? ch->cnt << 1 | oc->cnt : ch->type == 2 ? d->ch[c - 3].cnt << 1 | ch->cnt : 0;
			long dw = fc->fnum ? (((long)(fc->fnum >> 7) << 24) / fc->fnum) : 0;
			EMIT(OPLC_CH);
			EMIT(c);
			EMIT(ch->type == 1 ? 2 + alg : ch->type == 2 ? OPL_PAIR2 + alg : ch->cnt);
			EMIT(ch->fb && ch->type != 2 ? 1L << (ch->fb + 14) : 0);
			EMIT(oc->sa ? d->outg : 0);
			EMIT(oc->sb ? d->outg : 0);
			EMIT(d->bd & 0x40 ? dw : dw >> 1);
		}
		for (k = 0; k < 2; k++) {
			int i = c * 2 + k;
			const OPLD_OP *op = &ch->op[k];
			if (d->op_dirty & (1ULL << i)) {
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
			} else if (d->step_dirty & (1ULL << i)) {
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
