/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sim.c - dsp/sn.a56, the SN76489 on the DSP (phase 9), on Hatari's DSP
 * core: loaded through the bootstrap, the SSI clocked as the matrix clocks
 * it, the commands atari/dspsn.c sends for a tone of period 112 at the
 * Master System's 3,579,545 Hz (998.8 Hz), at the loudest; what the SSI
 * sends measured: its pitch (from the crossings), its level, left and
 * right the same (the slots paired either way: the chip is mono); then the noise (white) on its own: its level, and no
 * tone in it; then silence.
 */
#include <math.h>
#include <string.h>
#include "sn_prog.h"
#include "../dspsim.h"

static unsigned long long slot_cycles = 325, next_slot;
static int slot0 = 1;
#define CAP_MAX (1L << 20)
static short cap[CAP_MAX];
static long ncap;

static void clock_ssi(void)
{
	while (cycles >= next_slot) {
		dsp_core_ssi_Receive_SC2(slot0);
		dsp_core_ssi_Receive_SCK();
		if (ncap < CAP_MAX)
			cap[ncap++] = (short)dsp_core.ssi.transmit_value;
		if (getenv("SIMRING")) {
			static int lo = 999, hi = -1;
			int d = (int)((dsp_core.registers[DSP_REG_R6] - dsp_core.registers[DSP_REG_R7]) & 255);
			if (d < lo || d > hi) {
				if (d < lo) lo = d;
				if (d > hi) hi = d;
				fprintf(stderr, "ring lead %d..%d at slot %ld\n", lo, hi, ncap);
			}
		}
		slot0 = !slot0;
		next_slot += slot_cycles;
	}
}

static void run(unsigned long long n)
{
	unsigned long long end = cycles + n;
	while (cycles < end && !dead) {
		if (!step())
			dead = 1;
		clock_ssi();
	}
}

static void cmd(int c, unsigned long v)
{
	put((unsigned long)c);
	clock_ssi();
	put(v & 0xffffff);
	clock_ssi();
}

/* (as atari/dspsn.c works them out) */
static long q11;
static long step_of(long n)
{
	long s = (q11 << 12) / n;
	return s >= (1L << 22) ? 0 : s;
}

int main(void)
{
	long rate = 32000000L / (2 * 325), i, cross = 0, start;
	double sum = 0, lr = 0, lr2 = 0;
	int bad = 0;
	sim_start(dsp_sn_prog);
	run(32000000ULL / 100);		/* (its start waits for the SSI's frame) */
	q11 = (long)((3579545L / 16) << 11) / rate;
	cmd(9, 9);
	cmd(10, 0x8000);
	cmd(8, 1);
	cmd(0, (unsigned long)step_of(112));
	cmd(4, 1677722);
	run(32000000ULL / 10);		/* 0.1 s to settle */
	start = ncap;
	run(32000000ULL);		/* a second */
	for (i = start + 2; i + 1 < ncap; i += 2) {
		if ((cap[i] >= 0) != (cap[i - 2] >= 0))
			cross++;
		sum += (double)cap[i] * cap[i];
		lr += fabs((double)cap[i] - cap[i + 1]);
		lr2 += fabs((double)cap[i] - cap[i - 1]);
	}
	if (getenv("SIMRUNS")) {
		long run = 1, k = 0;
		for (i = start + 2; i < ncap && k < 40; i += 2) {
			if ((cap[i] >= 0) == (cap[i - 2] >= 0))
				run++;
			else {
				fprintf(stderr, "%ld ", run);
				run = 1;
				k++;
			}
		}
		fprintf(stderr, "\n");
	}
	{
		long n = (ncap - start) / 2;
		double hz = cross / 2.0 * rate / n, rms = sqrt(sum / n);
		if (lr2 < lr)
			lr = lr2;	/* (the frame's left and right: either pairing of the slots) */
		printf("sn.a56: %ld Hz output; tone %.1f Hz (998.8 wanted), level %.0f, left and right differ by %.0f\n",
		       rate, hz, rms, lr);
		if (fabs(hz - 998.8) > 3 || rms < 5000 || rms > 7000 || lr > 0)
			bad = 1;
	}
	/* the noise alone: white, at clock / 512 */
	cmd(4, 0);
	cmd(0, 0);
	cmd(3, (unsigned long)step_of(32));
	cmd(7, 1677722);
	run(32000000ULL / 10);
	start = ncap;
	sum = 0;
	cross = 0;
	run(32000000ULL / 2);
	for (i = start + 2; i < ncap; i += 2) {
		if ((cap[i] >= 0) != (cap[i - 2] >= 0))
			cross++;
		sum += (double)cap[i] * cap[i];
	}
	{
		long n = (ncap - start) / 2;
		double rms = sqrt(sum / n), per_s = cross * (double)rate / n;
		printf("sn.a56: noise level %.0f, %.0f crossings a second (a random sign at 6,991 shifts a second: about 3,500)\n",
		       rms, per_s);
		if (rms < 5000 || per_s < 2500 || per_s > 4500)
			bad = 1;
	}
	cmd(7, 0);
	run(32000000ULL / 10);
	start = ncap;
	run(32000000ULL / 10);
	for (i = start; i < ncap; i++)
		if (cap[i]) {
			printf("sn.a56: not silent at the end\n");
			bad = 1;
			break;
		}
	if (sim_done())
		bad = 1;
	printf(bad ? "sn.a56: FAILED\n" : "sn.a56: all passed\n");
	return bad;
}
