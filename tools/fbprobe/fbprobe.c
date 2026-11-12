/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fbprobe.c - FBPROBE.PRG, for the FireBee's tester (the hub, 10 Nov 2026:
 * what decides whether EPlay's FireBee edition is a small port or a new
 * audio driver). Plain 68000 code, no DSP, nothing set or started: it only
 * reads, and writes what it found to FBPROBE.TXT beside itself (and on
 * the screen):
 *	- the cookie jar, every cookie (_MCH, _CPU, _SND, _FPU, _VDO, ...);
 *	- the memory (Malloc(-1), Mxalloc(-1) for ST RAM and the other) and,
 *	  with a Falcon's video or after (_VDO), Vsetmode(-1);
 *	- the Falcon's XBIOS sound calls that only report: Locksnd and
 *	  Unlocksnd, Sndstatus(0), Soundcmd(n, -1) for each n (inquire),
 *	  Buffptr; Setbuffer, Devconnect and Buffoper are not called;
 *	- the MFP's Timer A (its control, data, enable and mask registers)
 *	  and the DMA sound's registers, each read with a bus-error guard;
 *	- a read at $FF8800 (the YM2149's), with the guard.
 * FBPROBE 2 (12 Nov 2026: whether EPlay can play through the XBIOS's sound
 * calls where there is no Falcon DMA, as FireTOS answered them): after the
 * reads, it plays. At each Falcon rate (49,170, 32,780, 24,585 and
 * 12,292 Hz) a 1 kHz tone of about a second, 16-bit stereo, through
 * Setmode, Devconnect (the DMA's playback to the DAC), Setbuffer and
 * Buffoper, Buffptr read half a second in to measure the rate played;
 * and MFP Timer A at about 1 kHz through Xbtimer, its interrupts counted
 * for a second (EPlay's tick). Quiet first (the tester is asked to listen).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "tos.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed addresses */

extern long fb_addr, fb_val;
extern short fb_size, fb_ok;
extern long fb_peek_s(void);
extern volatile long fb_ticks;
static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
extern void fb_tick(void);
extern long fb_hz200(void);

/* XBIOS calls with a word and two longs (Setbuffer) and three words and
   a long (Xbtimer), as tos.h's: the arguments in registers (an operand
   addressed by %sp would move as they are pushed) */
static long trap14_wwll(short fn, short a, long b, long c)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %4,-(%%sp)\n\tmove.l %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\t"
			 "trap #14\n\tlea 12(%%sp),%%sp"
			 : "=r"(r) : "r"(fn), "r"(a), "r"(b), "r"(c) : TRAP_CLOBBER);
	return r;
}

static long trap14_wwwwl(short fn, short a, short b, short c, long d)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %5,-(%%sp)\n\tmove.w %4,-(%%sp)\n\tmove.w %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\t"
			 "move.w %1,-(%%sp)\n\ttrap #14\n\tlea 12(%%sp),%%sp"
			 : "=r"(r) : "r"(fn), "r"(a), "r"(b), "r"(c), "r"(d) : TRAP_CLOBBER);
	return r;
}

static unsigned long hz200(void)
{
	Supexec(fb_hz200);
	return (unsigned long)fb_val;
}

static void wait_ticks(unsigned long n)
{
	unsigned long t0 = hz200();
	while (hz200() - t0 < n)
		;
}

/* a quarter of a sine, 0 to 90 degrees in 16 steps, out of 16000 */
static const short qsin[17] = { 0, 1569, 3122, 4642, 6122, 7547, 8889, 10148, 11314, 12364, 13289, 14075,
				14713, 15195, 15514, 15665, 16000 };

static short sine64(unsigned k)	/* k: 0..63, a whole period */
{
	k &= 63;
	if (k < 16)
		return qsin[k];
	if (k < 32)
		return qsin[32 - k];
	if (k < 48)
		return (short)-qsin[k - 32];
	return (short)-qsin[64 - k];
}

/* FBPROBE 2's part: plays */
static void play_tests(void)
{
	static const struct { short pre; long hz; } rt[] = { { 1, 49170 }, { 2, 32780 }, { 3, 24585 }, { 7, 12292 } };
	long bytes = 49170L * 4 * 6 / 5;	/* 1.2 s at the highest rate */
	short *buf;
	int i;
	say("%s", "");
	say("FBPROBE 2: the XBIOS's sound calls, played (please listen: four tones of about a second)");
	if (trap14_w(128) != 1) {	/* Locksnd */
		say("  Locksnd() refused: nothing played");
		return;
	}
	buf = (short *)Mxalloc(bytes, 0);	/* ST RAM, where a DMA can read */
	if ((long)buf <= 0) {
		say("  no %ld bytes of ST RAM: nothing played", bytes);
		trap14_w(129);
		return;
	}
	for (i = 0; i < (int)(sizeof(rt) / sizeof(rt[0])); i++) {
		long n = rt[i].hz * 6 / 5, k, p[4] = { 0, 0, 0, 0 };
		unsigned long step = 64000UL * 65536UL / (unsigned long)rt[i].hz, ph = 0;	/* (64 steps a period, 1 kHz) */
		long r1, r2, r3, r4, r5, r6, a, b;
		unsigned long t0, t1;
		for (k = 0; k < n; k++) {
			short v = (short)(sine64((unsigned)(ph >> 16)) / 4);	/* (-18 dB) */
			buf[2 * k] = buf[2 * k + 1] = v;
			ph += step;
		}
		r1 = trap14_ww(140, 1);					/* Sndstatus(1): reset */
		r2 = trap14_ww(132, 1);					/* Setmode(1): 16-bit stereo */
		r3 = trap14_w6(139, 0, 8, 0, rt[i].pre, 1, 0);		/* Devconnect(DMAPLAY, DAC, 25 MHz, pre, no handshake) */
		r4 = trap14_wwll(131, 0, (long)buf, (long)(buf + 2 * n));	/* Setbuffer(play, start, end) */
		r5 = trap14_ww(136, 1);					/* Buffoper(PLAY_ENABLE) */
		t0 = hz200();
		trap14_wl(141, (long)p);				/* Buffptr */
		a = p[0];
		wait_ticks(100);
		trap14_wl(141, (long)p);
		b = p[0];
		t1 = hz200();
		wait_ticks(160);
		r6 = trap14_ww(136, 0);					/* Buffoper(0): stopped */
		say("  %5ld Hz (prescale %d): Sndstatus(1) %ld, Setmode %ld, Devconnect %ld, Setbuffer %ld, "
		    "Buffoper %ld/%ld", rt[i].hz, rt[i].pre, r1, r2, r3, r4, r5, r6);
		if (b > a && t1 > t0)
			say("    Buffptr moved %ld bytes in %ld ms: %ld Hz played (start $%08lX, buffer $%08lX)", b - a,
			    (long)(t1 - t0) * 5, (b - a) / 4 * 200 / (long)(t1 - t0), a, (long)buf);
		else
			say("    Buffptr didn't move: $%08lX then $%08lX (the buffer at $%08lX)", a, b, (long)buf);
		wait_ticks(40);
	}
	Mfree(buf);
	say("  Unlocksnd()                %ld", trap14_w(129));

	say("%s", "");
	say("MFP Timer A at about 1 kHz (Xbtimer(0, 3, 154): 2.4576 MHz / 16 / 154 = 997.4 Hz), for a second:");
	fb_ticks = 0;
	{
		long r = trap14_wwwwl(31, 0, 3, 154, (long)fb_tick);	/* Xbtimer */
		unsigned long t0 = hz200(), t1;
		long got;
		wait_ticks(200);
		got = fb_ticks;
		t1 = hz200();
		trap14_ww(26, 13);					/* Jdisint(13): Timer A off */
		say("  Xbtimer %ld; %ld interrupts in %ld ms (997 wanted)", r, got, (long)(t1 - t0) * 5);
	}
}

static char out[8192];
static long n_out;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	char b[200];
	va_list ap;
	int k;
	va_start(ap, fmt);
	k = vsnprintf(b, sizeof(b) - 2, fmt, ap);
	va_end(ap);
	if (k < 0)
		return;
	if (k > (int)sizeof(b) - 3)
		k = (int)sizeof(b) - 3;
	b[k++] = '\r';
	b[k++] = '\n';
	b[k] = 0;
	Cconws(b);
	if (n_out + k < (long)sizeof(out)) {
		memcpy(out + n_out, b, (size_t)k);
		n_out += k;
	}
}

static void peek(const char *what, long addr, int size)
{
	fb_addr = addr;
	fb_size = (short)size;
	Supexec(fb_peek_s);
	if (fb_ok)
		say("  %-28s $%08lX: $%0*lX", what, addr, size * 2, fb_val);
	else
		say("  %-28s $%08lX: no answer (bus error)", what, addr);
}

static long jar_p;
static long read_jar(void)
{
	jar_p = *(volatile long *)0x5a0L;
	return 0;
}

int main(void)
{
	long *jar, r;
	int i;
	say("FBPROBE 2 (EPlay's, for the FireBee): what the machine says about its sound, then a play test");
	say("%s", "");
	Supexec(read_jar);
	jar = (long *)jar_p;
	say("The cookie jar:%s", jar ? "" : " none");
	for (i = 0; jar && jar[0] && i < 64; i++, jar += 2) {
		char id[5];
		int k;
		for (k = 0; k < 4; k++) {
			char c = (char)(jar[0] >> (24 - 8 * k));
			id[k] = c >= 32 && c < 127 ? c : '.';
		}
		id[4] = 0;
		say("  %s  $%08lX", id, jar[1]);
	}
	say("%s", "");
	say("The memory: Malloc(-1) %ld; Mxalloc(-1, 0) ST RAM %ld; Mxalloc(-1, 1) other RAM %ld",
	    (long)Malloc(-1L), (long)Mxalloc(-1L, 0), (long)Mxalloc(-1L, 1));
	if ((get_cookie(0x5F56444FL) >> 16) >= 3)	/* _VDO: a Falcon's video or after */
		say("Vsetmode(-1) (the video mode)  $%04lX", trap14_ww(88, -1) & 0xffffL);
	say("%s", "");
	say("The XBIOS's sound calls (inquiries only):");
	r = trap14_w(128);	/* Locksnd */
	say("  Locksnd()                  %ld%s", r, r == 128 ? " (the call isn't there: its own number back)" : "");
	if (r == 1) {
		say("  Sndstatus(0)               %ld", trap14_ww(140, 0));
		for (i = 0; i <= 6; i++)
			say("  Soundcmd(%d, -1)            %ld", i, trap14_w6(130, (short)i, -1, 0, 0, 0, 0));
		{
			long p[4] = { 0, 0, 0, 0 };
			r = trap14_wl(141, (long)p);
			say("  Buffptr()                  %ld: play $%08lX, record $%08lX", r, p[0], p[1]);
		}
		say("  Unlocksnd()                %ld", trap14_w(129));
	}
	say("%s", "");
	say("The registers (each read guarded):");
	peek("MFP GPIP", 0xfffffa01L, 1);
	peek("MFP IERA", 0xfffffa07L, 1);
	peek("MFP IMRA", 0xfffffa13L, 1);
	peek("MFP Timer A control (TACR)", 0xfffffa19L, 1);
	peek("MFP Timer A data (TADR)", 0xfffffa1fL, 1);
	peek("MFP vector register (VR)", 0xfffffa17L, 1);
	peek("YM2149 (the PSG) at $FF8800", 0xffff8800L, 1);
	peek("DMA sound control", 0xffff8900L, 2);
	peek("DMA sound mode", 0xffff8920L, 2);
	peek("Falcon matrix source", 0xffff8930L, 2);
	peek("Falcon codec ADC/DAC", 0xffff8938L, 2);
	peek("DSP host interface ISR", 0xffffa202L, 1);
	play_tests();
	say("%s", "");
	say("Please send FBPROBE.TXT back. Thank you.");
	{
		long h = Fcreate("FBPROBE.TXT", 0);
		if (h >= 0) {
			Fwrite((short)h, n_out, out);
			Fclose((short)h);
			Cconws("\r\nWritten to FBPROBE.TXT. Press a key.\r\n");
		} else
			Cconws("\r\nFBPROBE.TXT couldn't be written. Press a key.\r\n");
	}
	trap1_w(7);	/* Crawcin: a key, so the window stays to be read */
	return 0;
}
