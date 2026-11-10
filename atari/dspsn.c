/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspsn.c - the SN76489 on the Falcon's DSP (dspsn.h). The chip: three
 * tones, each a period of 10 bits (its square wave turns over every period
 * counts of clock / 16), and a noise (its register shifted at clock / 512,
 * / 1024, / 2048, or half tone 2's rate), each with a volume of 4 bits, 2 dB
 * a step, 15 silent; its writes turned into sn.a56's commands by
 * src/sncmd.c.
 */
#include "tos.h"
#include "snd.h"
#include "dspsn.h"
#include "sncmd.h"
#include "sn_prog.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */

#define HZ200 (*(volatile unsigned long *)0x4baL)
#define HISR (*(volatile unsigned char *)0xffffa202L)
#define HTX32 (*(volatile unsigned long *)0xffffa204L)

#define Dsp_Lock() trap14_w(104)
#define Dsp_Unlock() trap14_w(105)
#define Ongibit(b) trap14_ww(30, (b))
#define Offgibit(b) trap14_ww(29, (b))

static int locked, failed, on;
static long rate;
static SNCMD sn;
static volatile int att, mute, amp_due;

static void put(void *u, long w)
{
	(void)u;
	if (failed)
		return;
	if (!(HISR & 2)) {
		unsigned long t0 = HZ200;
		while (!(HISR & 2))
			if (HZ200 - t0 > 200) {
				failed = 1;
				return;
			}
	}
	HTX32 = (unsigned long)w & 0xffffff;
}

void dspsn_write(int b)
{
	if (on)
		sncmd_write(&sn, b);
}

static long boot(void)
{
	int i;
	for (i = 0; i < 512; i++)
		put(0, (long)dsp_sn_prog[i]);
	return 0;
}

static unsigned long s_clock;
static int s_fb, s_width;

static long setup(void)
{
	sncmd_init(&sn, s_clock, rate, s_fb, s_width, 0, 0, put, 0);
	return 0;
}

int dspsn_open(unsigned long clock, int fb, int width, const char **why)
{
	int ext;
	unsigned long t;
	failed = 0;
	if (!(get_cookie(0x5F534E44L) & 8)) {	/* _SND bit 3: a DSP */
		*why = "needs the Falcon's DSP";
		return 0;
	}
	if (Dsp_Lock() == -1) {
		*why = "can't play: the DSP is in use by another program";
		return 0;
	}
	locked = 1;
	snd_take();
	/* reset: the sound chip's port A bit 4, 10 ms; then the bootstrap's
	   512 words */
	Ongibit(0x10);
	t = tos_ms();
	while (tos_ms() - t < 10)
		;
	Offgibit(~0x10);
	t = tos_ms();
	while (tos_ms() - t < 10)
		;
	Supexec(boot);
	/* on the matrix first: its clock starts the SSI, which sn.a56 waits
	   for before it reads the host port */
	rate = snd_rate_dsp(44100, &ext);
	snd_dsp_out(rate, ext);
	on = 1;
	s_clock = clock;
	s_fb = fb;
	s_width = width;
	Supexec(setup);
	if (failed) {
		dspsn_close();
		*why = "can't play: the DSP didn't answer";
		return 0;
	}
	return 1;
}

long dspsn_rate(void)
{
	return rate;
}

static long reamp(void)
{
	sn.att = att;
	sn.mute = mute;
	sncmd_reamp(&sn);
	return 0;
}

/* (the tick sends it: the host port's words from one place only) */
void dspsn_volume(int a, int m)
{
	att = a;
	mute = m;
	amp_due = 1;
}

void dspsn_poll(void)
{
	if (on && amp_due) {
		amp_due = 0;
		reamp();
	}
}

void dspsn_close(void)
{
	if (on) {
		snd_dsp_off();
		on = 0;
	}
	if (locked) {
		Dsp_Unlock();
		locked = 0;
	}
}

void dspsn_term(void)
{
	on = 0;
}
