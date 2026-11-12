/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * mp3ovl.h - an MP3 decoder on the CPU, as an overlay beside EPLAY.PRG
 * (common's ovl.c loads it, as TLS.OVL): MP3FPU.OVL, minimp3 for a 68060
 * with its FPU, for a machine with no Falcon DSP free (DESIGN.md, "MP3 on
 * the CPU"). The MP3OVL table is at the start of its text. The program
 * keeps the decoder's state (state_size bytes, from its own memory) and
 * hands each frame in; the overlay keeps nothing of its own between calls
 * but its scratch and its stack (one decoder at a time).
 */
#ifndef MP3OVL_H
#define MP3OVL_H

#define MP3OVL_MAGIC 0x4D50334FL	/* 'MP3O' */
#define MP3OVL_MAJOR 1			/* a change the program must know of */

typedef struct {
	int frame_bytes;	/* the bytes the frame took (0: no frame found in them) */
	int channels;		/* 1 or 2 */
	int hz;			/* its rate */
	int bitrate_kbps;
} MP3OVL_INFO;

typedef struct {
	long magic;		/* MP3OVL_MAGIC */
	short major;		/* MP3OVL_MAJOR */
	short size;		/* sizeof(MP3OVL) as it was built */
	const char *version;	/* the overlay's own */
	const char *what;	/* "minimp3 on the 68060's FPU" */
	long state_size;	/* the decoder's state, the program's memory */
	void (*init)(void *state);
	/* a frame from p (n bytes: a frame or more) to pcm (big-endian 16-bit,
	   channels interleaved, 1152 x 2 at most): the samples per channel, 0
	   when it held none (the bit reservoir filling, or no frame) */
	int (*frame)(void *state, const unsigned char *p, int n, short *pcm, MP3OVL_INFO *info);
} MP3OVL;

#endif
