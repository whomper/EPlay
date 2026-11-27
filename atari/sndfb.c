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
 * A ring of 16-bit stereo frames in ST RAM (Mxalloc mode 0), 12 seconds
 * long (two until 1.61), played looping (Buffoper's repeat), as mxPlay's on the FireBee.
 * The tick has no interrupt here (feedfb.c): GEM's turns run it, and fill
 * the ring's room each time (all but the quarter behind where it plays,
 * snd_room), so that nine seconds pass before a turn held up (a menu held
 * down, the system's file selector) is heard. Where
 * it plays: the tick (in supervisor mode, by Supexec) calls no XBIOS (in
 * 1.31 to 1.34 its Buffptr from Timer A's interrupt rebooted the FireBee
 * at a GEM action, the tester, 13 Nov 2026), so it counts the frames by
 * the 200 Hz clock (_hz_200) at the rate, and the turn before it
 * (snd_poll) asks Buffptr and puts right what that has drifted. Always at 49,170 Hz: play.c
 * resamples a file to it on
 * the CPU. The volume through Soundcmd's attenuation, as the Falcon's.
 * What the Falcon's snd.c measures and sets beyond that (the external
 * clock, the FDI, the matrix, the 8-bit DMA) is answered as absent.
 */
#include <string.h>
#include "tos.h"
#include "snd.h"
#include "xalloc.h"

#ifdef SND_XB_IN
/* (EPLAY.PRG's: this file built again as sndxb.o for a machine with no
   Atari sound of its own but the XBIOS's sound calls, a Milan's GSXB
   (phase 11, step 4); its calls sx_*, snd.c's chosen at its start
   (snd_xb), the variables snd.c's) */
#include "sndxb.h"
#define snd_init sx_init
#define snd_take sx_take
#define snd_give sx_give
#define snd_exit sx_exit
#define snd_term sx_term
#define snd_rate_for sx_rate_for
#define snd_rate_dsp sx_rate_dsp
#define snd_fdi_set sx_fdi_set
#define snd_dsp_out sx_dsp_out
#define snd_dsp_off sx_dsp_off
#define snd_mfp_note sx_mfp_note
#define snd_put8_raw sx_put8_raw
#define snd_open sx_open
#define snd_open_feed sx_open_feed
#define snd_open_gms sx_open_gms
#define snd_dac_volume sx_dac_volume
#define snd_close sx_close
#define snd_ring_frames sx_ring_frames
#define snd_update sx_update
#define snd_poll sx_poll
#define snd_played sx_played
#define snd_written sx_written
#define snd_room sx_room
#define snd_ptr sx_ptr
#define snd_wrote sx_wrote
#define snd_write sx_write
#define snd_play sx_play
#define snd_playing sx_playing
#endif

#define OUT_RATE 49170L
#define PRESCALE 1		/* (Devconnect's: 49,170 Hz at the 25 MHz clock) */
/* (12 s, not 2: GEM's turns fill it, and a menu held down holds them, the
   ring then played round again, its old sound heard from 1.5 s on; 12 s,
   as a file is read ahead, a fault reported 27 Nov 2026. 2.4 MB of ST
   RAM; the volume is the codec's attenuation, so nothing waits on what is
   written ahead but a level past its 22.5 dB) */
#define RING_S 12
#define RING_MAX (49170L * RING_S)

#ifndef SND_XB_IN
int snd_adder_adc;
long snd_ext_rate;
int snd_dma8, snd_dma8_interp;
long snd_in_rate, snd_dma8_max;
int snd_ext_use, snd_ext_only;
int snd_ta_busy, snd_ta_stuck;
long snd_ext_raw, snd_ext_clock;
int snd_fdi;
int snd_gpio_dir = -1, snd_gpio_data = -1;
char snd_ext_note[96];
int snd_fdi_nosig;
char snd_fdi_note[512];
unsigned short snd_matrix[4];
int snd_fw = 2;
long snd_underruns, snd_first_under = -1, snd_least_lead = -1;
int snd_xb_want;
#endif

static short *ring;
static volatile long ring_n, wpos, written, played;
static int was_reset;		/* (Sndstatus's reset done, at the first open) */
static volatile int playing;
static int locked;
/* (the tick's count: the 200 Hz clock when it last ran, its remainder;
   where Buffptr said it plays, a frame of the ring, -1 for no word) */
static volatile unsigned long hz_last;
static volatile long hz_acc, hw_p = -1;
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
		*why = "EPlay can't play: the sound is in use by another program.";
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
	if (locked) {
		x_unlocksnd();	/* (a crash too: the sound not left locked) */
		locked = 0;
	}
}

#ifdef SND_XB_IN
/* (EPLAY.PRG's, the XBIOS's sound alone: a rate of the Falcon's own, as a
   VGM's chips are made at, played at it, Devconnect's prescale; any other
   at 49,170 Hz, resampled by play.c. The FireBee's edition keeps 49,170
   alone, what its tester's FireTOS was heard at) */
static const long xb_rates[8] = { 49170, 32780, 24585, 19668, 16390, 12292, 9834, 8195 };
static const short xb_pre[8] = { 1, 2, 3, 4, 5, 7, 9, 11 };
static long out_rate = OUT_RATE;
static short out_pre = PRESCALE;
#undef OUT_RATE
#define OUT_RATE out_rate
#undef PRESCALE
#define PRESCALE out_pre
#endif

long snd_rate_for(long rate, int *ext)
{
	*ext = 0;
#ifdef SND_XB_IN
	{
		int i;
		for (i = 0; i < 8; i++)
			if (rate == xb_rates[i])
				return rate;
	}
#endif
	(void)rate;
	return 49170L;
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
#ifdef SND_XB_IN
	{
		int i;
		out_rate = 49170L;
		out_pre = 1;
		for (i = 0; i < 8; i++)
			if (rate == xb_rates[i]) {
				out_rate = rate;
				out_pre = xb_pre[i];
			}
	}
#endif
	if (!ring) {
		short *p = x_alloc(RING_MAX * 4, 0);	/* (ST RAM: where the sound's DMA reads) */
		if ((long)p <= 0)
			return 0;
		ring = p;
	}
	ring_n = rate * RING_S;
	if (ring_n > RING_MAX)
		ring_n = RING_MAX;
	memset(ring, 0, (size_t)ring_n * 4);
	x_buffoper(0);
	/* (reset: as FBPROBE 2 began each; the first open's alone: a FireBee
	   tester heard "a little unnecessary beep at automatic transition"
	   from one file to the next, 17 Nov 2026, and an AC'97 codec's reset
	   is heard) */
	if (!was_reset) {
		x_sndstatus(1);
		was_reset = 1;
	}
	x_setmode(1);			/* 16-bit stereo */
	x_devconnect(0, 8, 0, PRESCALE, 1);	/* DMA play to the DAC, 25 MHz clock, no handshake */
	x_setbuffer(0, ring, ring + 2 * ring_n);
	snd_fw = 2;			/* (EPLAY.PRG's variable too: 16-bit stereo, no tracks) */
	wpos = written = played = 0;
	playing = 0;
	hw_p = -1;
	snd_underruns = 0;
	snd_first_under = snd_least_lead = -1;
	return 1;
}

int snd_open_gms(long rate, int ext)
{
	(void)rate;
	(void)ext;
	return 0;
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
	if (att > 15)
		att = 15;	/* (the codec's most, 22.5 dB: play.c puts the rest on the samples) */
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

/* the frames played: the tick's (supervisor mode, by Supexec: no XBIOS),
   from where Buffptr said the sound plays (snd_poll, just before, in the
   same GEM turn), the 200 Hz clock counting the rings it went round in a
   turn held up past a ring; with no word from Buffptr, the clock alone */
void snd_update(void)
{
	unsigned long h;
	long d, est;
	if (!playing)
		return;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"	/* (a system variable's fixed address) */
	h = HZ200;
#pragma GCC diagnostic pop
	hz_acc += (long)(h - hz_last) * OUT_RATE;
	hz_last = h;
	est = hz_acc / 200;
	hz_acc -= est * 200;
	if (hw_p >= 0) {
		d = hw_p - played % ring_n;
		if (d < 0)
			d += ring_n;
		if (d > ring_n - 512 && est < ring_n / 2)
			d = 0;		/* (a little behind: still the same place) */
		while (est - d > ring_n / 2)
			d += ring_n;	/* (a whole ring went by meanwhile) */
		hw_p = -1;
	} else
		d = est;
	if (d <= 0)
		return;
	if (played + d > written) {
		if (!snd_underruns++)
			snd_first_under = played;
		written = played + d;
		wpos = (played + d) % ring_n;	/* (where the DMA is now) */
	}
	played += d;
	if (played > ring_n / 4) {
		long lead = written - played;
		if (snd_least_lead < 0 || lead < snd_least_lead)
			snd_least_lead = lead;
	}
}

/* (GEM's turn, before the tick: feed_poll) where the DMA is, from Buffptr */
void snd_poll(void)
{
	hw_p = playing ? hw_pos() : -1;
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
	/* (a quarter of the ring kept behind where Buffptr says it plays: how
	   far ahead of that a FireBee's or a Milan's driver has already read
	   isn't known, and mxPlay keeps a whole second; the ring still holds
	   a second and a half ahead) */
	long r = ring_n - (written - played) - ring_n / 4;
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
		hw_p = -1;
		x_buffoper(3);		/* play, looping */
		playing = 1;
	}
}

int snd_playing(void)
{
	return playing;
}
