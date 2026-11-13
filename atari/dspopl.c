/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspopl.c - the YM3812 on the Falcon's DSP (dspopl.h), as dspopn.c plays
 * the YM2612: the DSP's words from one place only, the tick, but for the
 * start (before the tick runs); a burst (a tick's changes) goes with the
 * host's flag 0 set, so that the DSP takes its commands one after another,
 * no sample made between.
 */
#include "tos.h"
#include "tickcode.h"
#include "snd.h"
#include "opld.h"
#include "dspopl.h"
#include "opl_prog.h"
#include "opltab.h"
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

static OPLD chip;
static int locked, failed, on, twice, ext_on;
static long rate, synth;
static volatile long gain_due = -1;	/* the volume's gain for the tick to send */
static int burst;
static long last_w[8];		/* (the last burst's first words, for the log) */
static int last_n;
static unsigned long sent, f_sent;
static int f_hisr;

TICK_CODE("70dspopl_put")
static void put(void *u, long w)
{
	(void)u;
	if (failed)
		return;
	if (!burst) {
		HICR |= 8;	/* (HF0: a burst coming) */
		burst = 1;
		last_n = 0;
	}
	if (last_n < 8)
		last_w[last_n++] = w;
	if (!(HISR & 2)) {
		unsigned long t0 = HZ200;
		while (!(HISR & 2))
			if (HZ200 - t0 > 200) {
				failed = 1;
				f_sent = sent;
				f_hisr = HISR;
				return;
			}
	}
	HTX32 = (unsigned long)w & 0xffffff;
	sent++;
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
	/* HF0 down through the load: the 56001's boot ROM ends the 512 words
	   early when the host sets HF0 (dspopn.c's, EPlay 1.24) */
	HICR &= ~8;
	burst = 1;		/* (so put doesn't raise it) */
	for (i = 0; i < 512; i++)
		put(0, (long)dsp_opl_prog[i]);
	load(6, DSP_OPL_PROG_EXT_AT, dsp_opl_prog_ext, DSP_OPL_PROG_EXT_N);
	load(1, OPL_X_GAIN, opl_gain, sizeof(opl_gain) / sizeof(opl_gain[0]));
	load(1, OPL_X_AM, opl_am, 512);
	load(1, OPL_X_PMW, opl_pmw, 8);
	load(4, OPL_Y_WAVE, opl_wave, 4096);
	put(0, OPLC_RESET);
	burst = 0;
	return 0;
}

static long setup(void)
{
	opld_send(&chip, put, 0);
	put(0, OPLC_PLAY);
	put(0, twice ? 2 : 1);
	return 0;
}

int dspopl_open(unsigned long clock, const char **why)
{
	int ext;
	unsigned long t;
	failed = 0;
	sent = 0;
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
	/* 24,585 Hz on the Falcon's clock; at the FDI's 44.1 or 48 kHz the
	   chip made at half of it */
	rate = snd_rate_dsp(24585, &ext);
	twice = rate > 30000;
	synth = twice ? rate / 2 : rate;
	opld_init(&chip, clock ? clock : 3579545UL, synth);
	gain_due = -1;
	/* on the matrix first: its clock starts the SSI, which PLAY waits for */
	snd_dsp_out(rate, ext);
	ext_on = ext;
	on = 1;
	Supexec(setup);
	if (failed) {
		dspopl_close();
		*why = "can't play: the DSP didn't answer";
		return 0;
	}
	return 1;
}

int dspopl_ext(void)
{
	return ext_on;
}

long dspopl_rate(void)
{
	return rate;
}

long dspopl_synth(void)
{
	return synth;
}

TICK_CODE("71dspopl_write")
void dspopl_write(int port, int reg, int val)
{
	if (on && !port)
		opld_write(&chip, reg, val);
}

TICK_CODE("72dspopl_tick")
int dspopl_tick(int (*run)(void *v, long n), void *v, long n)
{
	int live;
	if (!on)
		return run(v, n);
	if (gain_due >= 0) {
		opld_gain(&chip, gain_due);
		gain_due = -1;
	}
	/* HF0 goes up at the tick's first word (put), not before the file's
	   run: the DSP makes no sample while it is up */
	live = run(v, n);
	opld_send(&chip, put, 0);
	HICR &= ~8;
	burst = 0;
	return live;
}

void dspopl_hush(void)
{
	int c;
	if (!on)
		return;
	opld_write(&chip, 0xbd, 0);
	for (c = 0; c < OPL_CH; c++)
		opld_write(&chip, 0xb0 + c, chip.ch[c].block << 2 | chip.ch[c].fnum >> 8);
	opld_send(&chip, put, 0);
	HICR &= ~8;
	burst = 0;
}

void dspopl_volume(int att, int mute)
{
	gain_due = mute ? 0 : (long)((long long)OPL_OUTG * att_q15[att & 15] >> 15);
}

/* the ring's least lead since PLAY (STATUS: words; -1 no answer) */
static long least;

static long status(void)
{
	unsigned long t0;
	least = -1;
	put(0, OPLC_STATUS);
	t0 = HZ200;
	while (!(HISR & 1))
		if (HZ200 - t0 > 40)
			return 0;
	least = (long)(HRX32 & 0xffffff);
	return 0;
}

static long hf0_down(void)
{
	HICR &= ~8;
	burst = 0;
	return 0;
}

void dspopl_close(void)
{
	if (on && !failed) {
		Supexec(status);
		if (least >= 0)
			elog("VGM: the DSP's ring's least lead %ld words (%ld ms)%s", least,
			     least * 500 / (rate ? rate : 24585), least < 64 ? ": it fell behind" : "");
		else
			elog("VGM: the DSP didn't say how it kept up");
	}
	if (on && failed)
		elog("VGM: the DSP stopped taking the 68030's words after %lu; HISR $%02X; the last burst began "
		     "%lX %lX %lX %lX %lX %lX %lX %lX", f_sent, f_hisr,
		     last_w[0], last_w[1], last_w[2], last_w[3], last_w[4], last_w[5], last_w[6], last_w[7]);
	if (on) {
		/* HF0 down (supervisor mode: the host port), for the next
		   program's bootstrap */
		Supexec(hf0_down);
		snd_dsp_off();
		on = 0;
	}
	if (locked) {
		Dsp_Unlock();
		locked = 0;
	}
}

void dspopl_term(void)
{
	on = 0;
}
