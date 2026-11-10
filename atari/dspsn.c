/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspsn.c - the SN76489 on the Falcon's DSP (dspsn.h). The chip: three
 * tones, each a period of 10 bits (its square wave turns over every period
 * counts of clock / 16), and a noise (its register shifted at clock / 512,
 * / 1024, / 2048, or half tone 2's rate), each with a volume of 4 bits, 2 dB
 * a step, 15 silent. A byte with bit 7 set chooses a register (channel,
 * tone or volume) and gives its low 4 bits; one without gives a tone's
 * high 6 bits, or the volume again.
 */
#include "tos.h"
#include "snd.h"
#include "dspsn.h"
#include "sn_prog.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */

#define HZ200 (*(volatile unsigned long *)0x4baL)
#define HISR (*(volatile unsigned char *)0xffffa202L)
#define HTX32 (*(volatile unsigned long *)0xffffa204L)

#define Dsp_Lock() trap14_w(104)
#define Dsp_Unlock() trap14_w(105)
#define Ongibit(b) trap14_ww(30, (b))
#define Offgibit(b) trap14_ww(29, (b))

/* the amplitudes, 2 dB a step (0.2 of full scale at 0: four channels
   at most 0.8), and the volume's 1.5 dB steps, Q15 */
static const long amp_of[16] = { 1677722, 1332662, 1058571, 840853, 667913, 530542, 421425, 334749,
				 265901,  211213,  167772,  133266, 105857, 84085,  66791,  0 };
static const long att_q15[16] = { 32768, 27571, 23198, 19519, 16423, 13818, 11627, 9783,
				  8231,  6925,  5827,  4903,  4125,  3471,  2920,  2457 };

static int locked, failed, on;
static long rate, q11;			/* the SSI's rate; clock / 16 / rate, Q11 */
static int period[3], vol[4] = { 15, 15, 15, 15 }, latch, nctl;
static volatile int att, mute, amp_due;

static void put(unsigned long w)
{
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
	HTX32 = w & 0xffffff;
}

static void cmd(int c, unsigned long v)
{
	put((unsigned long)c);
	put(v);
}

/* the step of a square wave turning over every n counts (Q23 of a
   sample), 0 above a quarter of the rate (a level: held up) */
static long step_of(long n)
{
	long s;
	if (n <= 0)
		return 0;
	s = (q11 << 12) / n;
	return s >= (1L << 22) ? 0 : s;
}

static long amp(int ch)
{
	if (mute)
		return 0;
	return (amp_of[vol[ch] & 15] >> 8) * att_q15[att & 15] >> 7;	/* (in 32 bits) */
}

static void tone(int ch)
{
	long s = step_of(period[ch]);
	cmd(ch, (unsigned long)s);
	if (!s)
		cmd(11, (unsigned long)ch);	/* (held up: the volume plays it) */
}

static void noise(void)
{
	long s;
	if ((nctl & 3) == 3)
		s = step_of(2L * period[2]);
	else
		s = step_of(32L << (nctl & 3));
	cmd(3, (unsigned long)s);
}

void dspsn_write(int b)
{
	if (!on)
		return;
	if (b & 0x80) {
		int ch = (b >> 5) & 3;
		latch = b & 0x70;
		if (b & 0x10) {
			vol[ch] = b & 15;
			cmd(4 + ch, (unsigned long)amp(ch));
		} else if (ch < 3) {
			period[ch] = (period[ch] & 0x3f0) | (b & 15);
			tone(ch);
			if (ch == 2 && (nctl & 3) == 3)
				noise();
		} else {
			nctl = b & 7;
			cmd(8, (unsigned long)((nctl >> 2) & 1));
			noise();
		}
	} else {
		int ch = (latch >> 5) & 3;
		if (latch & 0x10) {
			vol[ch] = b & 15;
			cmd(4 + ch, (unsigned long)amp(ch));
		} else if (ch < 3) {
			period[ch] = (period[ch] & 15) | ((b & 0x3f) << 4);
			tone(ch);
			if (ch == 2 && (nctl & 3) == 3)
				noise();
		} else {
			nctl = b & 7;
			cmd(8, (unsigned long)((nctl >> 2) & 1));
			noise();
		}
	}
}

static long boot(void)
{
	int i;
	for (i = 0; i < 512; i++)
		put(dsp_sn_prog[i]);
	return 0;
}

static unsigned long s_fb, s_top;

static long setup(void)
{
	int ch;
	cmd(9, s_fb);
	cmd(10, s_top);
	cmd(8, 1);
	for (ch = 0; ch < 4; ch++)
		cmd(4 + ch, 0);
	return 0;
}

int dspsn_open(unsigned long clock, int fb, int width, const char **why)
{
	int ext, ch;
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
	q11 = (long)(((clock ? clock : 3579545UL) / 16) << 11) / rate;
	s_fb = fb ? (unsigned long)fb : 9;
	s_top = 1UL << ((width ? width : 16) - 1);
	Supexec(setup);
	if (failed) {
		dspsn_close();
		*why = "can't play: the DSP didn't answer";
		return 0;
	}
	for (ch = 0; ch < 3; ch++)
		period[ch] = 0;
	for (ch = 0; ch < 4; ch++)
		vol[ch] = 15;
	latch = nctl = 0;
	return 1;
}

long dspsn_rate(void)
{
	return rate;
}

static long reamp(void)
{
	int ch;
	for (ch = 0; ch < 4; ch++)
		cmd(4 + ch, (unsigned long)amp(ch));
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
