/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspopl.h - the YM3812 (OPL2) or the YMF262 (OPL3) of a VGM file on the
 * Falcon's DSP (phase 10, step 7): dsp/opl.a56 loaded (the bootstrap's 512
 * words, then its outer part and its tables), its SSI to the DAC (and the
 * FDI) at 24,585 Hz on the Falcon's clock, or at half the FDI's 44.1 or 48
 * kHz, each sample twice; with the OPL3's second bank (18 channels) at
 * 16,390 Hz, or a third of the FDI's, each sample three times; the chip's registers (src/opld.c) and what changed sent from the
 * tick, as dspopn.h's YM2612.
 */
#ifndef EPLAY_DSPOPL_H
#define EPLAY_DSPOPL_H

#include "oplcpu.h"

/* the DSP locked, reset, loaded and sounding (silent until written): clock
   the chip's (Hz), opl3 a YMF262, nch the channels made (9, or 18 with its
   second bank), want a rate lower than the most (0: the most; else the
   Falcon's clock at it). 0 with why: no DSP, another program has it, it
   didn't answer */
int dspopl_open(unsigned long clock, int opl3, int nch, long want, const char **why);
void dspopl_write(int port, int reg, int val);	/* (the tick's, or before it runs) a register; port 1 the OPL3's second bank */
/* (from the tick: supervisor mode) n of the file's samples run (0 at its
   end), then what changed sent; 0 at the end */
int dspopl_tick(int (*run)(void *v, long n), void *v, long n);
void dspopl_volume(int att, int mute);	/* att steps of 1.5 dB down (0 to 15): sent by the tick */
void dspopl_hush(void);		/* (the tick's) every key off */
void dspopl_close(void);	/* silent, off the matrix, the DSP unlocked */
int dspopl_ext(void);		/* 1: on the external clock (the FDI) */
long dspopl_rate(void);		/* the SSI's rate */
long dspopl_synth(void);	/* the rate the chip is made at (half the SSI's at the FDI's) */
void dspopl_term(void);		/* (supervisor: term.c) */
long dspopl_least(void);	/* the DSP's ring's least lead since PLAY (words, of 1,536), as the tick last heard; -1 not yet */
int dspopl_late2(void);
/* (phase 11) the tables the DSP is loaded with, for the CPU's model
   (oplcpu.c) */
const OPLC_TABS *dspopl_tabs(void);		/* an OPL3 made with nine channels keyed one of its second bank's */

#endif
