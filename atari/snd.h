/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * snd.h - the Falcon's sound: a ring in ST RAM that the sound DMA plays
 * round and round, 16-bit stereo, and the program writing ahead of it;
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

/* the external clock on the DSP port as the DMA measures it: 0 if none
   (the FDI's 44100 or 48000 Hz) */
extern long snd_ext_rate;
extern long snd_ext_raw;	/* the DMA's rate on it at prescale 1, as measured (0: no clock) */
extern long snd_ext_clock;	/* the clock's frequency from that, Hz */
extern int snd_fdi;		/* the FDI's driver is there */

/* the rate to play a file of rate Hz at: exactly, through the external
   clock or the Falcon's own (49170, 32780, 24585... Hz), else the
   nearest of them; *ext 1 for the external clock */
long snd_rate_for(long rate, int *ext);

/* the ring ready at that rate, silent, the DMA stopped: 0 if no ST RAM */
int snd_open(long rate, int ext);
void snd_close(void);		/* stopped, the ring given back */

long snd_room(void);		/* frames that can be written now */
void snd_write(const short *frames, long n);	/* n stereo frames, at most snd_room() */
short *snd_ptr(long *n);	/* where the next frames go: n frames there before the ring's end */
void snd_wrote(long n);		/* n frames put at snd_ptr() */
void snd_play(void);		/* the DMA started (after the first frames) */
long snd_played(void);		/* frames played since snd_open */
void snd_update(void);		/* snd_played brought up to date: from the tick only */
extern long snd_underruns;	/* times the DMA came past what was written */
long snd_written(void);
long snd_ring_frames(void);
int snd_playing(void);

#endif
