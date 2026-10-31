/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sim.c - dsp/huff.a56 on Hatari's DSP56001 core (common's
 * tests/ec/fetch-hatari.sh) against the model's huffman() (src/mp3), bit
 * for bit: every granule of every channel of the files given, through the
 * model, then through the DSP, its 576 values compared. Prints the DSP's
 * cycles for the Huffman part and what they come to on a 32 MHz DSP for
 * the file's own rate and bit rate.
 *
 *     sim FILE.MP3...
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "dsp_core.h"
#include "dsp_cpu.h"
#include "mp3frame.h"
#include "mp3dsp.h"
#include "mp3tab.h"
#include "huff_prog.h"

void DSP_SsiTransmit_SC1(uint32_t v) { (void)v; }
void DSP_SsiTransmit_SC2(uint32_t v) { (void)v; }
void dsp56k_disasm_init(void) {}
uint16_t dsp56k_disasm(int mode, FILE *f) { (void)mode; (void)f; return 0; }
const char *dsp56k_getInstructionText(void) { return ""; }
static void hint(int s) { (void)s; }

static unsigned long long cycles;
static int dead;

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
		put(w[i]);
}

/* the bits of the main data words (24 a word), from bit at */
static unsigned bit(const unsigned long *w, long nw, long at)
{
	long wi = at / 24;
	return wi < nw ? (unsigned)(w[wi] >> (23 - at % 24)) & 1 : 0;
}

static long gcs, bad;
static unsigned long long huff_cycles;

void mp3dsp_huff_hook(const unsigned long *g, const unsigned long *w, long nw,
		      long start, long end, const int *is)
{
	long skip = start % 16, from = start - skip, n = (end - from + 15) / 16 + 3, i;
	unsigned long long t;
	int k, wrong = 0;

	put(2);
	put((unsigned long)skip);
	put((unsigned long)(end - start));
	put(g[1]);
	put(g[6]);
	put(g[7]);
	put(g[5] & 31);
	put((g[5] >> 5) & 31);
	put((g[5] >> 10) & 31);
	put((g[4] >> 5) & 1);
	put((unsigned long)n);
	for (i = 0; i < n; i++) {
		unsigned long c = 0;
		for (k = 0; k < 16; k++)
			c = c << 1 | bit(w, nw, from + i * 16 + k);
		put(c << 7);
	}
	if (get() != 1)
		dead = 1;
	if (getenv("SIMTRACE") && !gcs) {
		int z;
		for (z = 0; z < atoi(getenv("SIMTRACE")); z++) {
			fprintf(stderr, "P:%04x r4 %04x n4 %04x m4 %04x r0 %04x r1 %04x x0 %06x a %02x:%06x:%06x b %02x:%06x\n", dsp_core.pc,
				dsp_core.registers[DSP_REG_R4], dsp_core.registers[DSP_REG_N4], dsp_core.registers[DSP_REG_M4], dsp_core.registers[DSP_REG_R0], dsp_core.registers[DSP_REG_R1],
				dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_A2], dsp_core.registers[DSP_REG_A1],
				dsp_core.registers[DSP_REG_A0], dsp_core.registers[DSP_REG_B2], dsp_core.registers[DSP_REG_B1]);
			step();
		}
	}
	t = cycles;
	if (get() != 2)
		dead = 1;
	huff_cycles += cycles - t;
	{
		long got[576], left;
		for (k = 0; k < 576 && !dead; k++) {
			got[k] = get();
			if (got[k] != is[k] && !wrong++)
				fprintf(stderr, "granule %ld: line %d: the DSP %ld, the model %d\n", gcs, k, got[k], is[k]);
		}
		left = get();
		if (wrong && getenv("SIMDEBUG")) {
			fprintf(stderr, "skip %ld bits %ld bv %lu r1 %lu r2 %lu t %lu %lu %lu c1 %lu left %ld\n",
				skip, end - start, g[1], g[6], g[7], g[5] & 31, (g[5] >> 5) & 31,
				(g[5] >> 10) & 31, (g[4] >> 5) & 1, left);
			for (k = 0; k < 40; k++)
				fprintf(stderr, "%ld/%d ", got[k], is[k]);
			fprintf(stderr, "\n");
			exit(1);
		}
	}
	gcs++;
	bad += wrong != 0;
}

int main(int argc, char **argv)
{
	static unsigned long pk[PK_MAX], tab[64];
	static dw pcm[2][1152];
	static unsigned char buf[16 << 20];
	int a, i, fail = 0;

	dsp_core_init(hint);
	dsp56k_init_cpu();
	dsp_core_reset();
	for (i = 0; i < 512; i++)
		put(dsp_huff_prog[i]);
	load(1, 0x1000, t_dhx, T_DHX_N);
	load(4, 0x1000, t_dhy, T_DHY_N);
	load(1, 0x0e00, t_dpx, T_DPX_N);
	load(4, 0x0e00, t_dpy, T_DPY_N);
	load(1, 0x0f00, t_dselx, T_DSELX_N);
	load(4, 0x0f00, t_dsely, T_DSELY_N);
	for (i = 0; i <= 24; i++)	/* 2^(24 - c), c bits there */
		tab[i] = i ? 1UL << (24 - i) : 0;
	load(4, 0x40, tab, 25);
	for (i = 0; i <= 13; i++)
		tab[i] = (1UL << i) - 1;
	load(1, 0x60, tab, 14);
	for (i = 0; i <= 13; i++)
		tab[i] = i ? 1UL << (i - 1) : 0;
	load(4, 0x60, tab, 14);

	for (a = 1; a < argc && !dead; a++) {
		static MP3PARSE ps;
		static MP3DSP d;
		FILE *f = fopen(argv[a], "rb");
		long n, off = 0, samples = 0, rate = 0, bytes = 0, g0 = gcs, b0 = bad;
		unsigned long long c0 = huff_cycles;
		double secs;
		if (!f) {
			perror(argv[a]);
			return 1;
		}
		n = (long)fread(buf, 1, sizeof(buf), f);
		fclose(f);
		mp3_reset(&ps);
		mp3dsp_reset(&d);
		while (off + 4 <= n && !dead) {
			MP3HDR h;
			int ns;
			if (!mp3_header(buf + off, &h) || off + h.length > n) {
				off++;
				continue;
			}
			if (mp3_packet(&ps, &h, buf + off, pk) > 0) {
				mp3dsp_frame(&d, pk, pcm, &ns);
				samples += h.samples;
			}
			rate = h.rate;
			bytes += h.length;
			off += h.length;
		}
		secs = rate ? (double)samples / rate : 1;
		printf("%-28s %4.0f kbit/s %5ld granules, %ld wrong: %6.0f cycles a granule, %4.1f%% of the DSP\n",
		       argv[a], bytes * 8 / secs / 1000, gcs - g0, bad - b0,
		       (double)(huff_cycles - c0) / (gcs - g0 ? gcs - g0 : 1),
		       100.0 * (huff_cycles - c0) / secs / 32e6);
		fail |= bad != b0;
	}
	if (dead) {
		fprintf(stderr, "the DSP stopped answering at P:%04x, r0 %04x r1 %04x r3 %04x r4 %04x x0 %06x lc %04x\n",
			dsp_core.pc, dsp_core.registers[DSP_REG_R0], dsp_core.registers[DSP_REG_R1],
			dsp_core.registers[DSP_REG_R3], dsp_core.registers[DSP_REG_R4],
			dsp_core.registers[DSP_REG_X0], dsp_core.registers[DSP_REG_LC]);
		return 1;
	}
	return fail;
}
