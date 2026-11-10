/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sim.c - dsp/opn.a56, the YM2612 on the DSP (phase 10), on Hatari's DSP
 * core, against its model (src/opnfast.c), bit for bit: each test file
 * (tests/ym2612/mkvgm.py's) through src/opnd.c, its commands sent to both
 * a tick at a time as the 68030 sends them (opnd_tick), the samples
 * between (RUN) compared word for word. Then the worst case for
 * time: all six channels live, feedback 7, each algorithm; its cycles a
 * sample against a stock Falcon's DSP at 24,585 Hz (32 MHz: 1,301).
 *
 *	sim FILE.VGM...
 */
#include <math.h>
#include <string.h>
#include "opn_prog.h"
#include "../dspsim.h"
#include "vgm.h"
#include "opnd.h"
#include "opnfast.h"

#define RATE 24585L

static OPND chip;
static OPNF model;
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
	put(OPNC_RUN);
	put((unsigned long)n);
	t = cycles;
	profiling = getenv("SIMPROF") != NULL;
	opnf_words(&model, lr, (int)n);
	for (i = 0; i < 2 * n; i++) {
		long w = get() & 0xffffff;
		if (dead)
			return;
		if (w != lr[i] && bad_at < 0) {
			bad_at = nsamp + i / 2;
			fprintf(stderr, "sample %ld %s: the DSP %06lx, the model %06lx\n", bad_at, i & 1 ? "right" : "left",
				w, lr[i]);
			if (getenv("OPNDUMP")) {
				int c = atoi(getenv("OPNDUMP")), k, j;
				for (k = 0; k < 4; k++) {
					OPNF_OP *o = &model.ch[c].op[k];
					fprintf(stderr, " op%d model: mode %d att %06lx gain %06lx phase %06lx step_e %06lx\n  DSP:",
						k, o->mode, o->att & 0xffffff, o->gain, o->phase, o->step_e);
					for (j = 0; j < 11; j++)
						fprintf(stderr, " %06x", dsp_core.ramext[0x4000 + 0x1400 + 16 * (c * 4 + k) + j]);
					fprintf(stderr, " | G %06x P %06x S %06x\n", dsp_core.ramint[0][0x40 + 8 * c + 2 * k + 1],
						dsp_core.ramint[0][0x40 + 8 * c + 2 * k], dsp_core.ramint[1][0x40 + 8 * c + 2 * k]);
				}
				fprintf(stderr, " live %d, DSP CHS:", model.ch[c].live);
				for (j = 0; j < 4; j++)
					fprintf(stderr, " %06x", dsp_core.ramext[0x4000 + 0x1600 + 8 * c + j]);
				fprintf(stderr, " HND %06x\n", dsp_core.ramint[0][0x10 + c]);
				fprintf(stderr, " DAC: DSP on %x v %06x wp %x rp %x rf %06x cnt %d si %d sf %06x q0 %06x; model on %d v %06lx wp %d rp %d cnt %d si %ld sf %06lx q0 %06lx\n",
					dsp_core.ramint[0][0x0a], dsp_core.ramint[0][0x0b], dsp_core.ramint[0][0x32],
					dsp_core.ramint[0][0x33], dsp_core.ramint[0][0x34], dsp_core.ramint[0][0x35],
					dsp_core.ramint[0][0x36], dsp_core.ramint[0][0x37], dsp_core.ramext[0x1400],
					model.dac_on, model.dac & 0xffffff, model.dwp, model.drp, model.dcnt, model.dsi, model.dsf,
					model.dq[0] & 0xffffff);
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
	opnf_word(&model, w);
}

static void sample(void)
{
	if (++pend >= (getenv("OPNBATCH") ? atol(getenv("OPNBATCH")) : 4096))
		flush();
}

#define TICK_HZ (2457600.0 / 16 / 154)
#define STEP 2897647UL

static int vstep(void *v, long n)
{
	return vgm_run(v, n);
}

static long at(void *v)
{
	return ((VGMRUN *)v)->at;
}

static void opn(void *u, int port, int reg, int val)
{
	(void)u;
	opnd_write(&chip, port, reg, val);
}

static void tables(void)
{
	load(1, OPN_X_GAIN, (const unsigned long *)model.gain_tab, OPNF_GAINS);
	load(1, OPN_X_TRI, (const unsigned long *)model.triw, 128);
	load(1, OPN_X_PMW, (const unsigned long *)model.pmw, 128);
	load(4, OPN_Y_SIN, (const unsigned long *)model.sin_tab, 1024);
	put(12);			/* RESET */
}

static int play(const char *path)
{
	FILE *f = fopen(path, "rb");
	unsigned char *b;
	long n, i, total;
	double tick = 1;
	unsigned long vacc = 0;
	VGMHEAD h;
	VGMRUN r;
	if (!f)
		return 1;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc((size_t)n);
	if (fread(b, 1, (size_t)n, f) != (size_t)n || !vgm_head(b, n, &h) || !h.ym2612_clock)
		return 1;
	fclose(f);
	opnf_init(&model);
	tables();
	opnd_init(&chip, h.ym2612_clock, RATE);
	vgm_begin(&r, b, n, &h, 0, 0, 0);
	r.opn_fn = opn;
	opnd_send_drate(&chip, both, NULL);
	opnd_send(&chip, both, NULL);
	bad_at = -1;
	nsamp = 0;
	run_cycles = 0;
	total = (long)((double)vgm_length(&h) / VGM_RATE * RATE);
	for (i = 0; i < total && !dead; i++) {
		tick += TICK_HZ / RATE;
		if (tick >= 1) {
			tick -= 1;
			vacc += STEP;
			if (!opnd_tick(&chip, vstep, at, &r, (long)(vacc >> 16), both, NULL))
				break;
			vacc &= 0xffff;
		}
		sample();
	}
	flush();
	free(b);
	printf("%-28s %6ld samples, %s; %.0f cycles a sample\n", strrchr(path, '/') ? strrchr(path, '/') + 1 : path,
	       nsamp, bad_at < 0 ? "the same, bit for bit" : "DIFFERENT", (double)run_cycles / (nsamp ? nsamp : 1));
	return bad_at >= 0 || dead;
}

/* the worst case: six channels, algorithm alg, feedback 7, every operator
   at its loudest and on, each at its own pitch */
static int worst(int alg, double *per)
{
	int c, k, dbg = getenv("OPNDBG") ? atoi(getenv("OPNDBG")) : 0;
	opnf_init(&model);
	tables();
	opnd_init(&chip, 7670453, RATE);
	/* (OPNDBG, to find a difference: 1 no LFO, 2 no feedback, 4 no
	   tremolo, 8 no vibrato, 16 one channel) */
	for (c = 0; c < (dbg & 16 ? 1 : 6); c++) {
		int port = c / 3, ch = c % 3;
		for (k = 0; k < 4; k++) {
			int o = ch + 4 * k;
			opnd_write(&chip, port, 0x30 + o, 0x01 + k);
			opnd_write(&chip, port, 0x40 + o, k == 3 ? 0 : 0x10);
			opnd_write(&chip, port, 0x50 + o, 0x1f);
			opnd_write(&chip, port, 0x60 + o, dbg & 4 ? 0 : 0x80);
			opnd_write(&chip, port, 0x80 + o, 0x0f);
		}
		opnd_write(&chip, port, 0xa4 + ch, 0x20 + c);
		opnd_write(&chip, port, 0xa0 + ch, 0x40 + 16 * c);
		opnd_write(&chip, port, 0xb0 + ch, (dbg & 2 ? 0 : 0x38) | alg);
		opnd_write(&chip, port, 0xb4 + ch, dbg & 8 ? 0xc0 : 0xc7);
		opnd_write(&chip, 0, 0x28, 0xf0 | (port ? 4 : 0) | ch);
	}
	if (!(dbg & 1))
		opnd_write(&chip, 0, 0x22, 0x0f);
	bad_at = -1;
	nsamp = 0;
	run_cycles = 0;
	opnd_send(&chip, both, NULL);
	pend = 2000;
	flush();
	*per = (double)run_cycles / nsamp;
	printf("algorithm %d, six channels: %.0f cycles a sample (of 1,301), %s\n", alg, *per,
	       bad_at < 0 ? "the same, bit for bit" : "DIFFERENT");
	return bad_at >= 0 || dead;
}

int main(int argc, char **argv)
{
	int i, bad = 0;
	double most = 0, per;
	sim_start(dsp_opn_prog);
	load(6, DSP_OPN_PROG_EXT_AT, dsp_opn_prog_ext, DSP_OPN_PROG_EXT_N);
	for (i = 1; i < argc && !dead; i++)
		bad |= play(argv[i]);
	for (i = getenv("OPNALG") ? atoi(getenv("OPNALG")) : 0; i < (getenv("OPNALG") ? atoi(getenv("OPNALG")) + 1 : 8) && !dead; i++) {
		bad |= worst(i, &per);
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
	printf(bad ? "opn.a56: FAILED\n" : "opn.a56: all passed\n");
	return bad;
}
