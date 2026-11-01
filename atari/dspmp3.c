/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dspmp3.c - the MP3 decoder on the Falcon's DSP (dspmp3.h), through the
 * host port as common's dspec.c drives it: Dsp_Lock, the DSP held in reset
 * and let go (its bootstrap then takes 512 words), then mp3.a56's own
 * commands: LOADP for its outer part, LOADX and LOADY for the tables
 * (tools/mkmp3tab.py's mp3dsp_load), RESET, then PLAY a frame at a time.
 *
 * Every word waits for the host port's flag (TXDE to send, RXDF to read):
 * never a transfer timed by the 68030's speed, which other players do and
 * an accelerated Falcon outruns. The host port is reached in supervisor
 * mode: through Supexec from GEM's side, and from the tick (an interrupt,
 * supervisor already).
 */
#include "tos.h"
#include "dspmp3.h"
#include "mp3tab.h"
#include "mp3_prog.h"
#include "mp3frame.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */
#pragma GCC diagnostic ignored "-Wunused-const-variable"	/* mp3tab.h's model tables */

#define HZ200  (*(volatile unsigned long *)0x4baL)	/* (supervisor mode only) */
#define HISR   (*(volatile unsigned char *)0xffffa202L)
#define HTX32  (*(volatile unsigned long *)0xffffa204L)	/* 0, then the word's 3 bytes */
#define HRX16  (*(volatile short *)0xffffa206L)		/* the word's low 16 bits */

#define Dsp_Lock()	trap14_w(104)
#define Dsp_Unlock()	trap14_w(105)
#define Ongibit(b)	trap14_ww(30, (b))
#define Offgibit(b)	trap14_ww(29, (b))

static int locked, failed;
static int f_what, f_hisr;	/* how it failed: 1 sending, 2 reading, 3 loading; the host status */
static long f_at, f_of;		/* at which word of how many */
static long put_i, put_n;	/* the word going, of how many (the tick's packets) */
static int rst;			/* dspmp3_reset_step's: 3 done */
static unsigned long rst_t;

/* a word to the DSP; a second at most for its room */
static void put(unsigned long w)
{
	if (failed)
		return;
	if (!(HISR & 2)) {
		unsigned long t0 = HZ200;
		while (!(HISR & 2))
			if (HZ200 - t0 > 200) {
				failed = 1;
				f_what = put_n ? 1 : 3;
				f_at = put_i;
				f_of = put_n;
				f_hisr = HISR;
				return;
			}
	}
	HTX32 = w & 0xffffff;
}

/* the load, a piece at a time (dspmp3_tick_load): the items are the
   bootstrap's 512 words, the decoder's outer part (LOADP), the tables
   (mp3tab.h's mp3dsp_load) and t_dpair into internal Y, then RESET */
#define NTAB ((int)(sizeof(mp3dsp_load) / sizeof(mp3dsp_load[0])))
static int item;		/* the one going: 0 the bootstrap, 1 the outer part, 2.. the tables */
static long done_w;		/* its words sent */
static long chunk;		/* the words this piece may send */

static void item_of(int k, int *cmd, unsigned *addr, const unsigned long **w, long *n)
{
	if (k == 0) {
		*cmd = 0, *addr = 0, *w = dsp_mp3_prog, *n = 512;
	} else if (k == 1) {
		*cmd = 6, *addr = DSP_MP3_PROG_EXT_AT, *w = dsp_mp3_prog_ext, *n = DSP_MP3_PROG_EXT_N;
	} else if (k - 2 < NTAB) {
		*cmd = mp3dsp_load[k - 2].space, *addr = mp3dsp_load[k - 2].addr;
		*w = mp3dsp_load[k - 2].w, *n = mp3dsp_load[k - 2].n;
	} else {
		*cmd = 4, *addr = 0x0010, *w = t_dpair, *n = T_DPAIR_N;	/* internal Y */
	}
}

static long load_some(void)
{
	long left = chunk;
	while (left > 0 && item <= NTAB + 2 && !failed) {
		int cmd;
		unsigned addr;
		const unsigned long *w;
		long n;
		item_of(item, &cmd, &addr, &w, &n);
		if (done_w == 0 && cmd) {	/* (the bootstrap takes its words bare) */
			put((unsigned long)cmd);
			put(addr);
			put((unsigned long)n);
		}
		while (done_w < n && left > 0) {
			put(w[done_w++]);
			left--;
		}
		if (done_w == n) {
			item++;
			done_w = 0;
		}
	}
	if (item == NTAB + 3 && !failed) {
		put(5);			/* RESET */
		item++;
	}
	return 0;
}

/* a piece of the load from the tick (supervisor already): 32 words a
   tick, so the load takes two thirds of a second and leaves GEM most of
   a stock Falcon's 68030 meanwhile (Play's stretch: 256 a tick 190 ms,
   64 105 ms, 32 75 ms) */
int dspmp3_tick_load(void)
{
	if (item <= NTAB + 3 && !failed) {
		chunk = 32;
		load_some();
	}
	return failed ? -1 : item > NTAB + 3;
}

int dspmp3_open(void)
{
	if (!(get_cookie(0x5F534E44L) & 8))	/* _SND bit 3: a DSP */
		return DSPMP3_NONE;
	if (Dsp_Lock() == -1)
		return DSPMP3_BUSY;
	locked = 1;
	failed = 0;
	f_what = 0;
	put_n = 0;
	item = 0;
	done_w = 0;
	rst = 0;
	return DSPMP3_OK;
}

/* the DSP held in reset (the sound chip's port A, bit 4) and let go, a
   step at a time from GEM's steps: each wait a tick of the 200 Hz clock
   or two, not in Play's stretch. 1 when its bootstrap waits for its
   512 words */
int dspmp3_reset_step(void)
{
	switch (rst) {
	case 0:
		Ongibit(0x10);
		rst_t = tos_ms();
		rst = 1;
		return 0;
	case 1:
		if (tos_ms() - rst_t < 5)
			return 0;
		Offgibit(~0x10);
		rst_t = tos_ms();
		rst = 2;
		return 0;
	case 2:
		if (tos_ms() - rst_t < 5)
			return 0;
		rst = 3;
		/* fall through */
	default:
		return 1;
	}
}

void dspmp3_close(void)
{
	if (locked) {
		Dsp_Unlock();
		locked = 0;
	}
}

static long reset_cmd(void)
{
	put(5);
	return 0;
}

void dspmp3_reset(void)
{
	Supexec(reset_cmd);
}

/* the frame's packet, in supervisor mode (the tick) */
void dspmp3_tick_send(const unsigned long *pk, int n)
{
	int i;
	put_n = n + 2;
	put_i = 0;
	put(11);			/* PLAY */
	put_i++;
	put((unsigned long)n);
	for (i = 0; i < n; i++) {
		put_i++;
		put(pk[i]);
	}
	put_n = 0;
}

int dspmp3_tick_ready(void)
{
	return HISR & 1;
}

/* the frame's samples, in supervisor mode (the tick): while a word is
   there at once, a run of four instructions a word (the flag, the word,
   the count); when one isn't, the wait for it, a second at most */
void dspmp3_tick_read(short *to, long frames)
{
	long n = frames * 2;
	while (n > 0 && !failed) {
		__asm__ volatile("1:\tbtst\t#0,0xffffa202.w\n"
				 "\tbeq.s\t2f\n"
				 "\tmove.w\t0xffffa206.w,(%0)+\n"
				 "\tsubq.l\t#1,%1\n"
				 "\tbne.s\t1b\n"
				 "2:"
				 : "+a"(to), "+d"(n) : : "cc", "memory");
		if (n > 0 && !(HISR & 1)) {
			unsigned long t0 = HZ200;
			while (!(HISR & 1))
				if (HZ200 - t0 > 200) {
					failed = 1;
					f_what = 2;
					f_at = frames * 2 - n;
					f_of = frames * 2;
					f_hisr = HISR;
					break;
				}
		}
	}
}

void dspmp3_fail_info(int *what, long *at, long *of, int *hisr)
{
	*what = f_what;
	*at = f_at;
	*of = f_of;
	*hisr = f_hisr;
}

int dspmp3_failed(void)
{
	return failed;
}

/* ---- STREAM (12): the DSP plays its frames from its SSI on the
   matrix's clock. A packet goes when the DSP asks (HF2: its packet
   buffer free), a word whenever the host port has room, never waiting
   for it: what doesn't go this tick goes the next. After each frame the
   DSP says how it kept up: granules late (bits 16-23, a count that runs
   on) and its waits for the sound to move on (bits 0-15: the time it had
   to spare) */
#define HF2 0x08
static const unsigned long *s_buf;	/* the packet going (the player's, kept until sent) */
static int s_n, s_i, s_have;		/* its words, the next (-1: its count) */
static long s_frames, s_late, s_wmin, s_wsum;
static int s_late8;
static unsigned long s_last;		/* HZ200 at the last status word */

void dspmp3_tick_stream(void)
{
	s_have = 0;
	s_frames = s_late = s_wsum = 0;
	s_wmin = -1;
	s_late8 = 0;
	s_last = HZ200;
	put(12);			/* STREAM */
}

int dspmp3_tick_asks(void)
{
	return !s_have && (HISR & HF2);
}

void dspmp3_tick_offer(const unsigned long *pk, int n)
{
	s_buf = pk;
	s_n = n;
	s_i = -1;
	s_have = 1;
}

void dspmp3_tick_push(void)
{
	while (s_have && (HISR & 2)) {
		if (s_i < 0) {
			HTX32 = (unsigned long)s_n;
			s_i = 0;
		} else
			HTX32 = s_buf[s_i++] & 0xffffff;
		if (s_i == s_n)
			s_have = 0;
	}
}

int dspmp3_tick_pushing(void)
{
	return s_have;
}

void dspmp3_tick_status(void)
{
	if (HISR & 1) {
		unsigned long w = HTX32 & 0xffffff;
		long waits = (long)(w & 0xffff);
		int l8 = (int)(w >> 16) & 0xff;
		s_late += (l8 - s_late8) & 0xff;
		s_late8 = l8;
		if (s_frames >= 4 && (s_wmin < 0 || waits < s_wmin))
			s_wmin = waits;		/* (past the start, the ring's first fill) */
		s_wsum += waits;
		s_frames++;
		s_last = HZ200;
	}
	if (HZ200 - s_last > 400 && !failed) {	/* two seconds without a frame */
		failed = 1;
		f_what = 4;
		f_at = s_frames;
		f_of = 0;
		f_hisr = HISR;
	}
}

void dspmp3_stream_info(long *frames, long *late, long *wmin, long *wavg)
{
	*frames = s_frames;
	*late = s_late;
	*wmin = s_wmin;
	*wavg = s_frames ? s_wsum / s_frames : 0;
}
