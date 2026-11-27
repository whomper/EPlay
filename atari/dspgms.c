/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspgms.c - the internal synth on the Falcon's DSP (dspgms.h): the
 * program's 512 bootstrap words, then its stream's part (LOADP to
 * P:$7000), its output on the matrix, PLAY; STOP and its three counts.
 * The host port's words each waited for (its flags), a second at most.
 */
#include "tos.h"
#include "dsplock.h"
#include "snd.h"
#include "dspgms.h"
#include "gms_prog.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */

#define HZ200 (*(volatile unsigned long *)0x4baL)
#define HISR (*(volatile unsigned char *)0xffffa202L)
#define HTX32 (*(volatile unsigned long *)0xffffa204L)	/* 0, then the word's 3 bytes */
#define HRX32 (*(volatile unsigned long *)0xffffa204L)

#define Ongibit(b) trap14_ww(30, (b))
#define Offgibit(b) trap14_ww(29, (b))

static int locked, failed, playing;

static void put(unsigned long w)
{
	unsigned long t0;
	if (failed)
		return;
	t0 = HZ200;
	while (!(HISR & 2))
		if (HZ200 - t0 > 200) {
			failed = 1;
			return;
		}
	HTX32 = w & 0xffffff;
}

static long get(void)
{
	unsigned long t0, w;
	if (failed)
		return -1;
	t0 = HZ200;
	while (!(HISR & 1))
		if (HZ200 - t0 > 200) {
			failed = 1;
			return -1;
		}
	w = HRX32 & 0xffffff;
	return (long)w;
}

static long boot(void)
{
	int i;
	for (i = 0; i < 512; i++)
		put(dsp_gms_prog[i]);
	put(6);			/* LOADP: the stream's part */
	put(DSP_GMS_PROG_EXT_AT);
	put(DSP_GMS_PROG_EXT_N);
	for (i = 0; i < DSP_GMS_PROG_EXT_N; i++)
		put(dsp_gms_prog_ext[i]);
	return 0;
}

static long play(void)
{
	put(8);
	return 0;
}

static long blocks_, late_, skipped_;

static long stop(void)
{
	put(9);			/* (any word: STOP) */
	blocks_ = get();
	late_ = get();
	skipped_ = get();
	return 0;
}

int dspgms_open(const char **why)
{
	unsigned long t;
	failed = playing = 0;
	if (!(get_cookie(0x5F534E44L) & 8) || SND_XB) {	/* _SND bit 3: a DSP; the XBIOS's sound alone: no matrix */
		*why = "needs the Falcon's DSP";
		return 0;
	}
	if (dsp_lock() == -1) {
		*why = "the DSP is in use by another program";
		return 0;
	}
	locked = 1;
	/* reset: the sound chip's port A bit 4, 10 ms; then the bootstrap */
	Ongibit(0x10);
	t = tos_ms();
	while (tos_ms() - t < 10)
		;
	Offgibit(~0x10);
	t = tos_ms();
	while (tos_ms() - t < 10)
		;
	Supexec(boot);
	if (failed) {
		dspgms_close(0, 0, 0);
		*why = "the DSP didn't answer";
		return 0;
	}
	return 1;
}

void dspgms_out(long rate, int ext)
{
	snd_dsp_out(rate, ext);
}

void dspgms_play(void)
{
	Supexec(play);
	playing = !failed;
}

void dspgms_close(long *blocks, long *late, long *skipped)
{
	blocks_ = late_ = skipped_ = -1;
	if (playing && !failed)
		Supexec(stop);
	playing = 0;
	if (blocks)
		*blocks = blocks_;
	if (late)
		*late = late_;
	if (skipped)
		*skipped = skipped_;
	if (locked) {
		snd_dsp_off();
		dsp_unlock();
		locked = 0;
	}
}

void dspgms_term(void)
{
	if (locked) {
		dsp_unlock();
		locked = 0;
	}
}
