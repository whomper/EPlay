/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sim.c - dsp/mp3.a56, the whole DSP decoder, on Hatari's DSP core
 * against the model (src/mp3), bit for bit: every frame of the files
 * given goes to both as the 68030's packet, and every sample is compared.
 * The program and its tables are loaded as the 68030 will load them
 * (mp3tab.h's mp3dsp_load). Prints each file's DSP cycles a frame, and
 * what they come to on a 32 MHz DSP for the file's own rate.
 *
 *     sim FILE.MP3...       (SIMDEBUG=1: stop at the first difference)
 */
#include <string.h>
#include "../dspsim.h"
#include "mp3frame.h"
#include "mp3dsp.h"
#include "mp3tab.h"
#include "mp3_prog.h"

extern dw mp3dsp_dbg_xr[2][576], mp3dsp_dbg_sub[576], mp3dsp_dbg_rq[576];
extern dw mp3dsp_dbg_st[2][576], mp3dsp_dbg_ro[576], mp3dsp_dbg_al[576];
extern int mp3dsp_dbg_gr, mp3dsp_dbg_ch;
extern dw mp3dsp_dbg_sf[61];
static MP3DSP dsave;		/* the model's state before the frame being looked at */
static unsigned long sv_ov[1152], sv_ring[2048], sv_scf[44], sv_rp[2];	/* and the DSP's */

static void getbuf(int space, unsigned addr, unsigned long *w, int n)
{
	int i;
	put(8);
	put((unsigned long)space);
	put(addr);
	put((unsigned long)n);
	for (i = 0; i < n; i++)
		w[i] = (unsigned long)get() & 0xffffff;
}

static void save_state(void)
{
	getbuf(4, 0x2b10, sv_ov, 1152);
	getbuf(1, 0x2800, sv_ring, 2048);
	getbuf(1, 0x0840, sv_scf, 44);
	getbuf(1, 0x4d, sv_rp, 2);
}

static void restore_state(void)
{
	load(4, 0x2b10, sv_ov, 1152);
	load(1, 0x2800, sv_ring, 2048);
	load(1, 0x0840, sv_scf, 44);
	load(1, 0x4d, sv_rp, 2);
}

/* the frame to the DSP again (its state as before it), to stage st of
 * granule g, channel c */
static void stage(int st, int g, int c, const unsigned long *pk, int np, int nout)
{
	unsigned long w;
	int k;
	restore_state();
	w = (unsigned long)st;
	load(1, 0x93, &w, 1);
	w = (unsigned long)g;
	load(1, 0x95, &w, 1);
	w = (unsigned long)c;
	load(1, 0x96, &w, 1);
	put(7);
	put((unsigned long)np);
	for (k = 0; k < np; k++)
		put(pk[k]);
	sim_job();
	for (k = 0; k < nout; k++)
		get();
}
extern int mp3dsp_dbg_is[576];
extern long mp3dsp_dbg_at;

/* the DSP's buffer against the model's: the first difference */
static void cmpbuf(const char *what, int space, unsigned addr, const dw *m, int n)
{
	int i, first = -1, count = 0;
	long v, d0 = 0;
	put(8);
	put((unsigned long)space);
	put(addr);
	put((unsigned long)n);
	for (i = 0; i < n; i++) {
		v = get();
		if (v != m[i]) {
			if (first < 0) {
				first = i;
				d0 = v;
			}
			count++;
		}
	}
	if (getenv("SIMDUMP") && !strcmp(getenv("SIMDUMP"), what)) {
		put(8);
		put((unsigned long)space);
		put(addr);
		put(24);
		for (i = 0; i < 24; i++)
			fprintf(stderr, "%ld/%ld ", get(), (long)m[i]);
		fprintf(stderr, "\n");
	}
	if (first < 0)
		fprintf(stderr, "  %-10s the same\n", what);
	else
		fprintf(stderr, "  %-10s %d differ, the first at %d: the DSP %ld, the model %ld\n",
			what, count, first, d0, (long)m[first]);
}

#define XR0_ADDR 0x0600
#define XR1_ADDR 0x2480
#define OV0_ADDR 0x2b10
#define RSTAB_ADDR 0x0100
#define OV1_ADDR 0x2d50

/* the Falcon's clock for a file's rate: the lowest of 49170 / k at or above it */
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
	static unsigned long pk[PK_MAX];
	static dw pcm[2][1152];
	static unsigned char buf[16 << 20];
	int a, i, fail = 0;
	double worst = 0;
	/* SIMRS=1: each file resampled to the Falcon's clock above its rate,
	 * SIMRS=2: a 48 kHz file to 44.1 (the others left out), SIMRS=3: those
	 * above 32,780 Hz to it, Quality Normal's (the top subbands left out) */
	int rsmode = getenv("SIMRS") ? atoi(getenv("SIMRS")) : 0;
	/* SIMPCM=1: the model's samples (their top 16 bits) sent back as PCM
	 * packets (PK_PCM: WAV's way through the DSP), each granule one */
	int pcmmode = getenv("SIMPCM") ? atoi(getenv("SIMPCM")) : 0;
	const char *worst_name = "";

	sim_start(dsp_mp3_prog);
	put(6);				/* the program's outer part */
	put(DSP_MP3_PROG_EXT_AT);
	put(DSP_MP3_PROG_EXT_N);
	for (i = 0; i < DSP_MP3_PROG_EXT_N; i++)
		put(dsp_mp3_prog_ext[i]);
	for (i = 0; i < (int)(sizeof(mp3dsp_load) / sizeof(mp3dsp_load[0])); i++)
		load(mp3dsp_load[i].space, mp3dsp_load[i].addr, mp3dsp_load[i].w, mp3dsp_load[i].n);
	load(4, 0x0010, t_dpair, T_DPAIR_N);	/* internal Y */

	for (a = 1; a < argc && !dead; a++) {
		static MP3PARSE ps;
		static MP3DSP d;
		static MP3RS rs;
		static dw ro[2][MP3RS_MAX];
		int rs_on = 0;
		FILE *f = fopen(argv[a], "rb");
		long n, off = 0, frames = 0, samples = 0, rate = 0, bad = 0;
		unsigned long long c = 0, cmax = 0;
		double share;
		int nch = 1;
		if (!f) {
			perror(argv[a]);
			return 1;
		}
		n = (long)fread(buf, 1, sizeof(buf), f);
		fclose(f);
		mp3_reset(&ps);
		mp3dsp_reset(&d);
		put(5);
		while (off + 4 <= n && !dead) {
			MP3HDR h;
			int np, ns, ch, k, wrong = 0;
			unsigned long long t;
			if (!mp3_header(buf + off, &h) || off + h.length > n) {
				off++;
				continue;
			}
			np = mp3_packet(&ps, &h, buf + off, pk);
			rate = h.rate;
			if (rsmode == 2 && rate != 48000)
				break;
			if (rsmode == 3 && rate <= 32780)
				break;
			if (rsmode && !rs_on) {	/* (the taps over RSTAB: LOADY), RSSET */
				long rout = rsmode == 2 ? 44100 : rsmode == 3 ? 32780 : falcon_rate(rate);
				mp3rs_set(&rs, rate, rout);
				{	/* (the taps, up or down: RSTAB, as the 68030 loads them) */
					int k;
					put(4);
					put(RSTAB_ADDR);
					put(256);
					for (k = 0; k < 256; k++)
						put(rs.tab[k]);
				}
				d.sblim = rsmode == 3 ? MP3_SBLIM(rate) : 0;
				put(13);
				put(rs.si);
				put(rs.sf);
				put(rs.down ? 2 : 1);
				put(d.sblim ? d.sblim : 32);
				rs_on = 1;
			}
			off += h.length;
			if (np < 0)
				continue;
			if (getenv("SIMDEBUG")) {
				dsave = d;
				save_state();
			}
			nch = mp3dsp_frame(&d, pk, pcm, &ns);
			if (getenv("SIMPLAY")) {	/* the player's command: 16-bit, interleaved */
				unsigned long long t0 = cycles;
				put(11);
				put((unsigned long)np);
				for (k = 0; k < np; k++)
					put(pk[k]);
				for (k = 0; k < ns && !dead; k++)
					for (ch = 0; ch < 2; ch++) {
						long v = get() & 0xffff;
						long m = ((long)pcm[nch == 2 ? ch : 0][k] >> 8) & 0xffff;
						if (v != m && !wrong++)
							fprintf(stderr, "%s: frame %ld sample %d channel %d: PLAY %04lx, the model %04lx\n",
								argv[a], frames, k, ch, v, m);
					}
				c += cycles - t0;
				bad += wrong != 0;
				frames++;
				samples += ns;
				continue;
			}
			if (pcmmode) {		/* the frame's samples again, as PCM */
				static unsigned long pp[1 + 1152];
				static dw in[2][1152], ro2[2][MP3RS_MAX];
				int g, ngr = ns / 576, no;
				for (g = 0; g < ngr && !dead; g++) {
					int q = 1;
					for (k = 0; k < 576; k++)
						for (ch = 0; ch < nch; ch++) {
							long s16 = (long)pcm[ch][576 * g + k] >> 8;
							pp[q++] = (unsigned long)(s16 + 32768) << 7;
							in[ch][k] = (dw)(s16 * 256);
						}
					pp[0] = PK_PCM | PK_LSF | (nch == 2 ? PK_STEREO : 0);
					put(7);
					put((unsigned long)q);
					for (k = 0; k < q; k++)
						put(pp[k]);
					t = sim_job();
					c += t;
					no = 576;
					if (rs_on) {
						no = mp3rs_run(&rs, in, 576, nch, ro2);
						if ((int)get() != no) {
							fprintf(stderr, "%s: PCM: the resampled count differs\n", argv[a]);
							dead = 1;
						}
					}
					for (ch = 0; ch < nch && !dead; ch++)
						for (k = 0; k < no && !dead; k++) {
							long v = get(), m = rs_on ? (long)ro2[ch][k] : (long)in[ch][k];
							if (v != m && !wrong++)
								fprintf(stderr, "%s: frame %ld granule %d channel %d sample %d: "
									"PCM through the DSP %ld, wanted %ld\n",
									argv[a], frames, g, ch, k, v, m);
						}
				}
				bad += wrong != 0;
				frames++;
				samples += ns;
				continue;
			}
			put(7);
			put((unsigned long)np);
			for (k = 0; k < np; k++)
				put(pk[k]);
			t = sim_job();
			c += t;
			if (t > cmax)
				cmax = t;
			if (rs_on) {		/* the count, then each channel's outputs */
				int no, cnt;
				if (pk[0] & PK_MUTE)
					mp3rs_mute(&rs);
				no = mp3rs_run(&rs, pcm, ns, nch, ro);
				cnt = (int)get();
				if (cnt != no) {
					fprintf(stderr, "%s: frame %ld: the DSP resampled to %d, the model %d\n",
						argv[a], frames, cnt, no);
					wrong++;
					dead = 1;
				}
				for (ch = 0; ch < nch && !dead; ch++)
					for (k = 0; k < no && !dead; k++) {
						long v = get();
						if (v != ro[ch][k] && !wrong++)
							fprintf(stderr, "%s: frame %ld channel %d output %d: the DSP %ld, the model %ld\n",
								argv[a], frames, ch, k, v, (long)ro[ch][k]);
					}
				bad += wrong != 0;
				frames++;
				samples += ns;
				continue;
			}
			for (ch = 0; ch < nch; ch++)
				for (k = 0; k < ns && !dead; k++) {
					long v = get();
					if (v != pcm[ch][k] && !wrong++)
						fprintf(stderr, "%s: frame %ld channel %d sample %d: the DSP %ld, the model %ld\n",
							argv[a], frames, ch, k, v, (long)pcm[ch][k]);
				}
			bad += wrong != 0;
			frames++;
			samples += ns;
			if (wrong && getenv("SIMDEBUG")) {
				fprintf(stderr, "the frame's last granule, after the transform (in place):\n");
				cmpbuf("subbands", 1, nch > 1 ? XR1_ADDR : XR0_ADDR, mp3dsp_dbg_sub, 576);
				cmpbuf("overlap 0", 4, OV0_ADDR, d.ov[0], 576);
				if (nch > 1)
					cmpbuf("overlap 1", 4, OV1_ADDR, d.ov[1], 576);
				{	/* the frame again, to the stages of SIMGR, SIMCH (0, 0) */
					static dw isw[576];
					int g = getenv("SIMGR") ? atoi(getenv("SIMGR")) : 0;
					int cc = getenv("SIMCH") ? atoi(getenv("SIMCH")) : 0;
					fprintf(stderr, "granule %d, channel %d:\n", g, cc);
					/* the model again, keeping that granule's stages */
					mp3dsp_dbg_gr = g;
					mp3dsp_dbg_ch = cc;
					mp3dsp_frame(&dsave, pk, pcm, &ns);
					for (k = 0; k < 576; k++)
						isw[k] = mp3dsp_dbg_is[k];
					stage(1, g, cc, pk, np, nch * ns);
					cmpbuf("Huffman", 1, cc ? XR1_ADDR : XR0_ADDR, isw, 576);
					stage(2, g, cc, pk, np, nch * ns);
					cmpbuf("dequant", 1, cc ? XR1_ADDR : XR0_ADDR, mp3dsp_dbg_rq, 576);
					cmpbuf("scalefacs", 1, 0x0870, mp3dsp_dbg_sf, 61);
					fprintf(stderr, "  side info g3 %06lx g4 %06lx g8 %06lx\n", pk[2 + (g * nch + cc) * 9 + 3],
						pk[2 + (g * nch + cc) * 9 + 4], pk[2 + (g * nch + cc) * 9 + 8]);
					if (nch > 1) {
						stage(4, g, 0, pk, np, nch * ns);
						cmpbuf("stereo 0", 1, XR0_ADDR, mp3dsp_dbg_st[0], 576);
						cmpbuf("stereo 1", 1, XR1_ADDR, mp3dsp_dbg_st[1], 576);
					}
					stage(5, g, cc, pk, np, nch * ns);
					cmpbuf("reorder", 1, cc ? XR1_ADDR : XR0_ADDR, mp3dsp_dbg_ro, 576);
					stage(6, g, cc, pk, np, nch * ns);
					cmpbuf("alias", 1, cc ? XR1_ADDR : XR0_ADDR, mp3dsp_dbg_al, 576);
				}
				return 1;
			}
		}
		share = rate && frames ? 100.0 * c / ((double)samples / rate) / 32e6 : 0;
		printf("%-14s %d ch %2ld kHz %5ld frames, %ld wrong: %7.0f cycles a frame (most %llu), %5.1f%% of the DSP\n",
		       strrchr(argv[a], '/') ? strrchr(argv[a], '/') + 1 : argv[a], nch, rate / 1000, frames, bad,
		       frames ? (double)c / frames : 0, cmax, share);
		if (share > worst) {
			worst = share;
			worst_name = argv[a];
		}
		fail |= bad != 0;
	}
	if (argc > 2)
		printf("the most: %.1f%% (%s)\n", worst, strrchr(worst_name, '/') ? strrchr(worst_name, '/') + 1 : worst_name);
	return sim_done() | fail;
}
