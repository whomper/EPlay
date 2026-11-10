/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspopn.c - the YM2612 on the Falcon's DSP (dspopn.h). The DSP's words
 * from one place only, the tick, but for the start (before the tick runs).
 * A burst (a tick's changes) goes with the host's flag 0 set, so that the
 * DSP takes its commands one after another, no sample made between. The
 * Mega Drive's SN76489 plays in the same program: its writes turned into
 * sn.a56's commands by src/sncmd.c, numbered from OPNC_SN.
 */
#include "tos.h"
#include "snd.h"
#include "opnd.h"
#include "dspopn.h"
#include "sncmd.h"
#include "opn_prog.h"
#include "opntab.h"
#include "elog.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */

#define HZ200 (*(volatile unsigned long *)0x4baL)
#define HICR (*(volatile unsigned char *)0xffffa200L)
#define HISR (*(volatile unsigned char *)0xffffa202L)
#define HTX32 (*(volatile unsigned long *)0xffffa204L)
#define HRX32 (*(volatile unsigned long *)0xffffa204L)

#define Dsp_Lock() trap14_w(104)
#define Dsp_Unlock() trap14_w(105)
#define Ongibit(b) trap14_ww(30, (b))
#define Offgibit(b) trap14_ww(29, (b))

/* the volume's 1.5 dB steps, Q15 (dspsn.c's) */
static const long att_q15[16] = { 32768, 27571, 23198, 19519, 16423, 13818, 11627, 9783,
				  8231,  6925,  5827,  4903,  4125,  3471,  2920,  2457 };

static OPND chip;
static SNCMD psg;		/* the Mega Drive's SN76489, in opn.a56 */
static unsigned long psg_clock;
static int psg_fb, psg_w, psg_on;
static volatile int psg_att = -1, psg_mute;
static int locked, failed, on, twice;
static long rate, synth;
static volatile int gain_due = -1;	/* the volume's gain for the tick to send */

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

static void load(int cmd, unsigned addr, const unsigned long *w, long n)
{
	long i;
	put(0, cmd);
	put(0, (long)addr);
	put(0, n);
	for (i = 0; i < n; i++)
		put(0, (long)w[i]);
}

static long boot(void)
{
	int i;
	for (i = 0; i < 512; i++)
		put(0, (long)dsp_opn_prog[i]);
	/* its outer part, its tables, then all silent (RESET: the sine's
	   copies made) */
	load(6, DSP_OPN_PROG_EXT_AT, dsp_opn_prog_ext, DSP_OPN_PROG_EXT_N);
	load(1, OPN_X_GAIN, opn_gain, sizeof(opn_gain) / sizeof(opn_gain[0]));
	load(1, OPN_X_TRI, opn_tri, 128);
	load(1, OPN_X_PMW, opn_pmw, 128);
	load(4, OPN_Y_SIN, opn_sin, 1024);
	put(0, OPNC_RESET);
	return 0;
}

static long setup(void)
{
	opnd_send_drate(&chip, put, 0);
	opnd_send(&chip, put, 0);
	if (psg_clock) {
		/* (its amplitudes halved: 0.1 of full scale a channel, about 8 dB
		   under an FM channel at its loudest) */
		sncmd_init(&psg, psg_clock, synth, psg_fb, psg_w, OPNC_SN, 1, put, 0);
		psg_on = 1;
	}
	put(0, OPNC_PLAY);
	put(0, twice ? 2 : 1);
	return 0;
}

int dspopn_open(unsigned long clock, unsigned long sn_clock, int sn_fb, int sn_w, const char **why)
{
	int ext;
	unsigned long t;
	failed = 0;
	psg_on = 0;
	psg_clock = sn_clock;
	psg_fb = sn_fb;
	psg_w = sn_w;
	psg_att = -1;
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
	   512 words and the rest */
	Ongibit(0x10);
	t = tos_ms();
	while (tos_ms() - t < 10)
		;
	Offgibit(~0x10);
	t = tos_ms();
	while (tos_ms() - t < 10)
		;
	Supexec(boot);
	/* 24,585 Hz on the Falcon's clock; the FDI's 44.1 or 48 kHz too fast
	   for six channels on a stock DSP: the chip made at half of it */
	rate = snd_rate_dsp(24585, &ext);
	twice = rate > 30000;
	synth = twice ? rate / 2 : rate;
	opnd_init(&chip, clock ? clock : 7670453UL, synth);
	gain_due = -1;
	/* on the matrix first: its clock starts the SSI, which PLAY waits for */
	snd_dsp_out(rate, ext);
	on = 1;
	Supexec(setup);
	if (failed) {
		dspopn_close();
		*why = "can't play: the DSP didn't answer";
		return 0;
	}
	return 1;
}

long dspopn_rate(void)
{
	return rate;
}

long dspopn_synth(void)
{
	return synth;
}

void dspopn_write(int port, int reg, int val)
{
	if (on)
		opnd_write(&chip, port, reg, val);
}

void dspopn_sn(int b)
{
	if (on && psg_on)
		sncmd_write(&psg, b);
}

int dspopn_psg(void)
{
	return psg_on;
}

int dspopn_tick(int (*run)(void *v, long n), long (*at)(void *v), void *v, long n)
{
	int live;
	if (!on)
		return run(v, n);
	if (gain_due >= 0) {
		opnd_gain(&chip, gain_due);
		gain_due = -1;
	}
	HICR |= 8;		/* (HF0: a burst coming) */
	if (psg_on && psg_att >= 0) {
		psg.att = psg_att;
		psg.mute = psg_mute;
		psg_att = -1;
		sncmd_reamp(&psg);
	}
	live = opnd_tick(&chip, run, at, v, n, put, 0);
	HICR &= ~8;
	return live;
}

void dspopn_hush(void)
{
	int c;
	if (!on)
		return;
	for (c = 0; c < 6; c++)
		opnd_write(&chip, 0, 0x28, (c >= 3 ? 4 : 0) | c % 3);
	opnd_write(&chip, 0, 0x2b, 0);
	HICR |= 8;
	opnd_send(&chip, put, 0);
	if (psg_on)
		for (c = 0; c < 4; c++)
			sncmd_write(&psg, 0x9f | c << 5);
	HICR &= ~8;
}

void dspopn_volume(int att, int mute)
{
	gain_due = mute ? 0 : (int)((long long)OPN_OUTG * att_q15[att & 15] >> 15);
	psg_mute = mute;
	psg_att = att & 15;
}

/* the ring's least lead since PLAY (STATUS: words; -1 no answer) */
static long least;

static long status(void)
{
	unsigned long t0;
	least = -1;
	put(0, OPNC_STATUS);
	t0 = HZ200;
	while (!(HISR & 1))
		if (HZ200 - t0 > 40)
			return 0;
	least = (long)(HRX32 & 0xffffff);
	return 0;
}

void dspopn_close(void)
{
	if (on && !failed) {
		Supexec(status);
		if (least >= 0)
			elog("VGM: the DSP's ring's least lead %ld words (%ld ms)%s", least,
			     least * 500 / (rate ? rate : 24585), least < 64 ? ": it fell behind" : "");
		else
			elog("VGM: the DSP didn't say how it kept up");
	}
	if (on) {
		/* (HF0 is down: the tick puts it back after each burst, and it is
		   off by now; the host port isn't reachable here, in user mode) */
		snd_dsp_off();
		on = 0;
	}
	if (locked) {
		Dsp_Unlock();
		locked = 0;
	}
}

void dspopn_term(void)
{
	on = 0;
	psg_on = 0;
}
