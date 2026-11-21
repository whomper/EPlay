/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fbvgm.c - EPLAYFB.PRG's VGM chips made on the FireBee's ColdFire
 * (src/vgmcpu.c and the models under it: the SN76489, the YM2612, the
 * YM3812 and YMF262, the AY; Erez, 21 Nov 2026) run under qemu-m68k -cpu
 * cfv4e as a Linux program (common/tests/coldfire's runtime), or on the
 * host (HOST): a VGM rendered whole as vgmplay.c's vp_render takes it, at
 * the rate EPlay makes it at (49,170 Hz, the FM chips 24,585 Hz), the
 * samples written as 16-bit big-endian words to stdout. The models are
 * integer arithmetic, so the ColdFire's must be the host's, bit for bit.
 *
 *	fbvgm FILE.VGM > OUT.RAW
 */
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include "rt.h"
#endif
#include "vgm.h"
#include "vgmcpu.h"
#include "oplexp.h"
#include "opntab.h"
#include "opltab.h"

static unsigned char buf[4 << 20];
static VGMCPU cpu;
static VGMRUN run;
static short lr[2 * 1000];
static unsigned char outb[4 * 1000];

#ifdef HOST
static long read_file(const char *path, unsigned char *b, long cap)
{
	FILE *f = fopen(path, "rb");
	long n;
	if (!f)
		return -1;
	n = (long)fread(b, 1, (size_t)cap, f);
	fclose(f);
	return n;
}
#define put(p, n) fwrite((p), 1, (size_t)(n), stdout)
#else
#define put(p, n) sys_write(1, (p), (n))
#endif

int main(int argc, char **argv)
{
	static const OPLC_TABS tabs = { opl_gain, opl_am, opl_pmw, opl_sinq, opl_rq, OPL_GAIN_N };
	VGMHEAD h;
	long n, total = 0, rate;
	int m, k;
	if (argc < 2 || (n = read_file(argv[1], buf, sizeof(buf))) <= 0)
		return 2;
	if (!vgm_head(buf, n, &h))
		return 3;
	rate = h.ym2612_clock || h.ym3812_clock || h.ymf262_clock ? 24585 : 49170;
	vgm_begin(&run, buf, n, &h, vgmcpu_ay, vgmcpu_sn, &cpu);
	vgmcpu_init(&cpu, &h, rate, opn_sin, opn_gain, &tabs, 1);
	vgmcpu_attach(&cpu, &run);
	do {
		m = vgmcpu_render(&cpu, &run, lr, 1000);
		for (k = 0; k < 2 * m; k++) {
			outb[2 * k] = (unsigned char)((unsigned short)lr[k] >> 8);
			outb[2 * k + 1] = (unsigned char)lr[k];
		}
		if (m)
			put(outb, 4L * m);
		total += m;
	} while (m == 1000 && total < 30L * 49170);
	return total > 0 ? 0 : 1;
}
