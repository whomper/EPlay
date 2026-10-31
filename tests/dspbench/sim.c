/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sim.c - runs bench.a56 on Hatari's DSP56001 core (common's
 * tests/ec/fetch-hatari.sh), loaded through the bootstrap as on the
 * Falcon, and prints the DSP's clock cycles between its marks, with what
 * they come to for 44.1 kHz stereo: 2 channels x 2 granules a frame,
 * 44100 / 1152 frames a second, on a 32 MHz DSP.
 */
#include <stdio.h>
#include <stdint.h>
#include "dsp_core.h"
#include "dsp_cpu.h"
#include "bench_prog.h"

void DSP_SsiTransmit_SC1(uint32_t v) { (void)v; }
void DSP_SsiTransmit_SC2(uint32_t v) { (void)v; }
void dsp56k_disasm_init(void) {}
uint16_t dsp56k_disasm(int mode, FILE *f) { (void)mode; (void)f; return 0; }
const char *dsp56k_getInstructionText(void) { return ""; }
static void hint(int s) { (void)s; }

static unsigned long long cycles;

static int step(void)
{
	if (!dsp_core.running)
		return 0;
	dsp56k_execute_instruction();
	cycles += dsp_core.instr_cycle;
	return 1;
}

static void put(unsigned long w)
{
	while (!(dsp_core_read_host(CPU_HOST_ISR) & 2))
		step();
	dsp_core_write_host(CPU_HOST_TRXH, (w >> 16) & 255);
	dsp_core_write_host(CPU_HOST_TRXM, (w >> 8) & 255);
	dsp_core_write_host(CPU_HOST_TRXL, w & 255);
}

static int mark(void)
{
	long n = 0;
	while (!(dsp_core_read_host(CPU_HOST_ISR) & 1))
		if (!step() || ++n > 100000000)
			return 0;
	dsp_core_read_host(CPU_HOST_RXH);
	dsp_core_read_host(CPU_HOST_RXM);
	dsp_core_read_host(CPU_HOST_RXL);
	return 1;
}

int main(void)
{
	static const char *what[] = {
		"synthesis, window taps in external Y",
		"synthesis, window taps in internal Y",
		"inverse transform, window, overlap",
	};
	const double per_s = 2 * 2 * 44100.0 / 1152;	/* granule-channels a second */
	unsigned long long t;
	int i;

	dsp_core_init(hint);
	dsp56k_init_cpu();
	dsp_core_reset();
	for (i = 0; i < 512; i++)
		put(dsp_bench_prog[i]);
	if (!mark())
		return 1;
	for (i = 0; i < 3; i++) {
		t = cycles;
		if (!mark())
			return 1;
		t = cycles - t;
		printf("%-40s %7llu cycles a granule-channel: %4.1f instr/sample, %4.1f%% of the DSP\n",
		       what[i], t, t / 2.0 / 576, 100.0 * t * per_s / 32e6);
	}
	return 0;
}
