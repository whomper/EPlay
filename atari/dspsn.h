/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspsn.h - the SN76489 of a VGM file on the Falcon's DSP (phase 9):
 * dsp/sn.a56 loaded by the DSP's bootstrap, its SSI to the DAC (and the
 * FDI) on the Falcon's clock or the FDI's; the chip's writes from the tick
 * turned into its steps and amplitudes (sn.a56's commands).
 */
#ifndef EPLAY_DSPSN_H
#define EPLAY_DSPSN_H

/* the DSP locked, reset, loaded and sounding (silent until written):
   clock the chip's (Hz), fb its noise's taps and width its register's
   bits (the VGM header's: 0 for the Master System's, $0009 and 16).
   0 with why: no DSP, another program has it, it didn't answer */
int dspsn_open(unsigned long clock, int fb, int width, const char **why);
void dspsn_write(int byte);	/* (from the tick: supervisor mode) a byte to the chip */
void dspsn_volume(int att, int mute);	/* att steps of 1.5 dB down (0 to 15): sent by dspsn_poll */
void dspsn_poll(void);		/* (from the tick) a volume asked for, sent */
void dspsn_close(void);		/* silent, off the matrix, the DSP unlocked */
long dspsn_rate(void);		/* the SSI's rate it plays at */
void dspsn_term(void);		/* (supervisor: term.c) */

#endif
