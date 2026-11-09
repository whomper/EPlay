/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * snd.h - the Falcon's sound: a ring in ST RAM that the sound DMA plays
 * round and round, four 16-bit stereo tracks (the music in the first, the
 * others silent), and the program writing ahead of it;
 * the matrix (the DMA to the DAC and the DSP port, where the FDI is), the
 * clock (the Falcon's own, or an external one on the DSP port), and both
 * put back as they were found when EPlay quits.
 */
#ifndef SND_H
#define SND_H

/* the sound: 0 when the machine has no Falcon sound matrix (_SND bits 2, 4)
   or another program holds the sound system (Locksnd), with why in why */
int snd_init(const char **why);
void snd_exit(void);
/* the sound system (Locksnd) while EPlay plays: 1, or 0 when another
   program has it; snd_give puts the matrix back and lets it go */
int snd_take(void);
void snd_term(void);	/* (supervisor mode: term.c) */
void snd_give(void);

/* the external clock on the DSP port as the DMA measures it: 0 if none
   (the FDI's 44100 or 48000 Hz) */
extern long snd_ext_rate;
extern int snd_dma8;		/* the TT's or STE's 8-bit DMA sound, not the Falcon's */
extern int snd_dma8_interp;	/* it interpolates and dithers (a 68030), else the nearest sample */
extern long snd_in_rate;	/* the file's rate, for the 8-bit DMA's resampling (before snd_open) */
/* the STE's way: a file's frames (bpf bytes; the left's and right's top bytes at ol, orr;
   x 0x80 for unsigned 8-bit) into the ring at the DMA's rate, the nearest; the frames taken */
long snd_put8_raw(const unsigned char *src, long nin, int bpf, int ol, int orr, unsigned char x);
extern int snd_ext_use, snd_ext_only;	/* Settings' Output: DSP (no external clock), FDI (it at any ratio) */
extern int snd_ta_busy;	/* the last measure timed by the 200 Hz clock (Timer A in use), not Timer A */
extern long snd_ext_raw;	/* the DMA's rate on it at prescale 1, as measured (0: no clock) */
extern long snd_ext_clock;	/* the clock's frequency from that, Hz */
extern int snd_fdi;		/* the FDI's driver is there */
extern int snd_gpio_dir, snd_gpio_data;	/* the DSP port's GPIO at the start (the FDI's) */
extern char snd_ext_note[];
extern int snd_fdi_nosig;	/* snd_fdi_set found no signal at the FDI's input */
extern char snd_fdi_note[];	/* snd_fdi_set's tries: each value and its measure */

int snd_fdi_set(long rate);	/* the FDI to 44.1 or 48 kHz: 1 when its clock is at it */
void snd_dsp_out(long rate, int ext);	/* the DSP's SSI to the DAC and the FDI, at rate */
void snd_dsp_off(void);
void snd_dac_volume(int att, int mute);	/* the Falcon's own output: att * 1.5 dB down; mute: silent */
void snd_mfp_note(char *b, int n);	/* the MFP's interrupts and inputs now, for the log */
extern unsigned short snd_matrix[4];	/* $FF8930-7 after snd_dsp_out */		/* off the matrix again */	/* what was tried after the first measure, for the log */

/* the rate to play a file of rate Hz at: exactly, through the external
   clock or the Falcon's own (49170, 32780, 24585... Hz), else the
   nearest of them; *ext 1 for the external clock */
long snd_rate_for(long rate, int *ext);
long snd_rate_dsp(long rate, int *ext);	/* an MP3 from the DSP: the clock it resamples to */

/* the ring ready at that rate, silent, the DMA stopped: 0 if no ST RAM */
int snd_open(long rate, int ext);
int snd_open_feed(long rate, int ext);	/* the ring to the DSP's receive (one track): XSTREAM */
void snd_close(void);		/* stopped, the ring given back */

long snd_room(void);		/* frames that can be written now */
void snd_write(const short *frames, long n);	/* n stereo frames, at most snd_room() */
short *snd_ptr(long *n);	/* where the next frames go: n frames there before the ring's end */
extern int snd_fw;		/* the ring's words a frame: 8 (four tracks to the DAC, the music in
				   the first two), or 2 (snd_open_feed: to the DSP) */
#define SND_STEP snd_fw
void snd_wrote(long n);		/* n frames put at snd_ptr() */
void snd_play(void);		/* the DMA started (after the first frames) */
long snd_played(void);		/* frames played since snd_open */
void snd_update(void);		/* snd_played brought up to date: from the tick only */
extern long snd_underruns;	/* times the DMA came past what was written */
extern long snd_first_under;	/* frames played at the first (-1: none) */
extern long snd_least_lead;	/* the fewest frames written ahead after the first second (-1: none yet) */
long snd_written(void);
long snd_ring_frames(void);
int snd_playing(void);

#endif
