/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspgms.h - the internal synth on the Falcon's DSP (dsp/gms.a56): its
 * program loaded and the stream started, the DSP's transmit to the DAC
 * and the port at the output's rate, the sound DMA's four tracks into its
 * receive (snd_open_gms: the 68030 writes the blocks, gmsdsp.h); stopped,
 * its counts for the log.
 */
#ifndef EPLAY_DSPGMS_H
#define EPLAY_DSPGMS_H

/* the DSP locked and its program loaded: 1, else 0 with why set */
int dspgms_open(const char **why);
/* its transmit to the DAC and the port at rate (the external clock's,
   ext): before the DMA's ring is opened (snd_open_gms) */
void dspgms_out(long rate, int ext);
/* the stream started (the DMA's ring opened and written first) */
void dspgms_play(void);
/* the stream stopped, the DSP given back; its counts: the blocks
   rendered, those late, the words passed over finding blocks (-1: not
   answered) */
void dspgms_close(long *blocks, long *late, long *skipped);
void dspgms_term(void);		/* (term.c: EPlay ending however it ends) */

#endif
