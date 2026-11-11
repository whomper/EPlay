/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * vgm.h - VGM files (phase 9, Erez, 9 Nov 2026): a game machine's sound
 * chips' register writes, timed in samples of 44,100 Hz (the format's own
 * clock, whatever plays it). A VGZ is one gzipped (unpacked whole first).
 * Read here: the header, the GD3 tag (the track, the game, the composer),
 * and the commands, run for a number of samples at a time with the
 * writes to the chips EPlay plays handed to the caller:
 *
 *	- the AY-3-8910 / YM2149 (the ST's, Amstrad CPC, MSX, Spectrum),
 *	  registers 0 to 13 only (14 and 15 are the Atari's I/O ports); its
 *	  periods (tones, noise, envelope) turned from the file's clock to
 *	  the Atari's 2 MHz;
 *	- the SN76489 (Master System, Game Gear, BBC Micro), its bytes as
 *	  they are (for the DSP).
 *
 * A file with a loop plays it once more, the last 4 s fading out (the
 * AY's volumes turned down; one with the envelope on is left as it is),
 * as VGM players do. Every other chip's commands are passed over.
 */
#ifndef EPLAY_VGM_H
#define EPLAY_VGM_H

#define VGM_RATE 44100L
#define VGM_FADE (4 * VGM_RATE)	/* the fade's samples */
#define VGM_AY_CLOCK 2000000L	/* the Atari's YM2149 */

typedef struct {
	unsigned long version;	/* BCD: 0x151 is 1.51 */
	long data_at;		/* the first command */
	long loop_at;		/* the loop's first command, 0: none */
	long total;		/* samples, the whole file once */
	long loop_samples;	/* the loop's */
	long gd3_at;		/* 0: none */
	unsigned long sn_clock, ay_clock, ym2612_clock;	/* 0: not used (the second chip's bit left out) */
	int ay_type;		/* 0 AY8910 ... 0x10 YM2149 (the header's) */
	int sn_fb, sn_w;	/* the SN76489's noise taps and register width (0: the Master System's) */
	int other;		/* another chip is used (one EPlay doesn't play): its clock is set */
} VGMHEAD;

/* the header of the n bytes at b: 1 if it is a VGM file EPlay reads */
int vgm_head(const unsigned char *b, long n, VGMHEAD *h);
/* the same from the file's first n bytes, the file size bytes long (the
   list's: the header alone read) */
int vgm_head_of(const unsigned char *b, long n, long size, VGMHEAD *h);
/* the GD3 tag: the track's name, the game's, the composer's (English,
   else the other), in the Atari character set; "" where none */
void vgm_gd3(const unsigned char *b, long n, const VGMHEAD *h, char *title, char *album, char *artist, int len);
/* how long it plays, in samples: once, and the loop once more */
long vgm_length(const VGMHEAD *h);

typedef void (*VGM_AY)(void *u, int reg, int val);
typedef void (*VGM_SN)(void *u, int val);
/* the YM2612 (phase 10): a register of port 0 or 1 written; reg $2A of
   port 0 is its DAC's sample (the drums), from the file's data block */
typedef void (*VGM_OPN)(void *u, int port, int reg, int val);

/* the YM2612's DAC over a tick (opnd_tick's): its writes, each its place
   in the tick << 16 | the byte with its top bit turned << 8 (opn.a56's
   form), for the DSP to make the samples from (OPNC_DACE). A run given one (VGMRUN's dac) puts the data block's samples
   (0x8n) straight into it, with no call: Mega Drive drums are thousands a
   second, and the 68030 making their samples itself cost a stock Falcon
   more than its time (EPlay 1.18 to 1.20) */
#define VGMDAC_MAX 96
typedef struct {
	unsigned long ev[VGMDAC_MAX];
	int ne;			/* the writes so far */
	long base;		/* the file's sample at the tick's first */
	int val;		/* the DAC's byte now */
} VGMDAC;

/* a write at the tick's place e (a full tick's last place taking the last) */
static inline void vgmdac_put(VGMDAC *q, long e, int v)
{
	unsigned long w;
	/* (a byte already held left out: the DSP holds it; half of a Mega
	   Drive's drums' writes) */
	if (v == q->val)
		return;
	if (e > VGMDAC_MAX - 1)
		e = VGMDAC_MAX - 1;
	w = (unsigned long)e << 16 | (unsigned long)(v ^ 0x80) << 8;
	if (q->ne < VGMDAC_MAX)
		q->ev[q->ne++] = w;
	else
		q->ev[VGMDAC_MAX - 1] = w;
	q->val = v;
}

typedef struct {
	const unsigned char *b;
	long n, pos;
	VGMHEAD h;
	long wait;		/* samples before the next command */
	long at;		/* samples played */
	long end;		/* vgm_length's */
	int looped;		/* the loop taken once */
	int done;
	unsigned long ratio;	/* the AY's periods: 2 MHz / its clock, Q16 (65536: as they are) */
	unsigned char ay[16];	/* the file's AY registers (before turning) */
	int sn_period[3], sn_vol[4], sn_noise, sn_latch;	/* the SN76489's, as written (for a seek) */
	int fade;		/* the volumes' level now, 0 to 16 (16: none) */
	int att;		/* the AY's volumes this many steps (about 3 dB) down: 15 silent */
	VGM_AY ay_fn;
	VGM_SN sn_fn;
	VGM_OPN opn_fn;		/* (set after vgm_begin; 0: the YM2612 passed over) */
	VGMDAC *dac;		/* (set after vgm_begin, with opn_fn: the 0x8n samples into it; 0: through opn_fn) */
	/* a file still unpacking (a big VGZ played as it unpacks): the bytes
	   there so far (0: all), more() for a seek to unpack further (the
	   bytes there then, 0 all, -1 damaged); starved: the run waited at
	   avail (vgm_run then holds, the music not ended) */
	long avail;
	long (*more)(void *mu);
	void *mu;
	int starved;
	long waits;		/* (the runs that waited for it: the log's) */
	unsigned char opn[2][256];	/* the YM2612's registers as written (for a seek) */
	unsigned char opn_key[8];	/* each channel's key on/off byte ($28's), as written */
	long pcm_at, pcm_len;	/* the data block of the YM2612's samples (type 0): in b, its bytes */
	long pcm_pos;		/* the next of them (the 0x8n commands, 0xE0 sets it) */
	void *u;
} VGMRUN;

/* ready to play from the start (ay_fn, sn_fn may be 0) */
void vgm_begin(VGMRUN *r, const unsigned char *b, long n, const VGMHEAD *h, VGM_AY ay_fn, VGM_SN sn_fn, void *u);
/* samples played on: the commands whose time came run. 0 when it ended */
int vgm_run(VGMRUN *r, long samples);
/* to sample `to` without a sound (the commands run, the writes kept),
   then every AY register written as it is there, and the SN76489's
   registers as bytes that set them */
void vgm_seek(VGMRUN *r, long to);
/* the same from a copy of the run made at a place before (0: the start) */
void vgm_seek_from(VGMRUN *r, const VGMRUN *from, long to);
/* the AY's volumes att steps down (the TT's volume: its YM2149 has no
   mixer after it), written at once */
void vgm_set_att(VGMRUN *r, int att);
/* every AY register, and the SN76489's state, written again as they are */
void vgm_resend(VGMRUN *r);

#endif
