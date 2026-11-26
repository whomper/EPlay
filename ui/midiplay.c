/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * midiplay.c - a MIDI file out of the MIDI port (midiplay.h). MIDI.OVL
 * makes the file into records (smf.h: a time, a length, the bytes), all
 * of it at the open, into memory the tick reads; the tick (feed.h, Timer A
 * 997 times a second; the GEM turn's on the FireBee and with the XBIOS's
 * sound alone) puts each record that is due into a small ring and sends
 * the ring to the MIDI ACIA ($FFFC04, its data at $FFFC06), a byte each
 * time its TDRE flag says the last has gone, never timed by the CPU's
 * speed. The ACIA's interrupt and midivec are left alone (they are the
 * keyboard's too); nothing is sent while no MIDI file plays (the port's
 * rules: DESIGN.md). Nothing the music needs waits on GEM.
 *
 * At each start: the General MIDI reset (GM System On), 150 ms for the
 * synth to take it, then the file's state at that place (its system
 * exclusive messages and each channel's bank, program, controllers and
 * pitch bend before it: a seek's or Play's after Pause), then its bytes.
 * At Stop and Pause, and at the file's end: All Notes Off and Reset All
 * Controllers on all 16 channels. The volume is the GM master volume.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tos.h"
#include "tickcode.h"
#include "midiovl.h"
#include "midiplay.h"
#include "ovl.h"
#include "feed.h"
#include "snd.h"
#include "elog.h"
#include "xalloc.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */

#define ACIA_SR (*(volatile unsigned char *)0xfffffc04L)
#define ACIA_DR (*(volatile unsigned char *)0xfffffc06L)
#define TDRE 0x02
#define HZ200 (*(volatile unsigned long *)0x4baL)

#define SETTLE 150		/* ms after the reset */
#define PRE 8192		/* the start's bytes: the reset and the file's state */
#define FIFO 512		/* the tick's ring (a power of 2) */

static const MIDIOVL *ov;
/* the block: PRE bytes for the start, then the records (mem), made a
   piece at a time as it plays (smf_more: its first seconds at the open,
   the rest in GEM's turns, mp_step); the tick reads up to mem_n */
static unsigned char *blk, *mem;
static volatile long mem_n;	/* the records' bytes made (whole records) */
static long mem_cap;
static unsigned long made_ms;	/* the time they reach */
static int making;		/* more to make */
static unsigned char *fbuf;	/* the file, kept while its records are made */
static long fsize;
static SMF_ST *st, *pst;	/* the file playing's state; the list's (mp_probe) */
static SMF_INFO inf, minf;	/* (the length's; the making's) */
#define AHEAD 4000		/* ms of records made ahead of the music */
static char kept_name[64];
static long kept_size = -1;

static unsigned char *pre;
static int pre_n, pre_reset;	/* its bytes; the reset's (sent before SETTLE) */
static volatile int pre_at;
static volatile long rp;	/* the next record */
static unsigned char fifo[FIFO];
static volatile unsigned short f_r, f_w;
static volatile long el;	/* ms since the start (the tick's) */
static volatile int ended, on;
static volatile int vol_due = -1;	/* the master volume for the tick to send (0 to 16383) */
static long base_ms, len_ms;
static long seen, frac;
static long budget_us;
static long sent, late_max;	/* (the log's: bytes sent, the latest a message went, ms) */

int mp_magic(const unsigned char *b, long n)
{
	return n >= 4 && b[0] == 'M' && b[1] == 'T' && b[2] == 'h' && b[3] == 'd';	/* (smf.c's own is MIDI.OVL's) */
}

int mp_port(void)
{
	/* the ST, STE, Mega STE, TT and Falcon have the MIDI ACIA; the
	   FireBee's FPGA has it at the same place */
	if (get_cookie(0x5F43465FL))		/* '_CF_' */
		return 1;
	return (get_cookie(0x5F4D4348L) >> 16) <= 3;	/* '_MCH' */
}

static int load(char *err, int n)
{
	long taken;
	const MIDIOVL *o;
	if (ov)
		return 1;
	o = ovl_load("midi.ovl", &taken, err, n);
	if (!o) {
		snprintf(err, n, "MIDI files need MIDI.OVL beside EPlay.");
		return 0;
	}
	ovl_flush();
	if (o->magic != MIDIOVL_MAGIC || o->major != MIDIOVL_MAJOR || o->size < (short)sizeof(MIDIOVL)) {
		free((void *)o);
		snprintf(err, n, "MIDI.OVL isn't this EPlay's: copy it from its release.");
		return 0;
	}
	ov = o;
	elog("MIDI.OVL %s: %s, %ld bytes at $%lX", o->version, o->what, taken, (unsigned long)o);
	return 1;
}

static void give(void)
{
	if (blk)
		Mfree(blk);
	if (fbuf)
		Mfree(fbuf);
	blk = mem = fbuf = 0;
	mem_n = mem_cap = 0;
	making = 0;
	kept_size = -1;
	kept_name[0] = 0;
}

/* a state block for the overlay: 0 with err set */
static SMF_ST *state(SMF_ST **s, char *err, int n)
{
	if (!*s) {
		*s = x_alloc(ov->state_size(), 3);
		if ((long)*s <= 0) {
			*s = 0;
			snprintf(err, n, "There isn't the memory to play.");
		}
	}
	return *s;
}

/* records made till they reach ms, or for budget ms (0: no limit) */
static void make(unsigned long ms, long budget)
{
	unsigned long t0 = tos_ms();
	while (making && (made_ms < ms || (budget && (long)(tos_ms() - t0) < budget))) {
		long made;
		int r = ov->more(st, 32, &made, &made_ms);	/* (32: some 10 ms on an STE) */
		mem_n = made;	/* (whole records: the tick may send them now) */
		if (r == 1) {
			if (budget && (long)(tos_ms() - t0) >= budget)
				break;
			continue;
		}
		making = 0;
		Mfree(fbuf);
		fbuf = 0;
		if (r == 0) {
			trap1_wwll(0x4a, 0, (long)blk, PRE + made);	/* Mshrink: the block to what it took */
			elog("MIDI: all made: %ld messages, %ld bytes", minf.events, made);
		} else
			elog("MIDI: made to %lu ms of it (no room for more)", made_ms);
	}
}

/* the file read whole: 0 with err set */
static unsigned char *read_all(int fh, long size, char *err, int n)
{
	unsigned char *b;
	if (size < 14 || size > 8L * 1024 * 1024) {
		snprintf(err, n, size < 14 ? "The MIDI file is damaged." : "The MIDI file is too large.");
		return 0;
	}
	b = x_alloc(size, 3);
	if ((long)b <= 0) {
		snprintf(err, n, "There isn't the memory to play.");
		return 0;
	}
	if (Fseek(0, (short)fh, 0) != 0 || Fread((short)fh, size, b) != size) {
		Mfree(b);
		snprintf(err, n, "The MIDI file can't be read.");
		return 0;
	}
	return b;
}

TICK_CODE("70mp_rec")
static unsigned long rec_ms(long at)
{
	return (unsigned long)mem[at] << 24 | (unsigned long)mem[at + 1] << 16 | (unsigned long)mem[at + 2] << 8 |
	       mem[at + 3];
}

static void pre_put(const unsigned char *b, int n)
{
	if (pre_n + n <= PRE) {
		memcpy(pre + pre_n, b, n);
		pre_n += n;
	}
}

/* the reset, then the file's state before ms (its system exclusive
   messages, each channel's bank, program, controllers and pitch bend);
   rp the first record at ms or after */
static void chase(long ms)
{
	static unsigned char cc[16][128], prog[16], have[16][128 / 8];
	static unsigned short bend[16];
	static const unsigned char gm_on[] = { 0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7 };
	unsigned short used = 0, has_prog = 0, has_bend = 0;
	long at = 0;
	int ch, c;
	memset(have, 0, sizeof(have));
	pre_n = 0;
	pre_put(gm_on, sizeof(gm_on));
	pre_reset = pre_n;
	make((unsigned long)ms + 1, 0);	/* (its records to there first) */
	while (at < mem_n && (long)rec_ms(at) < ms) {
		const unsigned char *r = mem + at + SMF_REC_HEAD;
		int k = mem[at + 4], st = r[0];
		if (st == 0xf0 || (st < 0x80 && k)) {
			/* (a system exclusive message, or a piece of one: as it was;
			   the reset already sent is the file's own too) */
			if (!(k == sizeof(gm_on) && !memcmp(r, gm_on, k)))
				pre_put(r, k);
		} else {
			/* a record of channel messages, one ms's */
			int j = 0;
			while (j < k) {
				const unsigned char *q = r + j;
				int s = q[0], len = (s & 0xe0) == 0xc0 ? 2 : 3;
				if (s < 0x80 || s >= 0xf0)
					break;
				ch = s & 15;
				switch (s & 0xf0) {
				case 0xb0:
					if (q[1] < 120) {
						cc[ch][q[1]] = q[2];
						have[ch][q[1] >> 3] |= (unsigned char)(1 << (q[1] & 7));
						used |= 1 << ch;
					}
					break;
				case 0xc0:
					prog[ch] = q[1];
					has_prog |= 1 << ch;
					used |= 1 << ch;
					break;
				case 0xe0:
					bend[ch] = (unsigned short)(q[1] | q[2] << 7);
					has_bend |= 1 << ch;
					used |= 1 << ch;
					break;
				}
				j += len;
			}
		}
		at += SMF_REC_HEAD + k;
	}
	rp = at;
	for (ch = 0; ch < 16; ch++) {
		unsigned char m[3];
		if (!(used & (1 << ch)))
			continue;
		m[0] = (unsigned char)(0xb0 | ch);
		/* the bank before the program, the rest after; not the data
		   entry nor the (N)RPN numbers, out of their order */
		for (c = 0; c < 64; c += 32)
			if (have[ch][c >> 3] & (1 << (c & 7))) {
				m[1] = (unsigned char)c;
				m[2] = cc[ch][c];
				pre_put(m, 3);
			}
		if (has_prog & (1 << ch)) {
			m[0] = (unsigned char)(0xc0 | ch);
			m[1] = prog[ch];
			pre_put(m, 2);
			m[0] = (unsigned char)(0xb0 | ch);
		}
		for (c = 1; c < 120; c++) {
			if (c == 32 || c == 6 || c == 38 || (c >= 96 && c <= 101))
				continue;
			if (have[ch][c >> 3] & (1 << (c & 7))) {
				m[1] = (unsigned char)c;
				m[2] = cc[ch][c];
				pre_put(m, 3);
			}
		}
		if (has_bend & (1 << ch)) {
			m[0] = (unsigned char)(0xe0 | ch);
			m[1] = bend[ch] & 0x7f;
			m[2] = (bend[ch] >> 7) & 0x7f;
			pre_put(m, 3);
		}
	}
}

int mp_open(int fh, long size, long ms, const char *name, TAGS *t, char *info, int ni, char *out, int no,
	    long *len, char *err, int n)
{
	if (!mp_port()) {
		snprintf(err, n, "This machine has no MIDI port.");
		return 0;
	}
	if (!load(err, n))
		return 0;
	if (!(blk && kept_size == size && !strcmp(kept_name, name))) {
		unsigned long t0 = tos_ms();
		long l;
		give();
		if (!state(&st, err, n) || !(fbuf = read_all(fh, size, err, n)))
			return 0;
		fsize = size;
		/* its length and name, quickly (no records made); a file with
		   more tempo changes than that keeps: counted whole */
		memset(&inf, 0, sizeof(inf));
		l = ov->length(st, fbuf, size, &inf, err, n);
		if (l == -2)
			l = ov->render(fbuf, size, 0, 0, &inf, err, n) >= 0 ? inf.len_ms : -1;
		if (l < 0) {
			give();
			return 0;
		}
		/* the block for the most the records can be (a message's bytes
		   at most 3.5 times the file's: a program change of two bytes,
		   its time and its status written out), shrunk to them at the
		   end; where that much isn't there, counted first */
		mem_cap = size * 4 + 64;
		blk = x_alloc(PRE + mem_cap, 3);	/* (TT RAM where there is some: the tick reads it) */
		if ((long)blk <= 0) {
			long need = ov->render(fbuf, size, 0, 0, &minf, err, n);
			blk = 0;
			if (need >= 0) {
				mem_cap = need;
				blk = x_alloc(PRE + mem_cap, 3);
				if ((long)blk <= 0)
					blk = 0;
			}
			if (!blk) {
				give();
				snprintf(err, n, "There isn't the memory to play.");
				return 0;
			}
		}
		mem = blk + PRE;
		mem_n = 0;
		made_ms = 0;
		if (ov->begin(st, fbuf, size, mem, mem_cap, &minf, err, n) < 0) {
			give();
			return 0;
		}
		making = 1;
		kept_size = size;
		snprintf(kept_name, sizeof(kept_name), "%s", name);
		make(AHEAD, 0);
		elog("MIDI: type %d, %d tracks, division %d; %ld messages, %ld ms; channels $%04X; its first %lu ms made in %lu ms",
		     inf.type, inf.tracks, inf.division, inf.events, inf.len_ms, inf.channels, made_ms,
		     (unsigned long)(tos_ms() - t0));
	}
	pre = blk;
	len_ms = inf.len_ms;
	if (ms > len_ms)
		ms = len_ms;
	chase(ms);
	base_ms = ms;
	if (t && inf.title[0] && !t->title[0])
		snprintf(t->title, sizeof(t->title), "%s", inf.title);
	snprintf(info, ni, "MIDI  type %d  %d track%s", inf.type, inf.tracks, inf.tracks == 1 ? "" : "s");
	snprintf(out, no, "MIDI port");
	*len = len_ms;
	el = 0;
	ended = 0;
	return 1;
}

/* the tick's: the ring's room, a byte in */
#define ROOM() ((unsigned short)(FIFO - 1 - ((f_w - f_r) & (FIFO - 1))))

TICK_CODE("70mp_push")
static void push(const unsigned char *b, int k)
{
	unsigned short w = f_w;
	while (k--) {
		fifo[w] = *b++;
		w = (w + 1) & (FIFO - 1);
	}
	f_w = w;
}

/* the tick (an interrupt; on the FireBee a GEM turn's, by Supexec) */
TICK_CODE("70mp_tick")
static void tick(void)
{
	long d, t;
	unsigned short r;
	feed_t0();
	/* the ms since the start: Timer A's 1,002.6 us a tick (385/384 ms);
	   the turns' ticks are ms */
	d = feed_ticks - seen;
	seen += d;
	if (SND_XB)
		el += d;
	else {
		frac += d * 385;
		while (frac >= 384) {
			frac -= 384;
			el++;
		}
	}
	/* the reset at once, the file's state and bytes once the synth took it */
	while (pre_at < (el >= SETTLE ? pre_n : pre_reset) && ROOM()) {
		push(pre + pre_at, 1);
		pre_at++;
	}
	/* the master volume, once the reset (which sets it to the most) went */
	if (vol_due >= 0 && pre_at >= pre_reset && ROOM() >= 8) {
		unsigned char v[8] = { 0xf0, 0x7f, 0x7f, 0x04, 0x01, 0, 0, 0xf7 };
		v[5] = (unsigned char)(vol_due & 0x7f);
		v[6] = (unsigned char)(vol_due >> 7);
		push(v, 8);
		vol_due = -1;
	}
	if (pre_at >= pre_n) {
		t = base_ms + el - SETTLE;
		while (rp < mem_n) {
			int k = mem[rp + 4];
			long at = (long)rec_ms(rp);
			if (at > t || ROOM() < k)
				break;
			if (t - at > late_max)
				late_max = t - at;
			push(mem + rp + SMF_REC_HEAD, k);
			rp += SMF_REC_HEAD + k;
		}
	}
	/* the ring to the ACIA: a byte each time TDRE says the last has
	   gone, for budget_us at most */
	r = f_r;
	while (r != f_w) {
		if (ACIA_SR & TDRE) {
			ACIA_DR = fifo[r];
			r = (r + 1) & (FIFO - 1);
			sent++;
		} else if (feed_us() >= budget_us)
			break;
	}
	f_r = r;
	if (!making && rp >= mem_n && r == f_w && pre_at >= pre_n && base_ms + el - SETTLE >= len_ms)
		ended = 1;
}

/* (supervisor mode) a byte to the ACIA, TDRE waited for 100 ms at most */
static int out1(int b)
{
	unsigned long t0 = HZ200;
	while (!(ACIA_SR & TDRE))
		if (HZ200 - t0 > 20)
			return 0;
	ACIA_DR = (unsigned char)b;
	return 1;
}

/* (supervisor mode) the ring's last bytes, then all 16 channels' notes
   off and controllers reset */
static long all_off(void)
{
	int ch;
	while (f_r != f_w) {
		if (!out1(fifo[f_r]))
			break;
		f_r = (f_r + 1) & (FIFO - 1);
	}
	f_r = f_w = 0;
	for (ch = 0; ch < 16; ch++)
		if (!(out1(0xb0 | ch) && out1(123) && out1(0) && out1(121) && out1(0)))
			break;
	return 0;
}

void mp_start(void)
{
	if (!blk)
		return;
	pre_at = 0;
	f_r = f_w = 0;
	el = 0;
	seen = frac = 0;
	ended = 0;
	sent = late_max = 0;
	budget_us = SND_XB ? 10000 : 350;	/* (a GEM turn's: some 20 ms between) */
	on = 1;
	feed_on(tick, 1000);
}

static void off(void)
{
	if (on) {
		long ms = mp_ms();
		feed_off();
		Supexec(all_off);
		on = 0;
		base_ms = ms;
		elog("MIDI: %ld bytes sent; the latest message %ld ms after its time", sent, late_max);
	}
}

void mp_halt(void)
{
	off();
}

void mp_stop(void)
{
	off();
}

void mp_forget(void)
{
	off();
	give();
}

int mp_step(int ms)
{
	if (making)
		make((unsigned long)mp_ms() + AHEAD, ms > 0 ? ms : 1);
	return making;
}

long mp_ms(void)
{
	long t;
	if (!on)
		return base_ms;
	t = base_ms + (el > SETTLE ? el - SETTLE : 0);
	return t < len_ms ? t : len_ms;
}

int mp_ended(void)
{
	return ended;
}

void mp_volume(int att, int mute)
{
	long v = 16383;
	int i;
	/* (1.5 dB a step: 0.8414) */
	for (i = 0; i < att && i < 60; i++)
		v = v * 8414 / 10000;
	vol_due = mute ? 0 : (int)v;
}

long mp_probe(int fh, long size, TAGS *t)
{
	char err[80];
	unsigned char *b;
	SMF_INFO i;
	long r;
	if (!load(err, sizeof(err)) || !state(&pst, err, sizeof(err)) || !(b = read_all(fh, size, err, sizeof(err))))
		return -1;
	memset(&i, 0, sizeof(i));
	r = ov->length(pst, b, size, &i, err, sizeof(err));
	if (r == -2)
		r = ov->render(b, size, 0, 0, &i, err, sizeof(err)) >= 0 ? i.len_ms : -1;
	Mfree(b);
	if (r < 0)
		return -1;
	if (t && i.title[0])
		snprintf(t->title, sizeof(t->title), "%s", i.title);
	return r;
}

const char *mp_log(void)
{
	static char b[80];
	snprintf(b, sizeof(b), "%ld bytes to the MIDI port, the latest %ld ms after its time", sent, late_max);
	return b;
}
