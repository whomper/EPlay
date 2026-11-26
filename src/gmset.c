/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * gmset.c - the synth's sound set read in place (gmset.h)
 */
#include "gmset.h"

static unsigned long u32(const unsigned char *p)
{
	return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

static unsigned u16(const unsigned char *p)
{
	return (unsigned)p[0] << 8 | p[1];
}

static short s16(const unsigned char *p)
{
	return (short)u16(p);
}

#define BANK_SIZE 258L
#define PROG_SIZE 8L
#define REG_SIZE 20L
#define ART_SIZE 32L

int gm_open(GMSET *g, const unsigned char *b, long n, const char **why)
{
	long o;
	int k;
	*why = "not EPlay's sound set";
	g->prepared = 0;
	if (n < 32 || b[0] != 'E' || b[1] != 'P' || b[2] != 'G' || b[3] != 'M')
		return 0;
	*why = "a sound set of another version";
	if (u16(b + 4) != 1)
		return 0;
	g->b = b;
	g->n = n;
	g->bits = u16(b + 6) & 1 ? 16 : 8;
	g->rate = (long)u32(b + 8);
	g->nbank = u16(b + 16);
	g->nprog = u16(b + 18);
	g->nreg = u16(b + 20);
	g->nart = u16(b + 22);
	g->nwave = u16(b + 24);
	g->points = (long)u32(b + 28);
	*why = "a damaged sound set";
	if (g->rate < 4000 || g->rate > 96000 || g->nreg == 0 || g->nart == 0 || g->nwave == 0 || g->points <= 0)
		return 0;
	o = 32;
	g->o_bank = o;
	o += (g->nbank * BANK_SIZE + 3) & ~3L;
	g->o_prog = o;
	o += g->nprog * PROG_SIZE;
	g->o_reg = o;
	o += g->nreg * REG_SIZE;
	g->o_art = o;
	o += g->nart * ART_SIZE;
	g->o_len = o;
	g->o_off = o + g->nwave * 4L;
	o += g->nwave * 8L;
	g->o_smp = o;
	o += g->points * (g->bits / 8);
	if (o > n)
		return 0;
	/* every index and loop checked once, so the engine trusts them */
	for (k = 0; k < g->nwave; k++) {
		unsigned long off = u32(b + g->o_off + 4L * k), len = u32(b + g->o_len + 4L * k);
		if (len == 0 || off > (unsigned long)g->points || len > (unsigned long)g->points - off)
			return 0;
	}
	for (k = 0; k < g->nreg; k++) {
		GM_REGION r;
		long wl;
		gm_region(g, k, &r);
		if (r.wave >= g->nwave || r.art >= g->nart || r.lo > r.hi || r.hi > 127)
			return 0;
		gm_wave(g, r.wave, &wl);
		if ((r.flags & GM_LOOPED) && (r.loop_end <= r.loop_start || (long)r.loop_end > wl))
			return 0;
	}
	for (k = 0; k < g->nbank * 128; k++) {
		unsigned i = u16(b + g->o_bank + 2 + (k / 128) * BANK_SIZE + 2 * (k % 128));
		if ((i & 0x3fff) >= (unsigned)g->nreg)
			return 0;
	}
	for (k = 0; k < g->nprog; k++)
		if (u16(b + g->o_prog + PROG_SIZE * k + 4) >= (unsigned)g->nreg)
			return 0;
	*why = "";
	return 1;
}

void gm_region(const GMSET *g, int k, GM_REGION *r)
{
	const unsigned char *p = g->b + g->o_reg + REG_SIZE * k;
	r->flags = u16(p);
	r->lo = p[2];
	r->hi = p[3];
	r->tuning = s16(p + 4);
	r->gain = s16(p + 6);
	r->loop_start = u32(p + 8);
	r->loop_end = u32(p + 12);
	r->wave = u16(p + 16);
	r->art = u16(p + 18);
}

void gm_art(const GMSET *g, int k, GM_ART *a)
{
	const unsigned char *p = g->b + g->o_art + ART_SIZE * k;
	int i;
	for (i = 0; i < 4; i++) {
		a->eg1[i] = s16(p + 2 * i);
		a->eg2[i] = s16(p + 8 + 2 * i);
	}
	a->lfo_to_pitch = s16(p + 16);
	a->lfo_delay = s16(p + 18);
	a->lfo_freq = s16(p + 20);
	a->eg2_to_pitch = s16(p + 22);
	a->eg2_to_fc = s16(p + 24);
	a->cutoff = s16(p + 26);
	a->lfo_to_gain = (signed char)p[28];
	a->q = p[29];
	a->pan = (signed char)p[30];
}

const unsigned char *gm_wave(const GMSET *g, int w, long *len)
{
	*len = (long)u32(g->b + g->o_len + 4L * w);
	return g->b + g->o_smp + (long)u32(g->b + g->o_off + 4L * w) * (g->bits / 8);
}

/* (a program's regions follow one another to the one marked GM_LAST; the
   first one whose keys hold the key plays it) */
static int walk(const GMSET *g, int first, int key, GM_REGION *r, int *index)
{
	int k;
	for (k = first; k < g->nreg; k++) {
		gm_region(g, k, r);
		if (key >= r->lo && key <= r->hi) {
			*index = k;
			return 1;
		}
		if (r->flags & GM_LAST)
			break;
	}
	return 0;
}

int gm_find(const GMSET *g, int drums, int prog, int key, GM_REGION *r, int *index)
{
	int k;
	if (key < 0 || key > 127)
		return 0;
	if (drums) {
		for (k = 0; k < g->nprog; k++) {
			const unsigned char *p = g->b + g->o_prog + PROG_SIZE * k;
			if ((long)(u32(p) & 0xffff00L) == GM_DRUMS)
				return walk(g, u16(p + 4), key, r, index);
		}
		return 0;
	}
	if (prog < 0 || prog > 127)
		return 0;
	for (k = 0; k < g->nbank; k++) {
		const unsigned char *p = g->b + g->o_bank + BANK_SIZE * k;
		if (u16(p) == GM_MELODIC)
			return walk(g, u16(p + 2 + 2 * prog) & 0x3fff, key, r, index);
	}
	return 0;
}
