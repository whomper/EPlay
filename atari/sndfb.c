/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sndfb.c - the FireBee's sound (EPLAYFB.PRG): snd.h's calls over the
 * XBIOS's sound calls, which FireTOS gives over the FireBee's AC'97 codec
 * (FBPROBE 2, the tester's FireBee, 13 Nov 2026: Locksnd, Sndstatus,
 * Setmode, Devconnect, Setbuffer and Buffoper answered, and Buffptr moved
 * through the buffer at 49,170, 32,780, 24,585 and 12,292 Hz). No register
 * of the Falcon's sound is there (they read $FF00 and $FFFF), so nothing is
 * read or written but through the calls.
 *
 * A ring of 16-bit stereo frames in ST RAM (Mxalloc mode 0), half a second
 * long, played looping (Buffoper's repeat). Where it plays: the tick (an
 * interrupt) may call no XBIOS (TOS isn't re-entrant: in 1.31 to 1.34 its
 * Buffptr there rebooted the FireBee at a GEM action under FireTOS, and at
 * once under MiNT, the tester, 13 Nov 2026), so the tick counts the frames
 * by the 200 Hz clock (_hz_200) at the rate, and GEM's turn (snd_poll) asks
 * Buffptr and puts right what that has drifted. Always at 49,170 Hz: play.c
 * resamples a file to it on
 * the CPU. The volume through Soundcmd's attenuation, as the Falcon's.
 * What the Falcon's snd.c measures and sets beyond that (the external
 * clock, the FDI, the matrix, the 8-bit DMA) is answered as absent.
 */
#include <string.h>
#include "tos.h"
#include "snd.h"

#define OUT_RATE 49170L
#define PRESCALE 1		/* (Devconnect's: 49,170 Hz at the 25 MHz clock) */
#define RING_MAX (OUT_RATE / 2)

int snd_adder_adc;
long snd_ext_rate;
int snd_dma8, snd_dma8_interp;
long snd_in_rate, snd_dma8_max;
int snd_ext_use, snd_ext_only;
int snd_ta_busy;
long snd_ext_raw, snd_ext_clock;
int snd_fdi;
int snd_gpio_dir = -1, snd_gpio_data = -1;
char snd_ext_note[96];
int snd_fdi_nosig;
char snd_fdi_note[512];
unsigned short snd_matrix[4];
int snd_fw = 2;
long snd_underruns, snd_first_under = -1, snd_least_lead = -1;

static short *ring;
static volatile long ring_n, wpos, written, played;
static volatile int playing;
static int locked;
/* (the tick's count: the 200 Hz clock when it last ran, its remainder;
   GEM's correction, frames, for the tick to take) */
static volatile unsigned long hz_last;
static volatile long hz_acc, corr;
static volatile int corr_new;
#define HZ200 (*(volatile unsigned long *)0x4baL)

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"	/* (a system variable's fixed address) */
static long hz_read(void)
{
	return (long)HZ200;	/* (supervisor mode: Supexec) */
}
#pragma GCC diagnostic pop

/* XBIOS: Setbuffer (131) a word and two longs; Soundcmd (130) three words;
   the arguments in registers (one addressed by %sp would move as they are
   pushed) */
static long xb_wwll(short fn, short a, long b, long c)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %4,-(%%sp)\n\tmove.l %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\t"
			 "trap #14\n\tlea 12(%%sp),%%sp"
			 : "=r"(r) : "r"(fn), "r"(a), "r"(b), "r"(c) : TRAP_CLOBBER);
	return r;
}

static long xb_www(short fn, short a, short b)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #14\n\taddq.l #6,%%sp"
			 : "=r"(r) : "r"(fn), "r"(a), "r"(b) : TRAP_CLOBBER);
	return r;
}

#define x_locksnd()		trap14_w(128)
#define x_unlocksnd()		trap14_w(129)
#define x_soundcmd(m, d)	xb_www(130, (m), (d))
#define x_setbuffer(r, b, e)	xb_wwll(131, (r), (long)(b), (long)(e))
#define x_setmode(m)		trap14_ww(132, (m))
#define x_buffoper(m)		trap14_ww(136, (m))
#define x_sndstatus(m)		trap14_ww(140, (m))
#define x_buffptr(p)		trap14_wl(141, (long)(p))
#define x_devconnect(s, d, c, p, h) trap14_w6(139, (s), (d), (c), (p), (h), 0)

int snd_init(const char **why)
{
	/* the XBIOS's sound there and free: Locksnd answers 1 */
	if (x_locksnd() != 1) {
		*why = "EPlay can't play: FireTOS's sound is in use by another program.";
		return 0;
	}
	x_unlocksnd();
	return 1;
}

int snd_take(void)
{
	if (locked)
		return 1;
	if (x_locksnd() != 1)
		return 0;
	locked = 1;
	return 1;
}

void snd_give(void)
{
	if (!locked)
		return;
	x_unlocksnd();
	locked = 0;
}

void snd_exit(void)
{
	snd_close();
	snd_give();
}

/* (supervisor mode, EPlay ending however it ends: term.c) the play off */
void snd_term(void)
{
	if (playing)
		x_buffoper(0);
	playing = 0;
}

long snd_rate_for(long rate, int *ext)
{
	(void)rate;
	*ext = 0;
	return OUT_RATE;
}

long snd_rate_dsp(long rate, int *ext)
{
	return snd_rate_for(rate, ext);
}

int snd_fdi_set(long rate)
{
	(void)rate;
	return 0;
}

void snd_dsp_out(long rate, int ext)
{
	(void)rate;
	(void)ext;
}

void snd_dsp_off(void)
{
}

void snd_mfp_note(char *b, int n)
{
	if (n > 0)
		b[0] = 0;
}

long snd_put8_raw(const unsigned char *src, long nin, int bpf, int ol, int orr, unsigned char x)
{
	(void)src; (void)bpf; (void)ol; (void)orr; (void)x;
	return nin;
}

int snd_open(long rate, int ext)
{
	(void)ext;
	if (!ring) {
		short *p = Mxalloc(RING_MAX * 4, 0);	/* (ST RAM: where the sound's DMA reads) */
		if ((long)p <= 0)
			return 0;
		ring = p;
	}
	ring_n = rate / 2;
	if (ring_n > RING_MAX)
		ring_n = RING_MAX;
	memset(ring, 0, (size_t)ring_n * 4);
	x_buffoper(0);
	x_sndstatus(1);			/* (reset: as FBPROBE 2 began each) */
	x_setmode(1);			/* 16-bit stereo */
	x_devconnect(0, 8, 0, PRESCALE, 1);	/* DMA play to the DAC, 25 MHz clock, no handshake */
	x_setbuffer(0, ring, ring + 2 * ring_n);
	wpos = written = played = 0;
	playing = 0;
	corr_new = 0;
	snd_underruns = 0;
	snd_first_under = snd_least_lead = -1;
	return 1;
}

int snd_open_feed(long rate, int ext)
{
	(void)rate;
	(void)ext;
	return 0;
}

void snd_dac_volume(int att, int mute)
{
	if (!locked)
		return;
	x_soundcmd(0, (short)(((mute ? 15 : att) & 15) << 4));	/* LTATTEN */
	x_soundcmd(1, (short)(((mute ? 15 : att) & 15) << 4));	/* RTATTEN */
}

void snd_close(void)
{
	if (ring && playing)
		x_buffoper(0);
	playing = 0;
}

long snd_ring_frames(void)
{
	return ring_n;
}

/* where it plays now, from Buffptr: -1 if it says nothing to trust */
static long hw_pos(void)
{
	long p[4] = { 0, 0, 0, 0 }, a;
	if (x_buffptr(p))
		return -1;
	a = p[0];
	if (a < (long)ring || a > (long)(ring + 2 * ring_n))
		return -1;
	a = (a - (long)ring) >> 2;
	return a >= ring_n ? a - ring_n : a;
}

/* the frames played: the tick's (an interrupt: no XBIOS), by the 200 Hz
   clock at the rate, with GEM's correction (snd_poll) */
void snd_update(void)
{
	unsigned long h;
	long d;
	if (!playing)
		return;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"	/* (a system variable's fixed address) */
	h = HZ200;
#pragma GCC diagnostic pop
	hz_acc += (long)(h - hz_last) * OUT_RATE;
	hz_last = h;
	d = hz_acc / 200;
	hz_acc -= d * 200;
	if (corr_new) {
		d += corr;
		corr_new = 0;
	}
	if (d <= 0) {
		played += d;		/* (a correction back: the count was ahead) */
		return;
	}
	if (played + d > written) {
		if (!snd_underruns++)
			snd_first_under = played;
		written = played + d;
		wpos = (played + d) % ring_n;	/* (where the DMA is now) */
	}
	played += d;
	if (played > ring_n / 2) {
		long lead = written - played;
		if (snd_least_lead < 0 || lead < snd_least_lead)
			snd_least_lead = lead;
	}
}

/* (GEM's turn, not the tick's) where the DMA is, from Buffptr: the tick's
   count put right when it is more than 512 frames off (the clock's 5 ms is
   245 of them) */
void snd_poll(void)
{
	long p, at, diff;
	if (!playing || corr_new)
		return;
	p = hw_pos();
	if (p < 0)
		return;
	at = played % ring_n;
	diff = p - at;
	if (diff > ring_n / 2)
		diff -= ring_n;
	else if (diff < -ring_n / 2)
		diff += ring_n;
	if (diff > 512 || diff < -512) {
		corr = diff;
		corr_new = 1;
	}
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
	long r = ring_n - (written - played) - 1024;
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
		long k, i;
		short *p = snd_ptr(&k);
		if (k > n)
			k = n;
		for (i = 0; i < k; i++, p += 2, frames += 2) {
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
		hz_last = (unsigned long)Supexec(hz_read);
		hz_acc = 0;
		corr_new = 0;
		x_buffoper(3);		/* play, looping */
		playing = 1;
	}
}

int snd_playing(void)
{
	return playing;
}
