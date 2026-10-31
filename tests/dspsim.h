/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspsim.h - what the DSP tests share: Hatari's DSP56001 core (common's
 * tests/ec/fetch-hatari.sh) behind the Falcon's host port, a program
 * loaded through the bootstrap, words both ways, tables loaded with the
 * test programs' LOADX (1) and LOADY (4), and the cycles counted (and, with
 * SIMPROF=FILE, each address's, written by sim_done()).
 */
#ifndef DSPSIM_H
#define DSPSIM_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "dsp_core.h"
#include "dsp_cpu.h"

void DSP_SsiTransmit_SC1(uint32_t v) { (void)v; }
void DSP_SsiTransmit_SC2(uint32_t v) { (void)v; }
void dsp56k_disasm_init(void) {}
uint16_t dsp56k_disasm(int mode, FILE *f) { (void)mode; (void)f; return 0; }
const char *dsp56k_getInstructionText(void) { return ""; }
static void sim_hint(int s) { (void)s; }

static unsigned long long cycles, prof[32768];
static int dead, profiling;
static int sim_trace;		/* instructions still to print */
static unsigned sim_trace_at;	/* SIMTRACE=ADDR (hex): print 40 from the first time there */

static int step(void)
{
	unsigned pc;
	if (!dsp_core.running)
		return 0;
	pc = dsp_core.pc;
	if (sim_trace > 0) {
		sim_trace--;
		fprintf(stderr, "P:%04x a %02x:%06x:%06x b %02x:%06x x0 %06x x1 %06x y0 %06x y1 %06x r0 %04x r1 %04x r4 %04x\n",
			pc, dsp_core.registers[DSP_REG_A2], dsp_core.registers[DSP_REG_A1],
			dsp_core.registers[DSP_REG_A0], dsp_core.registers[DSP_REG_B2],
			dsp_core.registers[DSP_REG_B1], dsp_core.registers[DSP_REG_X0],
			dsp_core.registers[DSP_REG_X1], dsp_core.registers[DSP_REG_Y0],
			dsp_core.registers[DSP_REG_Y1], dsp_core.registers[DSP_REG_R0],
			dsp_core.registers[DSP_REG_R1], dsp_core.registers[DSP_REG_R4]);
	}
	if (sim_trace_at && pc == sim_trace_at && !sim_trace) {
		sim_trace = 40;
		sim_trace_at = 0;
	}
	dsp56k_execute_instruction();
	cycles += dsp_core.instr_cycle;
	if (profiling && pc < 32768)
		prof[pc] += dsp_core.instr_cycle;
	return 1;
}

static void put(unsigned long w)
{
	long n = 0;
	while (!(dsp_core_read_host(CPU_HOST_ISR) & 2))
		if (!step() || ++n > 2000000) {
			dead = 1;
			return;
		}
	dsp_core_write_host(CPU_HOST_TRXH, (w >> 16) & 255);
	dsp_core_write_host(CPU_HOST_TRXM, (w >> 8) & 255);
	dsp_core_write_host(CPU_HOST_TRXL, w & 255);
}

static long get(void)
{
	unsigned long w;
	long n = 0;
	while (!(dsp_core_read_host(CPU_HOST_ISR) & 1))
		if (!step() || ++n > 2000000) {
			dead = 1;
			return 0;
		}
	w = (unsigned long)dsp_core_read_host(CPU_HOST_RXH) << 16;
	w |= (unsigned long)dsp_core_read_host(CPU_HOST_RXM) << 8;
	w |= dsp_core_read_host(CPU_HOST_RXL);
	return (long)((w ^ 0x800000UL) - 0x800000UL);
}

static void load(int cmd, unsigned addr, const unsigned long *w, long n)
{
	long i;
	put((unsigned long)cmd);
	put(addr);
	put((unsigned long)n);
	for (i = 0; i < n; i++)
		put(w[i] & 0xffffff);
}

static void sim_start(const unsigned long *prog)
{
	int i;
	if (getenv("SIMTRACE"))
		sim_trace_at = (unsigned)strtoul(getenv("SIMTRACE"), NULL, 16);
	dsp_core_init(sim_hint);
	dsp56k_init_cpu();
	dsp_core_reset();
	for (i = 0; i < 512; i++)
		put(prog[i]);
}

/* the cycles between the program's words 1 (ready) and 2 (done) */
static unsigned long long sim_job(void)
{
	unsigned long long t;
	if (get() != 1)
		dead = 1;
	t = cycles;
	profiling = getenv("SIMPROF") != NULL;
	if (get() != 2)
		dead = 1;
	profiling = 0;
	return cycles - t;
}

static int sim_done(void)
{
	if (getenv("SIMPROF")) {
		FILE *pf = fopen(getenv("SIMPROF"), "w");
		int i;
		for (i = 0; i < 32768; i++)
			if (prof[i])
				fprintf(pf, "%04X %llu\n", i, prof[i]);
		fclose(pf);
	}
	if (dead) {
		fprintf(stderr, "the DSP stopped answering at P:%04x\n", dsp_core.pc);
		return 1;
	}
	return 0;
}

#endif
