/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * mp3dsp.h - the C model of the DSP's part of an MP3 decode: a packet from
 * mp3frame.c in, 1152 samples a channel out, with the DSP56001's
 * arithmetic (dspmath.h). dsp/mp3.a56 does the same, bit for bit; this is
 * its specification and its test.
 */
#ifndef MP3DSP_H
#define MP3DSP_H

#include "dspmath.h"

/* subband samples and dequantised lines are held as value / 2^MP3_XS */
#define MP3_XS 1

typedef struct {
	dw ov[2][576];		/* the transform's overlap, a granule's */
	dw v[2][16][64];	/* the synthesis filter's V, its last 16 slots */
	int vpos[2];
	unsigned char scf[2][22];	/* granule 0's long scalefactors */
	long sat;		/* values limited on the way (none, in a good file) */
	int sblim;		/* the subbands transformed (0: all 32); from here, as zeros */
} MP3DSP;

/* Settings' Quality Normal (Erez, 2 Nov 2026): the subbands up to about 15
 * kHz (22 of 32 at 44.1 kHz, 20 at 48), the rest left out before the
 * synthesis, for the Falcon's 32,780 Hz */
#define MP3_SBLIM(rate) ((int)(15200L * 64 / (rate)))

void mp3dsp_reset(MP3DSP *d);

/* a packet's samples, Q23, into pcm[channel][0..ns - 1] (1152, or 576 for
 * MPEG-2 and 2.5); gives the channels */
int mp3dsp_frame(MP3DSP *d, const unsigned long *pk, dw pcm[2][1152], int *ns);

/* ---- resampling, when the output's clock isn't the file's: the DSP's
 * stage between the synthesis filter and the SSI. A position runs through
 * the input a step (input samples an output) at a time; each output is 8
 * taps of a windowed sinc around it, of 32 phases (t_rsup, or t_rsdn when
 * it goes down). The input comes in blocks of 32 (the filter's slots), and
 * a block gives the outputs whose taps it holds */
#define MP3RS_MAX 1300		/* a frame's outputs at most (x 1.115) */

/* which taps a step down takes, by how far it goes down (rout / rin): to
 * 0.85 or more t_rsdn (48 to 44.1 kHz), to 0.71 t_rsdn441 (44.1 kHz to
 * 32,780 Hz), else t_rsdn48 (48 kHz to 32,780 Hz). The 68030 loads the
 * stream's taps, up or down, into the DSP's RSTAB before it starts */
#define MP3RS_DOWN(rin, rout) ((rout) * 100 >= (rin) * 85 ? 0 : (rout) * 100 >= (rin) * 71 ? 1 : 2)

typedef struct {
	const unsigned long *tab;	/* the taps: t_rsup, t_rsdn, t_rsdn441 or t_rsdn48 */
	unsigned long si, sf;	/* the step: si + sf / 2^24 input samples */
	unsigned long f;	/* the position: its fraction (24 bits: the DSP's A0) */
	int n;			/* and its sample, from the block's first */
	int down;		/* (goes down) */
	dw hist[2][7];		/* each channel's last 7 inputs */
} MP3RS;

/* from rate rin to rout; the step as the 68030 works it out for the DSP */
void mp3rs_set(MP3RS *r, long rin, long rout);

/* a mute frame's: its last inputs silence (then its samples, 0, through
 * mp3rs_run: its outputs 0, as many as ever) */
void mp3rs_mute(MP3RS *r);

/* ns samples of nch channels in (a multiple of 32), the outputs into out;
 * gives how many */
int mp3rs_run(MP3RS *r, dw pcm[2][1152], int ns, int nch, dw out[2][MP3RS_MAX]);

#ifdef MP3DSP_HUFF_HOOK
/* the tests' look at each granule's Huffman part: its side information,
 * the main data, where its codes start and end, and the values they gave */
void mp3dsp_huff_hook(const unsigned long *g, const unsigned long *w, long nw,
		      long start, long end, const int *is);
#endif

#ifdef MP3DSP_SYNTH_HOOK
/* the tests' look at each granule's synthesis filter: its subband
 * samples, and the 576 samples it gave */
void mp3dsp_synth_hook(int ch, dw sub[32][18], const dw *pcm);
#endif

#endif
