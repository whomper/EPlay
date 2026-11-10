/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sim.c - fm.a56 on Hatari's DSP core (phase 9, step 3): the cycles a
 * YM2612-like sample takes (24 operators, algorithm 0, an envelope step
 * each), and the output rates a stock Falcon's DSP (32 MHz) could keep
 * up with, with a share of its time left for the SSI and the host port.
 */
#include <string.h>
#include "fm_prog.h"
#include "../dspsim.h"

int main(void)
{
	const long n = 1000;
	unsigned long long c;
	double per, at;
	sim_start(dsp_fm_prog);
	put((unsigned long)n);
	c = sim_job();
	if (sim_done())
		return 1;
	per = (double)c / n;
	printf("fm.a56: %llu cycles for %ld samples: %.0f a sample (24 operators, %.1f an operator)\n", c, n, per,
	       per / 24);
	/* (the DSP's clock 32 MHz; 10 %% kept for the SSI's interrupt, the
	   host port's polls and the 68030's register writes) */
	at = 32000000.0 * 0.9 / per;
	printf("fm.a56: a stock Falcon's DSP keeps up to %.0f samples a second "
	       "(the chip's own 53,267; the Falcon's 49,170, 32,780, 24,585)\n", at);
	return 0;
}
