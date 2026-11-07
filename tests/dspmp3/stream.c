/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * stream.c - dsp/mp3.a56's STREAM (12) on Hatari's DSP core, as EPlay
 * plays an MP3: the SSI clocked as the matrix clocks it (a time slot each
 * STREAM_SLOT cycles, two a frame), a packet sent whenever the DSP asks
 * (HF2), a word whenever the host port has room, and its status words
 * read. It counts the frames the DSP says it decoded, the granules late
 * and the packets broken, and fails when the DSP stops: the stream's own
 * parts (the host receive's interrupt, the ring, the waits), which sim.c's
 * FRAME (7) never runs. As the file's rate (an external clock), or with
 * SIMRS=1 resampled to the Falcon's clock above it (sim.c's falcon_rate).
 * SIMLOAD=P plays the output P % fast, as a DSP P % slower than Hatari's;
 * SIMLAT=N has the 68030 ready with each word N cycles after the last.
 *
 *     stream FILE.MP3 [FRAMES]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dsp_core.h"
#include "dsp_cpu.h"
#include "mp3frame.h"
#include "mp3dsp.h"
#include "mp3tab.h"
#include "mp3_prog.h"
#include "../dspsim.h"

/* 32 MHz cycles a time slot (two a frame): the output's rate's */
static unsigned long long slot_cycles = 325, next_slot;
static int slot0 = 1;

/* the matrix: a time slot when its time comes */
static void clock_ssi(void)
{
	while (cycles >= next_slot) {
		dsp_core_ssi_Receive_SC2(slot0);
		dsp_core_ssi_Receive_SCK();
		slot0 = !slot0;
		next_slot += slot_cycles;
	}
}

static int sstep(void)
{
	int r = step();
	clock_ssi();
	return r;
}

static long falcon_rate(long rin)
{
	static const long k10[] = { 10, 15, 20, 25, 30, 40, 50, 60 };	/* (k x 10) */
	long best = 49170;
	int i;
	for (i = 0; i < 8; i++)
		if (491700 / k10[i] >= rin)
			best = 491700 / k10[i];
	return best;
}

int main(int argc, char **argv)
{
	static unsigned char buf[16 << 20];
	static unsigned long pk[PK_MAX];
	static MP3PARSE ps;
	long n, off = 0, want = argc > 2 ? atol(argv[2]) : 200, sent = 0, frames = 0, late = 0, rsy = 0;
	long pkn = 0, pki = -1;		/* the packet going: its words, the next (-1: its count) */
	int have = 0, i, rsmode = getenv("SIMRS") ? atoi(getenv("SIMRS")) : 0, f4 = 0, l6 = 0, r4 = 0;
	unsigned long long quiet = 0, next_word = 0, lat = getenv("SIMLAT") ? strtoull(getenv("SIMLAT"), 0, 10) : 0;
	FILE *f;
	if (argc < 2 || !(f = fopen(argv[1], "rb"))) {
		fprintf(stderr, "usage: stream FILE.MP3 [FRAMES]\n");
		return 2;
	}
	n = (long)fread(buf, 1, sizeof(buf), f);
	fclose(f);
	sim_start(dsp_mp3_prog);
	put(6);
	put(DSP_MP3_PROG_EXT_AT);
	put(DSP_MP3_PROG_EXT_N);
	for (i = 0; i < DSP_MP3_PROG_EXT_N; i++)
		put(dsp_mp3_prog_ext[i]);
	for (i = 0; i < (int)(sizeof(mp3dsp_load) / sizeof(mp3dsp_load[0])); i++)
		load(mp3dsp_load[i].space, mp3dsp_load[i].addr, mp3dsp_load[i].w, mp3dsp_load[i].n);
	load(4, 0x0010, t_dpair, T_DPAIR_N);
	put(5);				/* RESET */
	mp3_reset(&ps);
	{
		/* the first frame's rate: the output's, or up to the Falcon's */
		MP3HDR h;
		long o = 0, rin = 44100, rout;
		while (o + 4 <= n && !mp3_header(buf + o, &h))
			o++;
		if (o + 4 <= n)
			rin = h.rate;
		rout = rsmode ? falcon_rate(rin) : rin;
		/* SIMLOAD=P: the output P % faster, as a DSP P % slower than
		   Hatari's would be (a real Falcon's: external memory, the bus) */
		slot_cycles = 32000000ULL * 100 / (2ULL * (unsigned long long)rout *
						     (unsigned long long)(getenv("SIMLOAD") ? atoi(getenv("SIMLOAD")) : 100));
		if (rsmode) {
			MP3RS rs;
			mp3rs_set(&rs, rin, rout);
			load(4, 0x0100, rs.tab, 256);		/* RSTAB */
			put(13);
			put((unsigned long)(rin / rout));
			put((unsigned long)(((unsigned long long)(rin % rout) << 24) / (unsigned long long)rout));
			put(rin > rout ? 2 : 1);
			put(32);
		}
	}
	put(12);			/* STREAM */
	while (!dead && frames < want) {
		unsigned isr;
		if (!sstep()) {
			dead = 1;
			break;
		}
		isr = dsp_core_read_host(CPU_HOST_ISR);
		/* a status word from the DSP: the counts that run on */
		if (isr & 1) {
			unsigned long w = (unsigned long)dsp_core_read_host(CPU_HOST_RXH) << 16;
			w |= (unsigned long)dsp_core_read_host(CPU_HOST_RXM) << 8;
			w |= dsp_core_read_host(CPU_HOST_RXL);
			if (!(w & 0x800000)) {
				int nf4 = (int)(w >> 19) & 15, nl6 = (int)(w >> 9) & 63, nr4 = (int)(w >> 15) & 15;
				frames += (nf4 - f4) & 15;
				late += (nl6 - l6) & 63;
				rsy += (nr4 - r4) & 15;
				f4 = nf4;
				l6 = nl6;
				r4 = nr4;
				quiet = 0;
			}
		}
		/* the next packet, when the DSP asks (HF2) */
		if (!have && (isr & 8)) {
			for (;;) {
				MP3HDR h;
				int np;
				if (off + 4 > n) {
					pkn = 0;	/* the end: count 0 */
					break;
				}
				if (!mp3_header(buf + off, &h) || off + h.length > n) {
					off++;
					continue;
				}
				np = mp3_packet(&ps, &h, buf + off, pk);
				off += h.length;
				if (np < 0)
					continue;
				pkn = np;
				break;
			}
			pki = -1;
			have = 1;
		}
		/* SIMLAT=N: the 68030 ready with its next word N cycles after
		   the last (its interrupt, its tick, GEM holding it) */
		if (have && (isr & 2) && cycles >= next_word) {
			next_word = cycles + lat;
			unsigned long w = pki < 0 ? 0x800000UL | (unsigned long)pkn : pk[pki] & 0xffffff;
			dsp_core_write_host(CPU_HOST_TRXH, (w >> 16) & 255);
			dsp_core_write_host(CPU_HOST_TRXM, (w >> 8) & 255);
			dsp_core_write_host(CPU_HOST_TRXL, w & 255);
			if (++pki >= pkn) {
				have = 0;
				sent++;
				if (!pkn)
					break;
			}
		}
		if (++quiet > 40000000ULL) {
			fprintf(stderr, "stream: no status word for 40 million steps: the DSP at P:%04x, "
				"%ld packets sent, ISR $%02x, a packet going %d (word %ld of %ld)\n",
				dsp_core.pc, sent, isr, have, pki, pkn);
			dead = 1;
		}
	}
	printf("%s: %ld frames decoded, %ld granules late, %ld packets broken, %ld packets sent; "
	       "the system stack %u deep at most (P:%04x)%s\n",
	       strrchr(argv[1], '/') ? strrchr(argv[1], '/') + 1 : argv[1], frames, late, rsy, sent,
	       sp_most, sp_most_at, dead ? "; the DSP stopped" : "");
	return dead || frames < want || rsy;
}
