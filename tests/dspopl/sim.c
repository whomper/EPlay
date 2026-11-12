/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sim.c - dsp/opl.a56, the YM3812 (OPL2) on the DSP (phase 10, step 7), on
 * Hatari's DSP core, against its model (src/oplfast.c), bit for bit: each
 * test file (tests/opl/mkvgm.py's) through src/opld.c, its commands sent to
 * both a tick at a time as the 68030 sends them, the samples between (RUN)
 * compared word for word. Then the worst cases for time: nine channels in
 * FM, feedback 7, tremolo and vibrato on every operator; and the rhythm
 * mode with its five drums and six channels; their cycles a sample against
 * a stock Falcon's DSP at 24,585 Hz (32 MHz: 1,301).
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
					int x = 0x50 + 4 * c + 2 * k;
					fprintf(stderr, " op%d model: mode %d att %06lx gain %06lx phase %06lx step_e %06lx\n  DSP:",
						k, o->mode, o->att & 0xffffff, o->gain, o->phase, o->step_e);
					for (j = 0; j < 12; j++)
						fprintf(stderr, " %06x", dsp_core.ramext[0x4000 + 0x0c00 + 16 * (c * 2 + k) + j]);
					fprintf(stderr, " | P %06x G %06x S %06x W %06x\n", dsp_core.ramint[0][x],
						dsp_core.ramint[0][x + 1], dsp_core.ramint[1][x], dsp_core.ramint[1][x + 1]);
				}
				fprintf(stderr, " live %d, DSP CHS:", model.ch[c].live);
				for (j = 0; j < 3; j++)
					fprintf(stderr, " %06x", dsp_core.ramext[0x4000 + 0x0e00 + 8 * c + j]);
				fprintf(stderr, " HND %06x VOUT %06x\n", dsp_core.ramint[0][0x10 + c], dsp_core.ramint[0][0x20 + c]);
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
	put((unsigned long)(w & 0xffffff));
	oplf_word(&model, w);
}

static void tables(void)
{
	load(1, OPL_X_GAIN, (const unsigned long *)model.gain_tab, OPLF_GAINS);
	load(1, OPL_X_AM, (const unsigned long *)model.am_tab, 512);
	load(1, OPL_X_PMW, (const unsigned long *)model.pmw, 8);
	load(4, OPL_Y_WAVE, (const unsigned long *)model.wave, 4096);
	put(OPLC_RESET);
}

#define TICK_HZ (2457600.0 / 16 / 154)
#define STEP 2897647UL

static unsigned char *vb;
static long vn, vp, vwait;

static unsigned long u32(const unsigned char *q)
{
	return (unsigned long)q[0] | (unsigned long)q[1] << 8 | (unsigned long)q[2] << 16 | (unsigned long)q[3] << 24;
}

/* (tests/opl/fast.c's reader: the waits and 0x5A) */
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
		if (c == 0x5A) {
			opld_write(&chip, vb[vp + 1], vb[vp + 2]);
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
	opld_init(&chip, u32(vb + 0x50), RATE);
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

int main(int argc, char **argv)
{
	int i, bad = 0;
	double most = 0, per;
	sim_start(dsp_opl_prog);
	load(6, DSP_OPL_PROG_EXT_AT, dsp_opl_prog_ext, DSP_OPL_PROG_EXT_N);
	for (i = 1; i < argc && !dead; i++)
		bad |= play(argv[i]);
	for (i = 0; i < 4 && !dead; i++) {
		bad |= worst(i & 1, i >> 1, &per);
		if (per > most)
			most = per;
	}
	/* (10 %% kept for the SSI's interrupts, the host port and the 68030's commands) */
	printf("the worst: %.0f cycles a sample, %.0f%% of a stock Falcon's DSP at 24,585 Hz\n", most,
	       most * 100 / (32000000.0 / RATE));
	if (most > 0.9 * 32000000.0 / RATE)
		bad = 1;
	if (sim_done())
		bad = 1;
	printf(bad ? "opl.a56: FAILED\n" : "opl.a56: all passed\n");
	return bad;
}
