/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * snd.c - the Falcon's sound (snd.h), through its XBIOS: Locksnd,
 * Devconnect, Soundcmd, Setbuffer, Setmode, Buffoper and Buffptr. The
 * ring is in ST RAM (Mxalloc mode 0: the DMA reads nothing else); the
 * matrix's registers ($FF8930 on) are kept at the start and written back
 * at the end, since the XBIOS can't say what they were (the FDI's program
 * sets them at boot).
 */
#include <string.h>
#include "tos.h"
#include "snd.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed hardware addresses */

/* the XBIOS's sound calls */
static long x_setbuffer(short reg, void *beg, void *end)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %3,-(%%sp)\n\tmove.l %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\t"
			 "move.w #131,-(%%sp)\n\ttrap #14\n\tlea 12(%%sp),%%sp"
			 : "=r"(r) : "r"(reg), "r"(beg), "r"(end) : TRAP_CLOBBER);
	return r;
}
#define Locksnd()		trap14_w(128)
#define Unlocksnd()		trap14_w(129)
#define Soundcmd(m, d)		trap14_w6(130, (m), (d), 0, 0, 0, 0)
#define Setmode(m)		trap14_ww(132, (m))
#define Settracks(p, r)		trap14_w6(133, (p), (r), 0, 0, 0, 0)
#define Setmontracks(t)		trap14_ww(134, (t))
#define Buffoper(m)		trap14_ww(136, (m))
#define Devconnect(s, d, c, p, h) trap14_w6(139, (s), (d), (c), (p), (h), 0)
#define Buffptr(p)		trap14_wl(141, (long)(p))

#define DMAPLAY		0
#define EXTOUT		4
#define DAC		8
#define CLK25M		0
#define CLKEXT		1
#define NO_SHAKE	1
#define STEREO16	1
#define PLAY_ENABLE	1
#define PLAY_REPEAT	2

long snd_ext_rate;

static int locked;
static short *ring;		/* ST RAM */
static long ring_n;		/* frames */
static volatile long wpos, written, played;	/* the next frame to write; totals */
static long last_ppos;
static volatile int playing;
long snd_underruns;

/* the matrix, as found: $FF8920 (the DMA's mode), $FF8930-$FF893A */
static unsigned short keep[8];
static long keep_regs(void)
{
	volatile unsigned short *r = (volatile unsigned short *)0xffff8930L;
	int i;
	for (i = 0; i < 6; i++)
		keep[i] = r[i];
	keep[6] = *(volatile unsigned short *)0xffff8920L;
	return 0;
}
static long put_regs(void)
{
	volatile unsigned short *r = (volatile unsigned short *)0xffff8930L;
	int i;
	for (i = 0; i < 6; i++)
		r[i] = keep[i];
	*(volatile unsigned short *)0xffff8920L = keep[6];
	return 0;
}

/* where the DMA plays now, as a frame of the ring */
static long pos_once(void)
{
	long p[4];
	Buffptr(p);
	if (p[0] < (long)ring || p[0] > (long)(ring + 2 * ring_n))
		return -1;
	return (p[0] - (long)ring) / 4 % ring_n;
}

/* the DMA's counter is read a byte at a time, and a read as it moves on
   can be torn (a carry between two bytes: 256 frames or 64K bytes out,
   seen in Hatari as a whole ring counted twice): two reads that agree
   (within 256 frames, the ring's wrap allowed) are taken; -1 if none */
static long play_pos(void)
{
	long a = pos_once(), b;
	int i;
	for (i = 0; i < 4; i++) {
		long d;
		b = pos_once();
		d = b - a;
		if (d < 0)
			d += ring_n;
		if (a >= 0 && b >= 0 && d < 256)
			return b;
		a = b;
	}
	return -1;
}

static const long internal[] = { 49170, 32780, 24585, 19668, 16390, 12292, 9834, 8195 };
static const short prescale[] = { 1, 2, 3, 4, 5, 7, 9, 11 };

/* the external clock: the DMA started on it over silence, and how far it
   gets in 200 ms; none, and it doesn't move */
static long measure_ext(void)
{
	long a, b, t0, rate;
	if (!snd_open(44100, 1))
		return 0;
	/* over silence: a short part of the ring, cleared (64 KB; the rest
	   is played only once written) */
	ring_n = 16384;
	memset(ring, 0, (size_t)ring_n * 4);
	x_setbuffer(0, ring, ring + 2 * ring_n);
	Buffoper(PLAY_ENABLE | PLAY_REPEAT);
	t0 = (long)tos_ms();
	while ((long)tos_ms() - t0 < 20)
		;
	a = play_pos();
	t0 = (long)tos_ms();
	while ((long)tos_ms() - t0 < 200)
		;
	b = play_pos();
	Buffoper(0);
	snd_close();
	if (a < 0 || b < 0)
		return 0;
	if (b < a)
		b += ring_n;
	rate = (b - a) * 5;		/* frames in 200 ms (16384 is 0.33 s at 49.2 kHz) */
	/* the FDI's two, near enough (the 200 Hz clock is coarse); anything
	   else isn't a clock to play by (Hatari's runs at 50 kHz) */
	if (rate > 43000 && rate < 45200)
		return 44100;
	if (rate > 46800 && rate < 49200)
		return 48000;
	return 0;
}

int snd_init(const char **why)
{
	long snd = get_cookie(0x5F534E44L);	/* _SND: bit 2 the codec, 4 the matrix */
	if ((snd & 0x14) != 0x14) {
		*why = "EPlay 0.1 needs a Falcon's sound.";
		return 0;
	}
	if (Locksnd() != 1) {
		*why = "Another program is using the sound.";
		return 0;
	}
	locked = 1;
	Supexec(keep_regs);
	snd_ext_rate = measure_ext();
	return 1;
}

void snd_exit(void)
{
	snd_close();
	if (locked) {
		Supexec(put_regs);
		Unlocksnd();
		locked = 0;
	}
}

long snd_rate_for(long rate, int *ext)
{
	long best = internal[0], d, bd = 1L << 30;
	unsigned i;
	*ext = 0;
	if (snd_ext_rate && (rate - snd_ext_rate < snd_ext_rate / 100 && snd_ext_rate - rate < snd_ext_rate / 100)) {
		*ext = 1;
		return snd_ext_rate;
	}
	for (i = 0; i < sizeof(internal) / sizeof(internal[0]); i++) {
		d = rate > internal[i] ? rate - internal[i] : internal[i] - rate;
		if (d < bd) {
			bd = d;
			best = internal[i];
		}
	}
	return best;
}

int snd_open(long rate, int ext)
{
	short pre = 1;
	unsigned i;
	if (!ring) {
		long n = 2 * 49170L;			/* two seconds at the most */
		short *p = Mxalloc(n * 4, 0);
		if ((long)p <= 0)
			return 0;
		ring = p;
		ring_n = n;
	}
	if (!ext)
		for (i = 0; i < sizeof(internal) / sizeof(internal[0]); i++)
			if (internal[i] == rate)
				pre = prescale[i];
	Buffoper(0);
	/* (not cleared: the DMA starts once a quarter of a second is
	   written, and plays only what was) */
	/* the ring two seconds long at this rate (the DMA's end is the ring's) */
	ring_n = 2 * rate;
	if (ring_n > 2 * 49170L)
		ring_n = 2 * 49170L;
	Settracks(0, 0);
	Setmontracks(0);
	Setmode(STEREO16);
	Devconnect(DMAPLAY, DAC | EXTOUT, ext ? CLKEXT : CLK25M, pre, NO_SHAKE);
	x_setbuffer(0, ring, ring + 2 * ring_n);
	wpos = written = played = 0;
	last_ppos = 0;
	playing = 0;
	snd_underruns = 0;
	return 1;
}

void snd_close(void)
{
	if (ring) {
		Buffoper(0);
		playing = 0;
	}
}

long snd_ring_frames(void)
{
	return ring_n;
}

/* where the DMA plays now, from its counter's three bytes ($FF8909,
   $FF890B, $FF890D: supervisor mode, so from the tick only), two reads
   that agree as play_pos's; -1 if none */
static long hw_once(void)
{
	volatile unsigned char *c = (volatile unsigned char *)0xffff8909L;
	long a = (long)c[0] << 16 | (long)c[2] << 8 | c[4];
	if (a < (long)ring || a > (long)(ring + 2 * ring_n))
		return -1;
	a = (a - (long)ring) >> 2;	/* (no division: the tick's) */
	return a >= ring_n ? a - ring_n : a;
}

static long hw_pos(void)
{
	long a = hw_once(), b;
	int i;
	for (i = 0; i < 4; i++) {
		long d;
		b = hw_once();
		d = b - a;
		if (d < 0)
			d += ring_n;
		if (a >= 0 && b >= 0 && d < 256)
			return b;
		a = b;
	}
	return -1;
}

/* the frames played, from the DMA's position (the tick, a thousand times
   a second: it must be asked at least once a ring) */
void snd_update(void)
{
	long p, d;
	if (!playing)
		return;
	p = hw_pos();
	if (p < 0)
		return;			/* (no reading to trust: next time) */
	d = p - last_ppos;
	if (d < 0)
		d += ring_n;
	if (d > ring_n - 256)
		d = 0;			/* (a little behind: still the same place) */
	if (!d)
		return;
	last_ppos = p;
	if (played + d > written) {	/* past what was written: it played old sound */
		snd_underruns++;
		written = played + d;
		wpos = p;
	}
	played += d;
}

long snd_played(void)
{
	return played;
}

long snd_written(void)
{
	return written;
}

long snd_room(void)
{
	long r = ring_n - (written - played) - 1024;	/* a guard behind the DMA */
	return r > 0 ? r : 0;
}

short *snd_ptr(long *n)
{
	*n = ring_n - wpos;
	return ring + 2 * wpos;
}

void snd_wrote(long n)
{
	wpos += n;
	if (wpos >= ring_n)
		wpos -= ring_n;
	written += n;
}

void snd_write(const short *frames, long n)
{
	while (n > 0) {
		long k;
		short *p = snd_ptr(&k);
		if (k > n)
			k = n;
		memcpy(p, frames, (size_t)k * 4);
		frames += 2 * k;
		snd_wrote(k);
		n -= k;
	}
}

void snd_play(void)
{
	if (!playing) {
		/* the DMA starts at the ring's start; Buffptr, until it has,
		   may still say where the last one stopped (measure_ext's) */
		last_ppos = 0;
		Buffoper(PLAY_ENABLE | PLAY_REPEAT);
		playing = 1;
	}
}

int snd_playing(void)
{
	return playing;
}
