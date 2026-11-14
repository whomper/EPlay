/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sim.c - dsp/opl.a56, the YM3812 (OPL2) and the YMF262 (OPL3) on the DSP
 * (phase 10, step 7), on
 * Hatari's DSP core, against its model (src/oplfast.c), bit for bit: each
 * test file (tests/opl/mkvgm.py's) through src/opld.c, its commands sent to
 * both a tick at a time as the 68030 sends them, the samples between (RUN)
 * compared word for word. Then the worst cases for time: nine channels in
 * FM, feedback 7, tremolo and vibrato on every operator; and the rhythm
 * mode with its five drums and six channels; their cycles a sample against
 * a stock Falcon's DSP at 24,585 Hz (32 MHz: 1,301). The OPL3's worst cases
 * (eighteen channels; the rhythm mode with the second bank; four-operator
 * pairs) against its 16,390 Hz (1,952), the rate EPlay takes when a file
 * keys its second bank (Erez, 13 Nov 2026).
 *
 *	sim FILE.VGM...
 */
#include <math.h>
#include <string.h>
#include "opl_prog.h"
#include "../dspsim.h"
#include "opld.h"
#include "oplfast.h"

#define RATE 24585L
#define RATE3 16390L		/* (an OPL3 with its second bank in use) */

static OPLD chip;
static OPLF model;
static long pend;		/* samples to RUN before the next command */
static long bad_at = -1, nsamp;
static unsigned long long run_cycles;

static void flush(void)
{
	static long lr[2 * 4096];
	long i, n = pend;
	unsigned long long t;
	if (!n)
		return;
	pend = 0;
	put(OPLC_RUN);
	put((unsigned long)n);
	t = cycles;
	profiling = getenv("SIMPROF") != NULL;
	oplf_words(&model, lr, (int)n);
	for (i = 0; i < 2 * n; i++) {
		long w = get() & 0xffffff;
		if (dead)
			return;
		if (w != lr[i] && bad_at < 0) {
			bad_at = nsamp + i / 2;
			fprintf(stderr, "sample %ld %s: the DSP %06lx, the model %06lx\n", bad_at, i & 1 ? "right" : "left",
				w, lr[i]);
			if (getenv("OPLDUMP")) {
				int c = atoi(getenv("OPLDUMP")), k, j;
				for (k = 0; k < 2; k++) {
					OPLF_OP *o = &model.ch[c].op[k];
					int x = 0x64 + 4 * c + 2 * k;
					fprintf(stderr, " op%d model: mode %d att %06lx gain %06lx phase %06lx step_e %06lx\n  DSP:",
						k, o->mode, o->att & 0xffffff, o->gain, o->phase, o->step_e);
					for (j = 0; j < 12; j++)
						fprintf(stderr, " %06x", dsp_core.ramext[0x4000 + 0x0c00 + 16 * (c * 2 + k) + j]);
					fprintf(stderr, " | P %06x G %06x S %06x W %06x\n", dsp_core.ramint[0][x],
						dsp_core.ramint[0][x + 1], dsp_core.ramint[1][x], dsp_core.ramint[1][x + 1]);
				}
				fprintf(stderr, " live %d, DSP CHS:", model.ch[c].live);
				for (j = 0; j < 3; j++)
					fprintf(stderr, " %06x", dsp_core.ramext[0x4000 + 0x0e40 + 8 * c + j]);
				fprintf(stderr, " HND %06x VOUT %06x\n", dsp_core.ramint[0][0x3e + 2 * c], dsp_core.ramint[0][0x41 + 2 * c]);
			}
		}
	}
	profiling = 0;
	run_cycles += cycles - t;
	nsamp += n;
}

static void both(void *u, long w)
{
	(void)u;
	flush();
	if (getenv("OPLW") && nsamp > atol(getenv("OPLW")))
		fprintf(stderr, "@%ld %ld\n", nsamp, w & 0xffffff);
	put((unsigned long)(w & 0xffffff));
	oplf_word(&model, w);
}

static void tables(void)
{
	load(1, OPL_X_GAIN, (const unsigned long *)model.gain_tab, OPLF_GAINS);
	load(1, OPL_X_AM, (const unsigned long *)model.am_tab, 512);
	load(1, OPL_X_PMW, (const unsigned long *)model.pmw, 8);
	load(4, OPL_Y_WAVE, (const unsigned long *)model.wave, 8192);
	put(OPLC_RESET);
	if (getenv("OPLTAB")) {
		int i, bad = 0;
		put(OPLC_RUN); put(1); get(); get();	/* (the DSP to take the words) */
		for (i = 0; i < 8192; i++)
			if ((dsp_core.ramext[0x2000 + i] & 0xffffff) != (unsigned long)(((const long *)model.wave)[i] & 0xffffff) && bad++ < 5)
				fprintf(stderr, "Y:%04x the DSP %06x, the model %06lx\n", 0x2000 + i, dsp_core.ramext[0x2000 + i],
					((const long *)model.wave)[i] & 0xffffff);
		fprintf(stderr, "waveforms: %d words differ\n", bad);
	}
}

#define TICK_HZ (2457600.0 / 16 / 154)
#define STEP 2897647UL

static unsigned char *vb;
static long vn, vp, vwait;

static unsigned long u32(const unsigned char *q)
{
	return (unsigned long)q[0] | (unsigned long)q[1] << 8 | (unsigned long)q[2] << 16 | (unsigned long)q[3] << 24;
}

/* (tests/opl/fast.c's reader: the waits, 0x5A, an OPL3's 0x5E and 0x5F) */
static int run(long k)
{
	while (vp < vn) {
		unsigned c = vb[vp];
		if (vwait >= k) {
			vwait -= k;
			return 1;
		}
		k -= vwait;
		vwait = 0;
		if (c == 0x5A || c == 0x5E || c == 0x5F) {
			opld_write(&chip, vb[vp + 1] | (c == 0x5F ? 0x100 : 0), vb[vp + 2]);
			vp += 3;
		} else if (c == 0x66)
			return 0;
		else if (c == 0x61)
			vwait = vb[vp + 1] | vb[vp + 2] << 8, vp += 3;
		else if ((c & 0xf0) == 0x70)
			vwait = (c & 15) + 1, vp++;
		else
			return 0;
	}
	return 0;
}

static int play(const char *path)
{
	FILE *f = fopen(path, "rb");
	double tick = 1;
	unsigned long vacc = 0;
	int live = 1;
	if (!f)
		return 1;
	fseek(f, 0, SEEK_END);
	vn = ftell(f);
	fseek(f, 0, SEEK_SET);
	vb = malloc((size_t)vn);
	if (fread(vb, 1, (size_t)vn, f) != (size_t)vn)
		return 1;
	fclose(f);
	vp = 0x34 + (long)u32(vb + 0x34);
	vwait = 0;
	oplf_init(&model);
	tables();
	if (u32(vb + 0x50))
		opld_init(&chip, u32(vb + 0x50), RATE);
	else {
		/* an OPL3: eighteen channels where its second bank is keyed */
		long q;
		int bank2 = 0;
		opld_init3(&chip, u32(vb + 0x5c), RATE);
		for (q = vp; q + 2 < vn && vb[q] != 0x66; ) {
			unsigned c = vb[q];
			if (c == 0x5F && (vb[q + 1] & 0xf0) == 0xb0 && (vb[q + 1] & 15) < 9 && (vb[q + 2] & 0x20))
				bank2 = 1;
			q += c == 0x5E || c == 0x5F || c == 0x5A || c == 0x61 ? 3 : 1;
		}
		opld_channels(&chip, bank2 ? 18 : 9);
	}
	opld_send(&chip, both, NULL);
	bad_at = -1;
	nsamp = 0;
	run_cycles = 0;
	while (live && !dead) {
		tick += TICK_HZ / RATE;
		if (tick >= 1) {
			tick -= 1;
			vacc += STEP;
			live = run((long)(vacc >> 16));
			opld_send(&chip, both, NULL);
			vacc &= 0xffff;
		}
		if (++pend >= 4096)
			flush();
	}
	flush();
	free(vb);
	printf("%-12s %6ld samples, %s; %.0f cycles a sample\n", strrchr(path, '/') ? strrchr(path, '/') + 1 : path,
	       nsamp, bad_at < 0 ? "the same, bit for bit" : "DIFFERENT", (double)run_cycles / (nsamp ? nsamp : 1));
	return bad_at >= 0 || dead;
}

/* the worst cases: nine channels in FM (or both out), feedback 7, every
   operator loud, on, with tremolo and vibrato, each channel at its own
   pitch; rhy: the rhythm mode, its five drums too */
static int worst(int cnt, int rhy, double *per)
{
	int c, k;
	oplf_init(&model);
	tables();
	opld_init(&chip, 3579545, RATE);
	opld_write(&chip, 0x01, 0x20);
	for (c = 0; c < 9; c++) {
		static const int op1[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
		for (k = 0; k < 2; k++) {
			int s = op1[c] + 3 * k;
			opld_write(&chip, 0x20 + s, 0xc0 | (1 + k + c % 3));
			opld_write(&chip, 0x40 + s, k ? 0 : 0x10);
			opld_write(&chip, 0x60 + s, 0xf2);
			opld_write(&chip, 0x80 + s, 0x0f);
			opld_write(&chip, 0xe0 + s, (c + k) & 3);
		}
		opld_write(&chip, 0xa0 + c, 0x40 + 16 * c);
		opld_write(&chip, 0xc0 + c, 0x0e | cnt);
		opld_write(&chip, 0xb0 + c, 0x20 | (2 + c % 4) << 2 | 1);
	}
	opld_write(&chip, 0xbd, 0xc0 | (rhy ? 0x3f : 0));
	bad_at = -1;
	nsamp = 0;
	run_cycles = 0;
	opld_send(&chip, both, NULL);
	pend = 3000;
	flush();
	*per = (double)run_cycles / nsamp;
	printf("%s, %s: %.0f cycles a sample (of 1,301), %s\n", rhy ? "the rhythm mode" : "nine channels",
	       cnt ? "both out" : "FM", *per, bad_at < 0 ? "the same, bit for bit" : "DIFFERENT");
	return bad_at >= 0 || dead;
}

/* the OPL3's worst cases, NEW set: kind 0 eighteen channels in FM, 1 both
   out, 2 the rhythm mode and the second bank's nine in FM, 3 the six
   four-operator pairs (algorithm 0, the longest chain) and six channels in
   FM, 4 the pairs at algorithm 3 (the most outputs); each operator loud,
   on, with tremolo and vibrato, feedback 7, both outputs */
static int worst3(int kind, double *per)
{
	static const int op1[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
	static const char *what[7] = { "eighteen channels, FM", "eighteen channels, both out",
				       "the rhythm mode and nine in FM", "six pairs (algorithm 0) and six in FM",
				       "six pairs (algorithm 3) and six in FM", "six pairs (algorithm 1) and six in FM",
				       "six pairs (algorithm 2) and six in FM" };
	int c, k;
	oplf_init(&model);
	tables();
	opld_init3(&chip, 14318180, RATE3);
	opld_channels(&chip, 18);
	opld_write(&chip, 0x105, 1);
	opld_write(&chip, 0x104, kind >= 3 ? 0x3f : 0);	/* (kinds 3-6: the six pairs) */
	for (c = 0; c < 18; c++) {
		int b = c >= 9 ? 0x100 : 0, cc = c % 9, cnt = kind == 1 ? 1 : 0;
		if (kind == 4 && cc < 6)
			cnt = 1;	/* (each pair's both channels' 1: algorithm 3) */
		if (kind == 5 && cc >= 3 && cc < 6)
			cnt = 1;	/* (the second's: algorithm 1) */
		if (kind == 6 && cc < 3)
			cnt = 1;	/* (the first's: algorithm 2) */
		for (k = 0; k < 2; k++) {
			int s = b + op1[cc] + 3 * k;
			opld_write(&chip, 0x20 + s, 0xc0 | (1 + k + c % 3));
			opld_write(&chip, 0x40 + s, k ? 0 : 0x10);
			opld_write(&chip, 0x60 + s, 0xf2);
			opld_write(&chip, 0x80 + s, 0x0f);
			opld_write(&chip, 0xe0 + s, (c + k) & 7);
		}
		opld_write(&chip, b + 0xa0 + cc, 0x40 + 16 * cc);
		opld_write(&chip, b + 0xc0 + cc, 0x30 | 0x0e | cnt);
		opld_write(&chip, b + 0xb0 + cc, 0x20 | (2 + c % 4) << 2 | 1);
	}
	opld_write(&chip, 0xbd, 0xc0 | (kind == 2 ? 0x3f : 0));
	bad_at = -1;
	nsamp = 0;
	run_cycles = 0;
	opld_send(&chip, both, NULL);
	pend = 3000;
	flush();
	*per = (double)run_cycles / nsamp;
	printf("OPL3, %s: %.0f cycles a sample (of 1,952), %s\n", what[kind], *per,
	       bad_at < 0 ? "the same, bit for bit" : "DIFFERENT");
	return bad_at >= 0 || dead;
}

int main(int argc, char **argv)
{
	int i, bad = 0;
	double most = 0, most3 = 0, per;
	sim_start(dsp_opl_prog);
	load(6, DSP_OPL_PROG_EXT_AT, dsp_opl_prog_ext, DSP_OPL_PROG_EXT_N);
	for (i = 1; i < argc && !dead; i++)
		bad |= play(argv[i]);
	if (getenv("OPLWORST3")) {	/* (one OPL3 worst case alone: for SIMPROF) */
		worst3(atoi(getenv("OPLWORST3")), &per);
		sim_done();
		return 0;
	}
	for (i = 0; i < 4 && !dead; i++) {
		bad |= worst(i & 1, i >> 1, &per);
		if (per > most)
			most = per;
	}
	for (i = 0; i < 7 && !dead; i++) {
		bad |= worst3(i, &per);
		if (per > most3)
			most3 = per;
	}
	/* (10 %% kept for the SSI's interrupts, the host port and the 68030's commands) */
	printf("the worst: %.0f cycles a sample, %.0f%% of a stock Falcon's DSP at 24,585 Hz\n", most,
	       most * 100 / (32000000.0 / RATE));
	printf("the OPL3's worst: %.0f cycles a sample, %.0f%% of a stock Falcon's DSP at 16,390 Hz\n", most3,
	       most3 * 100 / (32000000.0 / RATE3));
	if (most > 0.9 * 32000000.0 / RATE || most3 > 0.9 * 32000000.0 / RATE3)
		bad = 1;
	if (sim_done())
		bad = 1;
	printf(bad ? "opl.a56: FAILED\n" : "opl.a56: all passed\n");
	return bad;
}
