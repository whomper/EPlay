/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * play.c - opn.a56's PLAY as the Falcon plays it: the SSI's ring sent in
 * the DSP port's 128-bit frame (eight 16-bit slots, the left's word in the
 * first, the right's one slot after it), the 68030's tick every 2,005 us
 * sending its words with HF0 held as atari/dspopn.c does (SIMWORD=N: N DSP
 * clocks of the 68030's own work after each word, HF0 still held), the
 * file a VGM. Counted: the slots whose word wasn't written in time (on a
 * Falcon a click, the word before sent again), the transmit interrupt's
 * longest wait against the right's window, and, for a file with every
 * channel in the middle, the frames whose left and right words differ
 * (they must be the same) and how far apart the two sides are.
 *
 *     play FILE.VGM [RATE [K [SECONDS]]]   (24585 1: the Falcon's own clock;
 *                                           48000 2 or 44100 2: the FDI's)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../dspsim.h"
#include "opn_prog.h"
#include "opnd.h"
#include "opnfast.h"
#include "vgm.h"

static unsigned long long frame_cycles, next_slot;
static int slot0 = 1;
static long tde_miss, tde_first = -1;
static unsigned long long tde_t0, tde_max, right_win;
static int tde_wait;
static long over_half, over_three_q;
#define CAP_MAX (1L << 23)
static unsigned short *cap;
static unsigned short *cap_r7;	/* (r7 as each slot's word was taken: its ring address after) */
static long watch_addr = -1;
static unsigned long watch_last;
static long ncap, left_par = -1;

static void clock_ssi(void)
{
	while (cycles >= next_slot) {
		dsp_core_ssi_Receive_SC2(slot0);
		if (dsp_core.ssi.crb_te && (dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] & (1 << DSP_SSI_SR_TDE))) {
			if (tde_first < 0)
				tde_first = ncap;
			tde_miss++;
		}
		if (slot0 && left_par < 0 && dsp_core.ssi.crb_te)
			left_par = ncap & 1;
		dsp_core_ssi_Receive_SCK();
		if (ncap < CAP_MAX) {
			cap_r7[ncap] = (unsigned short)dsp_core.registers[DSP_REG_R7];
			cap[ncap++] = (unsigned short)dsp_core.ssi.transmit_value;
		}
		if (dsp_core.ssi.crb_te && (dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] & (1 << DSP_SSI_SR_TDE))) {
			tde_t0 = cycles;
			tde_wait = 1;
		}
		slot0 = !slot0;
		/* (the right's slot one 16-bit slot after the left's, the left's
		   seven after the right's) */
		next_slot += (slot0 ? 7 : 1) * frame_cycles / 8;
	}
}

static int sstep(void)
{
	unsigned pc = dsp_core.pc;
	int r = step();
	if (watch_addr >= 0 && dsp_core.ramext[watch_addr] != watch_last) {
		printf("    y:%04lx %06lx -> %06x at P:%04x (frame %ld, r6 %04x r7 %04x)\n", watch_addr, watch_last,
		       dsp_core.ramext[watch_addr], pc, ncap / 2, dsp_core.registers[DSP_REG_R6], dsp_core.registers[DSP_REG_R7]);
		watch_last = dsp_core.ramext[watch_addr];
	}
	clock_ssi();
	if (tde_wait && !(dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] & (1 << DSP_SSI_SR_TDE))) {
		unsigned long long w = cycles - tde_t0;
		tde_wait = 0;
		if (w > tde_max)
			tde_max = w;
		if (w * 2 > right_win)
			over_half++;
		if (w * 4 > right_win * 3)
			over_three_q++;
	}
	return r;
}

static int hf0;
static long word_cost;

static void hflag(int on)
{
	hf0 = on;
	dsp_core_write_host(CPU_HOST_ICR, on ? 1 << CPU_HOST_ICR_HF0 : 0);
}

static void hput(void *u, long w)
{
	long n = 0;
	unsigned long long t;
	(void)u;
	if (!hf0)
		hflag(1);
	while (!(dsp_core_read_host(CPU_HOST_ISR) & 2))
		if (!sstep() || ++n > 4000000) {
			dead = 1;
			return;
		}
	dsp_core_write_host(CPU_HOST_TRXH, ((unsigned long)w >> 16) & 255);
	dsp_core_write_host(CPU_HOST_TRXM, ((unsigned long)w >> 8) & 255);
	dsp_core_write_host(CPU_HOST_TRXL, (unsigned long)w & 255);
	/* (the 68030's work for the next word, HF0 held) */
	t = cycles + (unsigned long long)word_cost;
	while (cycles < t && !dead)
		if (!sstep())
			dead = 1;
}

static OPND chip;
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

int main(int argc, char **argv)
{
	FILE *f;
	unsigned char *b;
	long n, rate = 24585, k = 1, synth, frames, i, diff = 0, first_diff = -1, sounding = 0;
	double secs = 30, tick_acc = 0;
	unsigned long long tick_cycles, next_tick;
	VGMHEAD h;
	VGMRUN r;
	OPNF model;
	int bad = 0, lag, best_lag = 0;
	long best = -1;
	if (argc < 2) {
		fprintf(stderr, "play FILE.VGM [RATE [K [SECONDS]]]\n");
		return 2;
	}
	if (argc > 2)
		rate = atol(argv[2]);
	if (argc > 3)
		k = atol(argv[3]);
	if (argc > 4)
		secs = atof(argv[4]);
	word_cost = getenv("SIMWORD") ? atol(getenv("SIMWORD")) : 0;
	synth = rate / k;
	cap = malloc(CAP_MAX * sizeof(*cap));
	cap_r7 = malloc(CAP_MAX * sizeof(*cap_r7));
	if (getenv("SIMWATCH"))
		watch_addr = strtol(getenv("SIMWATCH"), NULL, 16);
	f = fopen(argv[1], "rb");
	if (!f)
		return 2;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc((size_t)n);
	if (fread(b, 1, (size_t)n, f) != (size_t)n || !vgm_head(b, n, &h) || !h.ym2612_clock)
		return 2;
	fclose(f);
	/* the program and its tables, as atari/dspopn.c loads them */
	sim_start(dsp_opn_prog);
	load(6, DSP_OPN_PROG_EXT_AT, dsp_opn_prog_ext, DSP_OPN_PROG_EXT_N);
	opnf_init(&model);
	load(1, OPN_X_GAIN, (const unsigned long *)model.gain_tab, OPNF_GAINS);
	load(1, OPN_X_TRI, (const unsigned long *)model.triw, 128);
	load(1, OPN_X_PMW, (const unsigned long *)model.pmw, 128);
	load(4, OPN_Y_SIN, (const unsigned long *)model.sin_tab, 1024);
	put(OPNC_RESET);
	opnd_init(&chip, h.ym2612_clock, synth);
	vgm_begin(&r, b, n, &h, 0, 0, 0);
	r.opn_fn = opn;
	r.dac = &chip.dq;
	frame_cycles = (unsigned long long)(32000000.0 / rate + 0.5);
	right_win = frame_cycles / 8;
	opnd_send_drate(&chip, hput, NULL);
	opnd_send(&chip, hput, NULL);
	hput(NULL, OPNC_PLAY);
	hput(NULL, k);
	hflag(0);
	next_slot = cycles + frame_cycles;
	tick_cycles = 32000000ULL * 2005 / 1000000;
	next_tick = cycles + tick_cycles;
	frames = (long)(secs * rate);
	while (ncap < 2 * frames && !dead) {
		if (!sstep()) {
			dead = 1;
			break;
		}
		if (cycles >= next_tick && getenv("SIMSTOP") && (double)ncap / 2 / rate > atof(getenv("SIMSTOP")))
			next_tick = ~0ULL;	/* (SIMSTOP=S: no tick after S seconds: the notes held) */
		if (cycles >= next_tick) {
			/* the tick: the VGM's samples of 2,005 us (44,100 Hz) */
			long nv;
			next_tick += tick_cycles;
			tick_acc += 44100.0 * 2005 / 1000000;
			nv = (long)tick_acc;
			tick_acc -= nv;
			if (!opnd_tick(&chip, vstep, at, &r, nv, hput, NULL))
				break;
			hflag(0);
		}
	}
	/* the frames: the left's word in the frame sync's slot, the right's after */
	if (left_par < 0)
		left_par = 0;
	for (i = left_par + (getenv("SIMFROM") ? 2 * (long)(atof(getenv("SIMFROM")) * rate) : 0); i + 1 < ncap; i += 2) {
		if (cap[i] || cap[i + 1])
			sounding++;
		if (cap[i] != cap[i + 1]) {
			if (first_diff < 0)
				first_diff = (i - left_par) / 2;
			diff++;
		}
	}
	/* how far apart the sides are: the lag (in words) at which they agree most */
	for (lag = -3; lag <= 3; lag++) {
		long same = 0;
		for (i = left_par + 8; i + 8 < ncap; i += 2)
			same += cap[i] == cap[i + 1 + 2 * lag];
		if (same > best) {
			best = same;
			best_lag = lag;
		}
	}
	printf("%s at %ld Hz (k %ld), %.1f s, SIMWORD %ld: %ld frames, %ld sounding\n", argv[1], rate, k,
	       (double)ncap / 2 / rate, word_cost, ncap / 2, sounding);
	printf("  slots not written in time: %ld (the first at frame %ld)\n", tde_miss, tde_first / 2);
	printf("  the transmit interrupt's longest wait: %llu clocks of the right's %llu (past half: %ld, past 3/4: %ld)\n",
	       tde_max, right_win, over_half, over_three_q);
	printf("  frames whose left and right differ: %ld of %ld (the first at %ld = %.3f s); the sides agree most %+d frames apart\n",
	       diff, (ncap - left_par) / 2, first_diff, first_diff < 0 ? 0.0 : (double)first_diff / rate, best_lag);
	if (getenv("SIMDUMP") && first_diff >= 0) {
		long a = left_par + 2 * (first_diff - 6), j;
		for (j = 0; j < 24 && a + 2 * j + 1 < ncap; j++)
			printf("    frame %ld: L %04x R %04x%s (r7 after: %04x %04x)\n", (a - left_par) / 2 + j, cap[a + 2 * j], cap[a + 2 * j + 1],
			       cap[a + 2 * j] != cap[a + 2 * j + 1] ? "  <<" : "", cap_r7[a + 2 * j], cap_r7[a + 2 * j + 1]);
	}
	bad = dead || tde_miss > 0 || diff > 0;
	printf(bad ? "play: FAILED\n" : "play: all passed\n");
	free(b);
	free(cap);
	return bad;
}
