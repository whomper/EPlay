/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspopn.h - the YM2612 of a VGM file on the Falcon's DSP (phase 10):
 * dsp/opn.a56 loaded (the bootstrap's 512 words, then its outer part and
 * its tables), its SSI to the DAC (and the FDI) at 24,585 Hz on the
 * Falcon's clock, or at half the FDI's 44.1 or 48 kHz, each sample twice;
 * the chip's registers (src/opnd.c) and what changed sent from the tick.
 */
#ifndef EPLAY_DSPOPN_H
#define EPLAY_DSPOPN_H

#include "vgm.h"

/* the DSP locked, reset, loaded and sounding (silent until written): clock
   the chip's (Hz); sn_clock the Mega Drive's SN76489's (0: none), sn_fb and
   sn_w its noise's taps and register's bits (the VGM header's). 0 with
   why: no DSP, another program has it, it didn't answer */
int dspopn_open(unsigned long clock, unsigned long sn_clock, int sn_fb, int sn_w, const char **why);
void dspopn_sn(int byte);	/* (the tick's, or before it runs) a byte to the SN76489 */
int dspopn_psg(void);		/* the SN76489 plays with it */
/* the DAC over a tick, for vgm_run to put the drums' samples in (0: not on) */
VGMDAC *dspopn_dac(void);
void dspopn_write(int port, int reg, int val);	/* (the tick's, or before it runs) a register */
/* (from the tick: supervisor mode) n of the file's samples run (0 at its
   end; at: its place, for the DAC's samples), then what changed sent; 0
   at the end */
int dspopn_tick(int (*run)(void *v, long n), long (*at)(void *v), void *v, long n);
void dspopn_volume(int att, int mute);	/* att steps of 1.5 dB down (0 to 15): sent by the tick */
void dspopn_hush(void);		/* (the tick's) every key off */
void dspopn_close(void);	/* silent, off the matrix, the DSP unlocked */
int dspopn_ext(void);		/* 1: on the external clock (the FDI) */
long dspopn_rate(void);		/* the SSI's rate */
long dspopn_synth(void);	/* the rate the chip is made at (half the SSI's at the FDI's) */
void dspopn_term(void);		/* (supervisor: term.c) */

#endif
