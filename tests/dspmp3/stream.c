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
 * And what the SSI sends, word by word, against the model's decoding of
 * the same packets (mp3dsp.c: copied at the file's rate, or resampled),
 * bit for bit. SIMGAIN=N moves every packet's global_gain by N steps
 * (ReplayGain: louder or quieter). SIMDROP=F:AT:N puts a drop (gapless, PK_DROP) on frame F's
 * packet: after AT of its samples, N left out, the model the same.
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
#define CAP_MAX (1L << 22)
static unsigned short cap[CAP_MAX];	/* what the SSI sent, a word each slot */
static long ncap;

/* the matrix: a time slot when its time comes */
static void clock_ssi(void)
{
	while (cycles >= next_slot) {
		dsp_core_ssi_Receive_SC2(slot0);
		dsp_core_ssi_Receive_SCK();
		if (ncap < CAP_MAX)
			cap[ncap++] = (unsigned short)dsp_core.ssi.transmit_value;
		slot0 = !slot0;
		next_slot += slot_cycles;
	}
}

static unsigned dropchk_pc;	/* SIMLEAD=1: the lead each time the drop looks */
static int sstep(void)
{
	int r;
	if (dropchk_pc && dsp_core.pc == dropchk_pc) {
		long out = (long)dsp_core.ramint[DSP_SPACE_X][0x9c], r7 = (long)dsp_core.registers[DSP_REG_R7];
		long fr = r7 - out;
		if (fr < 0)
			fr += 3352;
		if (3352 - fr < 512)
			fprintf(stderr, "lead %ld words: the drop's %ld samples left played\n", 3352 - fr,
				(long)(dsp_core.ramext[0x999] & 0xffffff));
	}
	r = step();
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
	static MP3DSP md;		/* the model, the same packets */
	static MP3RS mrs;
	static dw pcm[2][1152], mo[2][MP3RS_MAX];
	static unsigned short *exp_w;
	long nexp = 0, nfr = 0, drop_f = -1, drop_at = 0, drop_n = 0;
	int sim_gain = getenv("SIMGAIN") ? atoi(getenv("SIMGAIN")) : 0;
	if (getenv("SIMDROP"))
		sscanf(getenv("SIMDROP"), "%ld:%ld:%ld", &drop_f, &drop_at, &drop_n);
	exp_w = malloc(CAP_MAX * sizeof(*exp_w));
	if (getenv("SIMLEAD"))
		dropchk_pc = (unsigned)strtoul(getenv("SIMLEAD"), 0, 16);
	mp3dsp_reset(&md);
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
		mp3rs_set(&mrs, rin, rout);
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
				if (sim_gain)
					mp3_packet_gain(pk, sim_gain);	/* (ReplayGain louder or quieter) */
				if (nfr == drop_f) {
					pk[0] |= PK_DROP | (unsigned long)drop_n << 12;
					pk[1] |= (unsigned long)drop_at << 11;
				}
				{
					/* the model: this packet's output, as the DSP's should be */
					int ns, nch = mp3dsp_frame(&md, pk, pcm, &ns), k, j;
					mp3_drop_set(&mrs, pk);
					k = rsmode ? mp3rs_run(&mrs, pcm, ns, nch, mo) : mp3_copy_run(&mrs, pcm, ns, nch, mo);
					for (j = 0; j < k && nexp + 2 <= CAP_MAX; j++) {
						exp_w[nexp++] = (unsigned short)((mo[0][j] >> 8) & 0xffff);
						exp_w[nexp++] = (unsigned short)((mo[nch == 2 ? 1 : 0][j] >> 8) & 0xffff);
					}
				}
				nfr++;
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
	if (sim_gain > 0) {
		/* louder: what reaches full scale held there, never wrapped (a
		   jump of more than 3/4 of the range between two frames of a
		   channel, from near one end to near the other) */
		long k, full = 0, wraps = 0;
		for (k = 0; k < nexp; k++) {
			short v = (short)exp_w[k];
			full += v == 32767 || v == -32768;
			if (k >= 2) {
				short w = (short)exp_w[k - 2];
				if (labs((long)v - w) > 49152 && labs(v) > 24576 && labs(w) > 24576)
					wraps++;
			}
		}
		printf("SIMGAIN=%d: %ld words at full scale, %ld wrapped\n", sim_gain, full, wraps);
		if (wraps)
			return 1;
	}
	{
		/* the sound: the model's first sound found in what the SSI sent,
		   then every word after it the same, to the end of either */
		long e0 = 0, at = -1, k, bad = 0, first_bad = -1, cmpn;
		while (e0 < nexp && !exp_w[e0])
			e0++;
		e0 &= ~1L;
		for (k = 0; k + 64 <= ncap && at < 0; k++)
			if (!memcmp(cap + k, exp_w + e0, 64 * sizeof(*cap)))
				at = k - e0;
		if (at < 0) {
			printf("%s: the model's sound not found in what the SSI sent\n", argv[1]);
			dead = 1;
		} else {
			cmpn = nexp < ncap - at ? nexp : ncap - at;
			cmpn -= 2304;		/* (the end: the last granule's, played or not yet) */
			for (k = e0; k < cmpn; k++)
				if (cap[at + k] != exp_w[k]) {
					if (first_bad < 0)
						first_bad = k;
					bad++;
				}
			printf("%s: %ld words of sound, as the model's%s", argv[1] && strrchr(argv[1], '/') ?
			       strrchr(argv[1], '/') + 1 : argv[1], cmpn - e0, bad ? "" : "\n");
			if (bad) {
				printf(": %ld differ, the first at word %ld (%04x, the model %04x)\n", bad, first_bad,
				       cap[at + first_bad], exp_w[first_bad]);
				dead = 1;
			}
		}
	}
	printf("%s: %ld frames decoded, %ld granules late, %ld packets broken, %ld packets sent; "
	       "the system stack %u deep at most (P:%04x)%s\n",
	       strrchr(argv[1], '/') ? strrchr(argv[1], '/') + 1 : argv[1], frames, late, rsy, sent,
	       sp_most, sp_most_at, dead ? "; the DSP stopped" : "");
	return dead || frames < want || rsy;
}
