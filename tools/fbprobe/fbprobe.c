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
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "tos.h"

#pragma GCC diagnostic ignored "-Warray-bounds"	/* fixed addresses */

extern long fb_addr, fb_val;
extern short fb_size, fb_ok;
extern long fb_peek_s(void);

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
	say("FBPROBE 1 (EPlay's, for the FireBee): what the machine says about its sound");
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
