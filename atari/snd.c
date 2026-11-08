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
#include <stdio.h>
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
#define Dsptristate(x, r)	trap14_w6(137, (x), (r), 0, 0, 0, 0)
#define Gpio(m, d)		trap14_w6(138, (m), (d), 0, 0, 0, 0)

#define DMAPLAY		0
#define DSPXMIT		1
#define DSPRECV		2
#define EXTOUT		4
#define DAC		8
#define CLK25M		0
#define CLKEXT		1
#define NO_SHAKE	1
#define STEREO16	1
#define PLAY_ENABLE	1
#define PLAY_REPEAT	2
#define RING_MAX	24585L	/* frames: half a second at the Falcon's fastest (393 KB of ST RAM, four tracks: as one track's two seconds were) */

long snd_ext_rate;

static int locked;
/* four stereo tracks a frame, the music in the first: with one, the DMA
   put the left channel in the DSP port's other six slots as well, heard on
   the Analog 8's channels 3 to 8 (Erez's lrtest.wav, 0.19). The other
   three are zeros, written once when the ring is taken */
#define TRACKS		4
#define FW		(2 * TRACKS)	/* a frame's words to the DAC */
/* the ring's frame now: FW words to the DAC (its four tracks), or 2 to
   the DSP (snd_open_feed: one track, through the matrix to its receive) */
int snd_fw = FW;
static int fw_shift = 4;	/* (bytes a frame, as a shift: 16, or 4) */
static long dirty;		/* bytes of the ring a feed wrote densely: the DAC's silent
				   tracks to clear there before it plays four again */
static short *ring;		/* ST RAM */
static long ring_n;		/* frames */
static volatile long wpos, written, played;	/* the next frame to write; totals */
static long last_ppos;
static volatile int playing;
long snd_underruns;
long snd_first_under = -1;	/* frames played at the first underrun (-1: none) */
long snd_least_lead = -1;	/* the fewest frames written ahead, past the first second */

/* ---- the TT's and the STE's sound (phase 6, Erez 7 Nov 2026): 8-bit
   stereo DMA, no matrix, no XBIOS sound calls: its registers at $FF8900
   set directly. A frame two bytes (left, right) in a ring in ST RAM. The
   music comes in as 16-bit frames at the file's rate (snd_ptr's staging
   buffer, as on the Falcon) and snd_wrote puts it at the DMA's rate: on a
   TT interpolated and dithered to 8 bits, on an STE the nearest sample.
   played and written count the DMA's frames; snd_played and snd_written
   give them as the file's (play.c's time) */
int snd_dma8;			/* the TT's or STE's sound */
int snd_dma8_interp;		/* interpolate and dither (a 68030), else the nearest sample */
#define D8(o)	(*(volatile unsigned char *)(0xffff8900L + (o)))
#define RING8_MAX	25033L	/* frames: half a second at 50,066 Hz (50 KB) */
#define STAGE_N		1024	/* the staging buffer's frames */
static unsigned char *ring8;
static short stage[2 * STAGE_N];
static long in8, out8;		/* the file's rate, the DMA's */
static unsigned long ph8, step8;	/* the position in the file's frames, 16.16; the step a frame out */
static short h8l, h8r;		/* the file's last frame before stage[0] */
static long e8l, e8r;		/* the dither's errors carried */
static unsigned char mode8, keep8[2];
static const long rates8[] = { 50066, 25033, 12517, 6258 };	/* $FF8921's 3, 2, 1, 0 */
static int nrates8 = 4;
static unsigned char *start8, *end8;

static long keep_regs8(void)
{
	keep8[0] = D8(0x01);
	keep8[1] = D8(0x21);
	return 0;
}

static long mw_send(void);
static unsigned short mw_cmd;
static long put_regs8(void)
{
	D8(0x01) = 0;
	D8(0x21) = keep8[1];
	if (!snd_dma8_interp) {		/* (the LMC1992 as TOS starts it: 0 dB) */
		mw_cmd = 0x4c0 | 40;
		mw_send();
	}
	return 0;
}

static long set8(void)
{
	unsigned long a = (unsigned long)start8, b = (unsigned long)end8;
	D8(0x01) = 0;			/* stopped */
	D8(0x21) = mode8;		/* stereo, the rate */
	D8(0x03) = (unsigned char)(a >> 16);
	D8(0x05) = (unsigned char)(a >> 8);
	D8(0x07) = (unsigned char)a;
	D8(0x0f) = (unsigned char)(b >> 16);
	D8(0x11) = (unsigned char)(b >> 8);
	D8(0x13) = (unsigned char)b;
	return 0;
}

static long go8(void)
{
	D8(0x01) = 3;			/* play, round and round */
	return 0;
}

static long stop8(void)
{
	D8(0x01) = 0;
	return 0;
}

/* the DMA's n frames as the file's, rounded down: n a ring's at most.
   In 32 bits, by in8 / out8 with 12 bits of fraction (r8): the 68000's
   edition has no 64-bit division (libgcc's is 68020 code: EPLAYST.PRG
   met an illegal instruction in __udivdi3, 7 Nov 2026). n * r8 < 25,033
   * 62,830 for files up to 192 kHz */
static unsigned long r8;
static unsigned long in_written;	/* the file's frames given to snd_wrote */
/* a * b by the 68000's mulu.w where both fit 16 bits (gcc otherwise
   calls libgcc's 32-bit multiply, and folds a C cast back into it) */
static inline unsigned long mul16(unsigned long a, unsigned long b)
{
#if defined(__m68k__)
	if (a < 65536UL && b < 65536UL) {
		unsigned long p = a;
		__asm__("mulu.w\t%1,%0" : "+d"(p) : "d"(b) : "cc");
		return p;
	}
#endif
	return a * b;
}

static long as_in(long n)
{
	if (out8 == in8)
		return n;
	return (long)(mul16((unsigned long)n, r8) >> 12);	/* (twice a tick on an STE) */
}

/* a / b by the 68000's divu.w where the quotient fits 16 bits, else
   libgcc's division (a tick's, on the STE) */
static inline unsigned long div16(unsigned long a, unsigned short b)
{
#if defined(__m68k__)
	if (b && (a >> 16) < b) {
		unsigned long q = a;
		__asm__("divu.w\t%1,%0" : "+d"(q) : "dm"(b) : "cc");
		return q & 0xffffUL;
	}
#endif
	return a / b;
}

#define in_for(n) as_in(n)		/* the file's frames that fill n of the DMA's */

/* a 16-bit sample to 8 bits: with its error carried to the next (a
   first-order noise shaping: the error's power moved up, out of the
   middle of what is heard), or cut */
static unsigned char to8(long v, long *e, int dither)
{
	long w;
	if (dither) {
		v += *e;
		w = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
		w = (w + 128) >> 8;
		if (w > 127)
			w = 127;
		*e = v - (w << 8);
		if (*e > 32767 || *e < -32768)
			*e = 0;
		return (unsigned char)w;
	}
	return (unsigned char)(v >> 8);
}

/* k of the file's frames in stage[] to the ring at the DMA's rate: the
   frames made */
static long put8(long k)
{
	long made = 0;
	const short *x = stage;
	unsigned char *r = ring8 + 2 * wpos;
	unsigned char *rend = ring8 + 2 * ring_n;
	unsigned long p = ph8, lim = (unsigned long)k << 16;
	if (snd_dma8_interp) {
		while (p < lim) {
			long i = (long)(p >> 16), f = (long)(p & 0xffff), al, ar, bl, br;
			if (i == 0) {
				al = h8l;
				ar = h8r;
			} else {
				al = x[2 * i - 2];
				ar = x[2 * i - 1];
			}
			bl = x[2 * i];
			br = x[2 * i + 1];
			*r++ = to8(al + (((bl - al) * f) >> 16), &e8l, 1);
			*r++ = to8(ar + (((br - ar) * f) >> 16), &e8r, 1);
			if (r >= rend)
				r = ring8;
			made++;
			p += step8;
		}
	} else {
		while (p < lim) {
			long i = (long)(p >> 16);
			*r++ = (unsigned char)(x[2 * i] >> 8);
			*r++ = (unsigned char)(x[2 * i + 1] >> 8);
			if (r >= rend)
				r = ring8;
			made++;
			p += step8;
		}
	}
	ph8 = p - lim;
	if (k > 0) {
		h8l = x[2 * k - 2];
		h8r = x[2 * k - 1];
	}
	return made;
}

/* the 68000's way (an STE): the file's bytes straight into the ring, no
   staging and no 16-bit pass: for 8 bits out a sample's top byte is all
   there is to take (ol and orr: the left's and right's top bytes in a
   frame of bpf bytes; x 0x80 for unsigned 8-bit WAV). The nearest frame
   at the DMA's rate: a 16-bit fraction a frame out, the file's frames
   stepped through by adds (no multiply, no shift: the 68000's are slow).
   A frame passed over past nin is owed to the next call (skip8). The
   file's frames taken back, at most nin */
static unsigned long skip8;
static unsigned short frac8;

long snd_put8_raw(const unsigned char *src, long nin, int bpf, int ol, int orr, unsigned char x)
{
	unsigned char *r = ring8 + 2 * wpos, *rend = ring8 + 2 * ring_n;
	unsigned short f = frac8, st = (unsigned short)step8;
	long whole = (long)(step8 >> 16), wb = (long)mul16((unsigned long)whole, (unsigned long)bpf), room = ring_n - (written - played) - 1024, made = 0,
	     k = 0;
	const unsigned char *sp = src;
	while (skip8 && k < nin) {	/* (owed from the last call) */
		sp += bpf;
		k++;
		skip8--;
	}
#if defined(__m68k__)
	/* the common case in assembly: signed samples (not 8-bit WAV), at or
	   below the DMA's rate (no whole frame stepped over). What fits is
	   worked out first (to the ring's end, and while the file's frames
	   last), so the loop holds no test but the fraction's carry: for
	   16-bit stereo one movep.w (the two top bytes, two apart) and one
	   move.w a frame, else two byte moves. Hatari's STE profile (7 Nov
	   2026, a CD's 44.1 kHz at 50,066 Hz): the loop with its tests took
	   101 cycles a frame, 63 % of the 68000 */
	while (!x && !whole && made < room && k < nin) {
		const unsigned char *a2 = sp + ol;
		long left = nin - k, n, run;
		unsigned short d = (unsigned short)(orr - ol);
		unsigned long lim;
		short cnt;
		if (left > 16384)
			left = 16384;	/* (left << 16 within 32 bits) */
		/* outputs j = 0.. at f + j st while under left << 16 */
		lim = ((unsigned long)left << 16) - f - 1;
		n = (long)div16(lim, st) + 1;
		if (n > room - made)
			n = room - made;
		run = (long)(rend - r) / 2;
		if (n > run)
			n = run;
		if (n > 32767)
			n = 32767;
		if (n <= 0)
			break;
		cnt = (short)(n - 1);
		if (d == 2 && n >= 2) {
			/* (two frames a pass: half the dbra's; an odd one first) */
			if (n & 1)
				__asm__ volatile("\tmovep.w\t0(%0),%%d0\n"
						 "\tmove.w\t%%d0,(%1)+\n"
						 "\tadd.w\t%3,%2\n"
						 "\tbcc.s\t2f\n"
						 "\tadda.w\t%4,%0\n"
						 "2:"
						 : "+a"(a2), "+a"(r), "+d"(f)
						 : "d"(st), "d"((short)bpf)
						 : "d0", "cc", "memory");
			cnt = (short)(n / 2 - 1);
			__asm__ volatile("1:\tmovep.w\t0(%0),%%d0\n"
					 "\tmove.w\t%%d0,(%1)+\n"
					 "\tadd.w\t%4,%2\n"
					 "\tbcc.s\t2f\n"
					 "\tadda.w\t%5,%0\n"
					 "2:\tmovep.w\t0(%0),%%d0\n"
					 "\tmove.w\t%%d0,(%1)+\n"
					 "\tadd.w\t%4,%2\n"
					 "\tbcc.s\t3f\n"
					 "\tadda.w\t%5,%0\n"
					 "3:\tdbra\t%3,1b"
					 : "+a"(a2), "+a"(r), "+d"(f), "+d"(cnt)
					 : "d"(st), "d"((short)bpf)
					 : "d0", "cc", "memory");
		} else if (d == 2)
			__asm__ volatile("1:\tmovep.w\t0(%0),%%d0\n"
					 "\tmove.w\t%%d0,(%1)+\n"
					 "\tadd.w\t%4,%2\n"
					 "\tbcc.s\t2f\n"
					 "\tadda.w\t%5,%0\n"
					 "2:\tdbra\t%3,1b"
					 : "+a"(a2), "+a"(r), "+d"(f), "+d"(cnt)
					 : "d"(st), "d"((short)bpf)
					 : "d0", "cc", "memory");
		else
			__asm__ volatile("1:\tmove.b\t(%0),(%1)+\n"
					 "\tmove.b\t0(%0,%6.w),(%1)+\n"
					 "\tadd.w\t%4,%2\n"
					 "\tbcc.s\t2f\n"
					 "\tadda.w\t%5,%0\n"
					 "2:\tdbra\t%3,1b"
					 : "+a"(a2), "+a"(r), "+d"(f), "+d"(cnt)
					 : "d"(st), "d"((short)bpf), "d"(d)
					 : "cc", "memory");
		if (r >= rend)
			r = ring8;
		made += n;
		{
			/* the file's frames passed: by a shift where a frame is 2 or
			   4 bytes, else one division a run */
			unsigned long pb = (unsigned long)(a2 - ol - sp);
			long used = bpf == 4 ? (long)(pb >> 2) : bpf == 2 ? (long)(pb >> 1) : (long)(pb / (unsigned)bpf);
			k += used;
			sp += pb;
		}
	}
#endif
	while (made < room && k < nin) {
		unsigned short o = f;
		*r++ = (unsigned char)(sp[ol] ^ x);
		*r++ = (unsigned char)(sp[orr] ^ x);
		if (r >= rend)
			r = ring8;
		made++;
		f = (unsigned short)(f + st);
		sp += wb;
		k += whole;
		if (f < o) {		/* (the fraction carried: a frame more) */
			sp += bpf;
			k++;
		}
	}
	if (k > nin) {
		skip8 = (unsigned long)(k - nin);
		k = nin;
	}
	frac8 = f;
	wpos += made;
	if (wpos >= ring_n)
		wpos -= ring_n;
	written += made;
	in_written += (unsigned long)k;
	return k;
}

/* the STE's and TT's volume: the LMC1992's master volume through the
   Microwire ($FF8922 data, $FF8924 mask): 0 to 40, -80 to 0 dB in 2 dB */
static long mw_send(void)
{
	volatile unsigned short *data = (volatile unsigned short *)0xffff8922L;
	volatile unsigned short *mask = (volatile unsigned short *)0xffff8924L;
	int t;
	for (t = 0; t < 2000 && *mask != 0x07ff; t++)
		;			/* (the last one out) */
	*mask = 0x07ff;
	*data = mw_cmd;
	return 0;
}

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
	if (p[0] < (long)ring || p[0] > (long)(ring + snd_fw * ring_n))
		return -1;
	return (p[0] - (long)ring) / (2 * snd_fw) % ring_n;
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

static short ext_pre = 1;	/* the external clock's prescale (snd_open) */

/* the DMA's rate on the external clock at prescale pre: started over
   silence (a short part of the ring, cleared: 64 KB; the rest is played
   only once written), and how far it gets in 200 ms, its position read
   all the while and the steps added up: 0.2 read it only at the start
   and the end, and a clock past 41 MHz (82 kHz at prescale 1: 45.1584
   MHz is 88.2) went round the 16384 frames in 200 ms and was taken for
   a slow one. 0: it doesn't move */
/* the time it takes, from the MFP's Timer A (free while nothing plays:
   feed.c takes it then) counting down at 2,457,600 / 200 = 12,288 Hz,
   256 counts a round, read as the loop goes: the 200 Hz clock lost its
   steps on Erez's Falcon (0.11: every clock measured 3 to 4 % fast, the
   FDI's 48 kHz as 49,679 Hz), someone's interrupts holding Timer C's
   off for more than its 5 ms; Timer A's count goes on, and a round is
   20 ms. Timer A in use by another program (its control not 0, or its
   interrupt enabled: its handler would be called each round): the 200 Hz
   clock, as before */
#define TA_CR	(*(volatile unsigned char *)0xfffffa19L)
#define TA_DR	(*(volatile unsigned char *)0xfffffa1fL)
#define TA_HZ	12288L
static long m_n, m_t;		/* frames counted, Timer A's counts (< 0: the 200 Hz clock's ms) */
int snd_ta_busy;		/* the last measure timed by the 200 Hz clock: Timer A in use */
static int m_by200;		/* 1: timed by the 200 Hz clock all the same (a second opinion, for the log) */

static long m_loop(void)
{
	long a, b, d, n = 0, t = 0, t0 = 0;
	unsigned char prev = 0, cur;
	int ta = !m_by200 && TA_CR == 0 && !(*(volatile unsigned char *)0xfffffa07L & 0x20);	/* (stopped, its interrupt off) */
	if (ta) {
		TA_DR = 0;		/* (256) */
		TA_CR = 7;		/* delay mode, / 200 */
		prev = TA_DR;
	} else
		t0 = (long)tos_ms();
	/* 20 ms first, then 200 counted */
	for (;;) {
		if (ta) {
			cur = TA_DR;
			t += (unsigned char)(prev - cur);
			prev = cur;
			if (t >= TA_HZ / 50)
				break;
		} else if ((long)tos_ms() - t0 >= 20)
			break;
	}
	t = 0;
	if (!ta)
		t0 = (long)tos_ms();
	a = play_pos();
	do {
		if (ta) {
			cur = TA_DR;
			t += (unsigned char)(prev - cur);
			prev = cur;
		} else
			t = (long)tos_ms() - t0;
		b = play_pos();
		if (a < 0) {
			a = b;
			continue;
		}
		if (b < 0)
			continue;
		d = b - a;
		if (d < 0)
			d += ring_n;
		if (d > ring_n / 2)	/* (a read a little behind: not a step) */
			continue;
		n += d;
		a = b;
	} while (ta ? t < TA_HZ / 5 : t < 200);
	if (ta)
		TA_CR = 0;
	m_n = n;
	m_t = ta ? t : -t;
	snd_ta_busy = !ta;
	return 0;
}

static int open_to(long rate, int ext, int feed);

static long measure(short pre)
{
	ext_pre = pre;
	if (!open_to(44100, 1, 2))	/* (one track, as 0.17 measured) */
		return 0;
	ring_n = 16384;
	memset(ring, 0, (size_t)ring_n * 2 * FW);
	x_setbuffer(0, ring, ring + snd_fw * ring_n);
	Buffoper(PLAY_ENABLE | PLAY_REPEAT);
	Supexec(m_loop);
	Buffoper(0);
	snd_close();
	/* the matrix as EPlay found it again: TOS 4.04 kept the measure's
	   (the DMA on the external clock, its record input on the DSP's
	   transmit with the handshake on), and the DSP's first start then
	   sent no frame for 2 s, until the watchdog's restart put it back
	   (the hub, 5 Nov 2026, on Erez's TOS; EmuTOS didn't show it) */
	Supexec(put_regs);
	if (m_t > 0)
		return (long)((long long)m_n * TA_HZ / m_t);
	return m_t < 0 ? m_n * 1000 / -m_t : 0;
}

long snd_ext_raw;		/* what the first measure found, Hz (for the log) */
long snd_ext_clock;		/* the clock that makes, Hz */
int snd_fdi;			/* the FDI's driver answers (Sndstatus $DD02: $4711) */
int snd_gpio_dir = -1, snd_gpio_data = -1;	/* the DSP port's GPIO ($FF8941, $FF8943) */
char snd_ext_note[96];		/* what measure_ext tried, for the log */

static long read_gpio(void)
{
	snd_gpio_dir = *(volatile unsigned char *)0xffff8941L & 7;
	snd_gpio_data = *(volatile unsigned char *)0xffff8943L & 7;
	return 0;
}

/* the external clock: measured at prescale 1 (rate = clock / 256 / 2),
   its frequency worked out from that, and the prescale that makes 44.1
   or 48 kHz of it chosen, then measured again to be sure. The FDI's
   clock may be 11.2896 MHz (256 x 44.1 kHz: prescale 0), 22.5792 (1) or
   45.1584 (3), and the same for 48 kHz: 0.1 knew only 22.5792 MHz.
   Hatari's runs at 50 kHz, a rate no music has: not taken */
static long measure_ext(void)
{
	static const long want[2] = { 44100, 48000 };
	long r = measure(1), clk;
	int i, k = 0;
	snd_ext_raw = r;
	snd_ext_clock = clk = r * 512;
	ext_pre = 1;
	snd_ext_note[0] = 0;
	if (r < 1000)
		return 0;
	for (i = 0; i < 2; i++) {
		long p = (clk + want[i] * 128) / (want[i] * 256) - 1, got, d;
		if (p < 0 || p > 15)
			continue;
		got = clk / 256 / (p + 1);
		d = got - want[i];
		if (d < 0)
			d = -d;
		if (d > want[i] / 40)		/* (the 200 Hz clock: 2.5 %) */
			continue;
		got = p == 1 ? r : measure((short)p);
		k += snprintf(snd_ext_note + k, sizeof(snd_ext_note) - (size_t)k, "%s%ld Hz at prescale %ld: %ld Hz",
			      k ? "; " : "", want[i], p, got);
		if (k >= (int)sizeof(snd_ext_note))
			k = (int)sizeof(snd_ext_note) - 1;
		d = got - want[i];
		if (d < 0)
			d = -d;
		if (d <= want[i] / 40) {
			ext_pre = (short)p;
			return want[i];
		}
	}
	return 0;
}

/* the FDI set to rate (44.1 or 48 kHz) as Cubase Audio does it: 4 on
   its lines, 50 ms, then 2 for 44.1 or 3 for 48 (Cubase's 8-track modes,
   its own clock), 5 or 7 (its 16-track modes' for the same: 0.30, as
   Erez's FDI gave 48 kHz at 2 and at 3 in every log from 0.4 to 0.29),
   the other rate's two (measured, in case its lines are the other way
   round), and 0 (Cubase's "S/PDIF controls the clock": the clock of what
   comes into its S/PDIF input), with time for it to settle. Each is
   measured, and the first at rate kept; the log has every reading. 1
   when the clock is at rate (snd_ext_rate, ext_pre set); else the lines
   as they were */
int snd_fdi_nosig;		/* the last try found no signal at the FDI's input to follow */
int snd_fdi_changed;		/* EPlay changed the FDI's lines (put back at the end) */
char snd_fdi_note[512];

static void wait_ms(long ms)
{
	unsigned long t0 = tos_ms();
	while ((long)(tos_ms() - t0) < ms)
		;
}

int snd_fdi_set(long rate)
{
	static const int v44[5] = { 2, 5, 3, 7, 0 }, v48[5] = { 3, 7, 2, 5, 0 };
	const int *v;
	int k, n = 0;
	snd_fdi_nosig = 0;
	if (!snd_fdi || (rate != 44100 && rate != 48000))
		return 0;
	if (snd_ext_rate == rate)
		return 1;
	v = rate == 44100 ? v44 : v48;
	snd_fdi_note[0] = 0;
	/* (Cubase Audio 2.06 resets the sound system first, Sndstatus(1),
	   each time it sets the FDI's rate or input: read from Erez's copy,
	   5 Nov 2026. The matrix is put back after each measure) */
	trap14_ww(140, 1);
	/* and then, before the lines, as its routine at $2ACE/$2E16 does:
	   16-bit stereo, four tracks, the monitor the first */
	Setmode(STEREO16);
	Settracks(TRACKS - 1, 0);
	Setmontracks(0);
	for (k = 0; k < 5; k++) {
		long got;
		int t;
		Gpio(0, 7);
		Gpio(2, 4);
		wait_ms(50);
		Gpio(2, v[k]);
		wait_ms(v[k] ? 50 : 200);	/* (the input's clock: its receiver to lock) */
		/* (Cubase's next: the DSP's transmit to the port and the DAC
		   on the external clock at prescale 1, through the FDI driver's
		   Devconnect; it measures nothing after 2 or 3) */
		Devconnect(DSPXMIT, DAC | EXTOUT, CLKEXT, 1, NO_SHAKE);
		snd_fdi_changed = 1;
		got = measure_ext();
		n += snprintf(snd_fdi_note + n, sizeof(snd_fdi_note) - (size_t)n, "%s%d: %ld Hz", n ? "; " : "",
			      v[k], snd_ext_raw);
		if (n >= (int)sizeof(snd_fdi_note))
			n = (int)sizeof(snd_fdi_note) - 1;
		/* the FDI's own clock off the rate asked: measured again 200 and
		   500 ms on, in case it takes longer to change than Cubase's
		   50 ms (Erez, 5 Nov 2026: Cubase changes it between 44.1 and 48;
		   EPlay read 48 kHz at 2 and at 3) */
		for (t = 0; k < 2 && got != rate && t < 2; t++) {
			wait_ms(t ? 300 : 150);
			got = measure_ext();
			n += snprintf(snd_fdi_note + n, sizeof(snd_fdi_note) - (size_t)n, ", %ld Hz at %d ms", snd_ext_raw,
				      t ? 500 : 200);
			if (n >= (int)sizeof(snd_fdi_note))
				n = (int)sizeof(snd_fdi_note) - 1;
		}
		/* the input's receiver may lock later than 200 ms: 0.10 measured
		   39,300 Hz there, 44.1 kHz with its first 25 ms missing; 0.9
		   44.1 kHz. Measured again, three times at most */
		if (!v[k]) {
			if (got != rate && snd_ext_raw > 1000) {
				/* the input's clock off its rate: timed by the 200 Hz
				   clock too, so that the log tells a clock at another
				   rate from a measure thrown off (0.25 and 0.26 read
				   38,593 Hz there, where 0.12 to 0.17 read 44.1 kHz;
				   the FDI's 48 kHz read 33,988 once at the start) */
				long r1 = snd_ext_raw;
				m_by200 = 1;
				measure_ext();
				m_by200 = 0;
				n += snprintf(snd_fdi_note + n, sizeof(snd_fdi_note) - (size_t)n,
					      " (by the 200 Hz clock %ld Hz)", snd_ext_raw);
				if (n >= (int)sizeof(snd_fdi_note))
					n = (int)sizeof(snd_fdi_note) - 1;
				snd_ext_raw = r1;
			}
			for (t = 0; t < 3 && got != rate && snd_ext_raw > 1000; t++) {
				wait_ms(100);
				got = measure_ext();
				n += snprintf(snd_fdi_note + n, sizeof(snd_fdi_note) - (size_t)n, ", %ld Hz",
					      snd_ext_raw);
				if (n >= (int)sizeof(snd_fdi_note))
					n = (int)sizeof(snd_fdi_note) - 1;
			}
			/* nothing at 44.1 or 48 kHz there: no signal it locks to
			   (Erez's 0.30: 39.3 kHz, its receiver free, until he
			   plugged the optical cable in again), told in the window */
			snd_fdi_nosig = !got;
		}
		snd_ext_rate = got;
		if (got == rate)
			return 1;
	}
	Gpio(2, snd_gpio_data);		/* (neither: as the FDI's program left it) */
	wait_ms(50);
	snd_ext_rate = measure_ext();
	return 0;
}

/* the MFP's interrupt set-up and its inputs, for the log: GPIP, AER,
   IERA, IERB, IMRA, IMRB (bit 3 of GPIP is the FDI's line its program
   reads; IERB bit 3 its interrupt) */
static unsigned char mfp_now[6];
static long mfp_read(void)
{
	static const unsigned char at[6] = { 0x01, 0x03, 0x07, 0x09, 0x13, 0x15 };
	int i;
	for (i = 0; i < 6; i++)
		mfp_now[i] = *(volatile unsigned char *)(0xfffffa00L + at[i]);
	return 0;
}

void snd_mfp_note(char *b, int n)
{
	Supexec(mfp_read);
	snprintf(b, (size_t)n, "GPIP $%02X, AER $%02X, IERA $%02X, IERB $%02X, IMRA $%02X, IMRB $%02X", mfp_now[0],
		 mfp_now[1], mfp_now[2], mfp_now[3], mfp_now[4], mfp_now[5]);
}

unsigned short snd_matrix[4];	/* $FF8930-$FF8937 after snd_dsp_out (the log) */
static long read_matrix(void)
{
	volatile unsigned short *r = (volatile unsigned short *)0xffff8930L;
	int i;
	for (i = 0; i < 4; i++)
		snd_matrix[i] = r[i];
	return 0;
}

/* the matrix: the DSP's SSI (DSPXMIT) to the FDI (EXTOUT), on the
   external clock, Cubase Audio's way; on the Falcon's clock (at the
   rate's prescale) to the DAC as well. The DAC on the FDI's clock played
   white noise on Erez's Falcon (0.9), and so did the Analog 8's channel 3;
   the DSP joined to the matrix (Dsptristate) */
void snd_dsp_out(long rate, int ext)
{
	short pre = 1;
	unsigned i;
	if (ext)
		pre = ext_pre;
	else
		for (i = 0; i < sizeof(internal) / sizeof(internal[0]); i++)
			if (internal[i] == rate)
				pre = prescale[i];
	Buffoper(0);
	/* one stereo pair out: the matrix's tracks as another program left
	   them (Cubase Audio leaves 4) put channels past 2 on the DSP port,
	   heard as a third channel at full scale on Erez's SoundPool Analog 8 */
	Settracks(0, 0);
	Setmontracks(0);
	Devconnect(DSPXMIT, ext ? EXTOUT : DAC | EXTOUT, ext ? CLKEXT : CLK25M, pre, NO_SHAKE);
	Dsptristate(1, 1);
	Supexec(read_matrix);
}

/* the DSP off the matrix, the matrix as EPlay found it */
void snd_dsp_off(void)
{
	Dsptristate(0, 0);
	Supexec(put_regs);
}

int snd_init(const char **why)
{
	long snd = get_cookie(0x5F534E44L);	/* _SND: bit 2 the codec, 4 the matrix */
	if ((snd & 0x14) != 0x14) {
		/* the TT's or the STE's 8-bit DMA (_SND bit 1) */
		if (!(snd & 2)) {
			*why = "EPlay needs DMA sound: a Falcon, a TT or an STE.";
			return 0;
		}
		snd_dma8 = 1;
		/* a 68020 or better interpolates (a TT); the 68000 takes the
		   nearest sample. A TT's DMA has no 6,258 Hz */
		snd_dma8_interp = get_cookie(0x5F435055L) >= 20;	/* '_CPU' */
		if ((get_cookie(0x5F4D4348L) >> 16) == 2)		/* '_MCH': a TT */
			nrates8 = 3;
		Supexec(keep_regs8);
		return 1;
	}
	if (Locksnd() != 1) {
		*why = "Another program is using the sound.";
		return 0;
	}
	locked = 1;
	Supexec(keep_regs);
	snd_fdi = trap14_ww(140, 0xDD02) == 0x4711;	/* Sndstatus: the FDI driver's sign */
	Supexec(read_gpio);
	snd_ext_rate = measure_ext();
	return 1;
}

void snd_exit(void)
{
	snd_close();
	if (snd_dma8) {
		Supexec(put_regs8);
		return;
	}
	if (snd_fdi_changed) {		/* the FDI's lines as its program left them */
		Gpio(0, snd_gpio_dir);
		Gpio(2, snd_gpio_data);
		snd_fdi_changed = 0;
	}
	if (locked) {
		Supexec(put_regs);
		Unlocksnd();
		locked = 0;
	}
}

/* Settings' Output (play.c sets them for each play): DSP leaves the
   external clock out, FDI takes it at any ratio the resampler reaches */
int snd_ext_use = 1, snd_ext_only;

long snd_rate_for(long rate, int *ext)
{
	long best = internal[0], d, bd = 1L << 30;
	unsigned i;
	*ext = 0;
	if (snd_dma8) {
		/* the lowest at or above the file's (it's resampled up to it),
		   else the highest: 44.1 and 48 kHz at 50,066, 22.05 at 25,033 */
		int j;
		best = rates8[0];
		for (j = 0; j < nrates8; j++)
			if (rates8[j] >= rate)
				best = rates8[j];
		return best;
	}
	if (snd_ext_use && snd_ext_rate && (rate - snd_ext_rate < snd_ext_rate / 100 && snd_ext_rate - rate < snd_ext_rate / 100)) {
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

/* the clock an MP3 plays on from the DSP when the FDI can't be put at its
   rate: the external clock as it is (the FDI's 44.1 or 48 kHz) when the
   DSP's resampler reaches it (x0.9 to x1.12), else the Falcon's lowest
   at or above the rate (49170 for 44.1 kHz: x1.115). The DSP resamples */
long snd_rate_dsp(long rate, int *ext)
{
	long best = internal[0];
	unsigned i;
	*ext = 0;
	if (snd_ext_use && snd_ext_rate &&
	    (snd_ext_only ? snd_ext_rate * 100 >= rate * 50 && snd_ext_rate * 100 <= rate * 200 :
			    snd_ext_rate * 100 >= rate * 90 && snd_ext_rate * 100 <= rate * 112)) {
		*ext = 1;
		return snd_ext_rate;
	}
	for (i = 0; i < sizeof(internal) / sizeof(internal[0]); i++)
		if (internal[i] >= rate)
			best = internal[i];
	return best;
}


int snd_open(long rate, int ext)
{
	return open_to(rate, ext, 0);
}

/* the ring to the DSP's receive (one track, through the matrix), at the
   DSP's output rate: a WAV or AIFF it resamples (XSTREAM), no host-port
   word a sample */
int snd_open_feed(long rate, int ext)
{
	return open_to(rate, ext, 1);
}

long snd_in_rate;		/* the file's rate, for the 8-bit DMA's resampling (play.c sets it) */

static int open8(long rate)
{
	int j;
	if (!ring8) {
		/* ST RAM: Mxalloc where GEMDOS has it (TOS 2 and on), else
		   Malloc, which an STE's TOS gives from ST RAM, all it has */
		unsigned char *p = Mxalloc(RING8_MAX * 2, 0);
		if ((long)p == -32)	/* (EINVFN: a GEMDOS without Mxalloc) */
			p = (unsigned char *)Malloc(RING8_MAX * 2);
		if ((long)p <= 0)
			return 0;
		ring8 = p;
	}
	mode8 = 3;
	for (j = 0; j < nrates8; j++)
		if (rates8[j] == rate)
			mode8 = (unsigned char)(3 - j);
	out8 = rate;
	in8 = snd_in_rate > 0 ? snd_in_rate : rate;
	/* 16.16, in 32 bits: the whole part, then the rest (< out8, so
	   << 16 stays under 2^32) */
	step8 = ((unsigned long)(in8 / out8) << 16) + (((unsigned long)(in8 % out8) << 16) / (unsigned long)out8);
	r8 = ((unsigned long)in8 << 12) / (unsigned long)out8;
	in_written = 0;
	ph8 = 1UL << 16;		/* (the first frame out: the file's first, whole) */
	frac8 = 0;
	skip8 = 0;
	h8l = h8r = 0;
	e8l = e8r = 0;
	ring_n = rate / 2;
	if (ring_n > RING8_MAX)
		ring_n = RING8_MAX;
	memset(ring8, 0, (size_t)ring_n * 2);
	start8 = ring8;
	end8 = ring8 + 2 * ring_n;
	Supexec(set8);
	snd_fw = 2;
	fw_shift = 1;
	wpos = written = played = 0;
	last_ppos = 0;
	playing = 0;
	snd_underruns = 0;
	snd_first_under = snd_least_lead = -1;
	return 1;
}

static int open_to(long rate, int ext, int feed)
{
	short pre = 1;
	unsigned i;
	if (snd_dma8)
		return feed ? 0 : open8(rate);
	if (!ring) {
		long n = RING_MAX;
		short *p = Mxalloc(n * 2 * FW, 0);
		if ((long)p <= 0)
			return 0;
		memset(p, 0, (size_t)n * 2 * FW);	/* (the three silent tracks: once) */
		ring = p;
		ring_n = n;
	}
	if (ext)
		pre = ext_pre;
	else
		for (i = 0; i < sizeof(internal) / sizeof(internal[0]); i++)
			if (internal[i] == rate)
				pre = prescale[i];
	Buffoper(0);
	/* (not cleared: the DMA starts once a quarter of a second is
	   written, and plays only what was) */
	/* the ring half a second long at this rate (the DMA's end is the ring's):
	   the queue in TT RAM holds the file ahead, the ring the tick's */
	ring_n = rate / 2;
	if (ring_n > RING_MAX)
		ring_n = RING_MAX;
	if (feed) {
		snd_fw = 2;
		fw_shift = 2;
		if (dirty < ring_n * 4)
			dirty = ring_n * 4;
		Settracks(0, 0);
		Setmontracks(0);
		Setmode(STEREO16);
		/* (2: an external clock measured, on one track to the DAC and
		   the port, as up to 0.18: with the four tracks, 0.25 read the
		   FDI's input clock at 44.1 kHz as 38,593 Hz, 7/8 of it, on
		   Erez's Falcon, where 0.12 to 0.17 read 44,089 to 44,135) */
		Devconnect(DMAPLAY, feed == 2 ? DAC | EXTOUT : DSPRECV, ext ? CLKEXT : CLK25M, pre, NO_SHAKE);
	} else {
		snd_fw = FW;
		fw_shift = 4;
		if (dirty) {
			memset(ring, 0, (size_t)dirty);	/* (the silent tracks again: a feed's
							   frames were 4 bytes) */
			dirty = 0;
		}
		Settracks(TRACKS - 1, 0);
		Setmontracks(0);	/* (the DAC: the first) */
		Setmode(STEREO16);
		Devconnect(DMAPLAY, DAC | EXTOUT, ext ? CLKEXT : CLK25M, pre, NO_SHAKE);
	}
	x_setbuffer(0, ring, ring + snd_fw * ring_n);
	wpos = written = played = 0;
	last_ppos = 0;
	playing = 0;
	snd_underruns = 0;
	snd_first_under = snd_least_lead = -1;
	return 1;
}

/* the Falcon's own output: att steps of 1.5 dB down (0..15), both
   channels; mute: the DAC's adder off (the matrix's sound no longer to
   it), on again as it was found. ($FF8938 and $FF893A: put back with the
   rest of the matrix at the end) */
#define LTATTEN 0
#define RTATTEN 1
#define ADDERIN 4
static short adder_found = -1;

void snd_dac_volume(int att, int mute)
{
	if (snd_dma8) {
		/* the STE (the 68000's way, no gain on the samples): its LMC1992;
		   EPlay's steps of 1.5 dB as its 2 dB, silent at the bottom. A TT
		   takes play.c's gain on the samples (snd_dma8_interp) */
		if (!snd_dma8_interp) {
			int d = mute ? 0 : 40 - (3 * att + 2) / 4;
			mw_cmd = (unsigned short)(0x4c0 | (d < 0 ? 0 : d));
			Supexec(mw_send);
		}
		return;
	}
	if (!locked)
		return;
	if (adder_found < 0) {
		adder_found = (short)(Soundcmd(ADDERIN, -1) & 3);
		if (!adder_found)
			adder_found = 2;		/* (the matrix) */
	}
	Soundcmd(LTATTEN, (att & 15) << 4);
	Soundcmd(RTATTEN, (att & 15) << 4);
	Soundcmd(ADDERIN, mute ? 0 : adder_found);
}

void snd_close(void)
{
	if (snd_dma8) {
		if (ring8)
			Supexec(stop8);
		playing = 0;
		return;
	}
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
	if (snd_dma8) {
		if (a < (long)ring8 || a > (long)(ring8 + 2 * ring_n))
			return -1;
		a = (a - (long)ring8) >> 1;
		return a >= ring_n ? a - ring_n : a;
	}
	if (a < (long)ring || a > (long)(ring + snd_fw * ring_n))
		return -1;
	a = (a - (long)ring) >> fw_shift;	/* (a frame's bytes; no division: the tick's) */
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
		if (!snd_underruns++)
			snd_first_under = played;
		written = played + d;
		wpos = p;
	}
	played += d;
	if (played > ring_n / 2) {		/* (the first second: the start's) */
		long lead = written - played;
		if (snd_least_lead < 0 || lead < snd_least_lead)
			snd_least_lead = lead;
	}
}

long snd_played(void)
{
	/* (the file's frames written, less the DMA's lead as the file's) */
	return snd_dma8 ? (long)in_written - as_in(written - played) : played;
}

long snd_written(void)
{
	return snd_dma8 ? (long)in_written : written;
}

long snd_room(void)
{
	long r = ring_n - (written - played) - 1024;	/* a guard behind the DMA */
	if (snd_dma8 && r > 0) {
		r = in_for(r) - 1;	/* (the file's frames that fill it, one spare for the step) */
		if (r > STAGE_N)
			r = STAGE_N;
	}
	return r > 0 ? r : 0;
}

short *snd_ptr(long *n)
{
	if (snd_dma8) {
		*n = STAGE_N;
		return stage;
	}
	*n = ring_n - wpos;
	return ring + snd_fw * wpos;
}

void snd_wrote(long n)
{
	if (snd_dma8) {
		long m = put8(n);
		in_written += (unsigned long)n;
		wpos += m;
		if (wpos >= ring_n)
			wpos -= ring_n;
		written += m;
		return;
	}
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
		long i;
		for (i = 0; i < k; i++, p += snd_fw, frames += 2) {
			p[0] = frames[0];
			p[1] = frames[1];
		}
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
		if (snd_dma8)
			Supexec(go8);
		else
			Buffoper(PLAY_ENABLE | PLAY_REPEAT);
		playing = 1;
	}
}

int snd_playing(void)
{
	return playing;
}
