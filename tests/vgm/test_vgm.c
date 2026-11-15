/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_vgm.c - src/vgm.c: a VGM file made here (an AY at the Spectrum's
 * 1,773,400 Hz, a loop, a GD3 tag), its header and tag read, its writes
 * at their times (the periods turned to 2 MHz, registers 14 and 15 left
 * out), the loop taken once, the fade, a seek; then the file cut short at
 * every length and changed at random, under AddressSanitizer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vgm.h"
#include "vgmcpu.h"
#include "oplexp.h"
#include "opltab.h"
#include <math.h>
#include "vgmfile.h"

static int bad;

static void same_n(const char *what, long got, long want)
{
	if (got != want) {
		printf("FAIL %s: %ld, wanted %ld\n", what, got, want);
		bad = 1;
	}
}

static void same_s(const char *what, const char *got, const char *want)
{
	if (strcmp(got, want)) {
		printf("FAIL %s: \"%s\", wanted \"%s\"\n", what, got, want);
		bad = 1;
	}
}

static unsigned char f[4096];
static long fn;

static void put32(long at, unsigned long v)
{
	f[at] = (unsigned char)v;
	f[at + 1] = (unsigned char)(v >> 8);
	f[at + 2] = (unsigned char)(v >> 16);
	f[at + 3] = (unsigned char)(v >> 24);
}

static void b(int c)
{
	f[fn++] = (unsigned char)c;
}

static void u16s(const char *s)
{
	for (; *s; s++) {
		b((unsigned char)*s);
		b(0);
	}
	b(0);
	b(0);
}

#define LOOP 441000L
static long loop_at, gd3_at;

/* the file: AY 0/1 = 0x100, 8 = 15, then a wait of 735; the loop: AY 2 =
   0x55, a write to register 14, a wait of 10 s, the end */
static void make(void)
{
	memset(f, 0, sizeof(f));
	memcpy(f, "Vgm ", 4);
	put32(0x08, 0x171);
	put32(0x34, 0x100 - 0x34);	/* (data at 0x100) */
	put32(0x74, 1773400);
	fn = 0x100;
	b(0xa0); b(0x00); b(0x00);
	b(0xa0); b(0x01); b(0x01);
	b(0xa0); b(0x08); b(0x0f);
	b(0x62);
	loop_at = fn;
	b(0xa0); b(0x02); b(0x55);
	b(0xa0); b(0x0e); b(0xff);
	for (int w = 0; w < 7; w++) {	/* 7 x 63000: 10 s */
		b(0x61); b(0x18); b(0xf6);
	}
	b(0x66);
	gd3_at = fn;
	memcpy(f + fn, "Gd3 ", 4);
	put32(fn + 4, 0x100);
	fn += 12;
	{
		long s = fn;
		u16s("Green Hill");
		u16s("");
		u16s("A Game");
		u16s("");
		u16s("Spectrum");
		u16s("");
		u16s("");
		u16s("Composer JP");
		u16s("2026");
		u16s("rig");
		u16s("");
		put32(gd3_at + 8, (unsigned long)(fn - s));
	}
	put32(0x04, (unsigned long)(fn - 4));
	put32(0x14, (unsigned long)(gd3_at - 0x14));
	put32(0x18, 735 + LOOP);
	put32(0x1c, (unsigned long)(loop_at - 0x1c));
	put32(0x20, LOOP);
}

static int log_reg[64], log_val[64], nlog;

static void ay(void *u, int reg, int val)
{
	(void)u;
	if (nlog < 64) {
		log_reg[nlog] = reg;
		log_val[nlog] = val;
		nlog++;
	}
}

static int opl_n, opl_last_reg, opl_last_val, opl_seen[512], opl_first;

void opl_log(void *u, int port, int reg, int val);
void opl_log(void *u, int port, int reg, int val)
{
	(void)u;
	if (!opl_n)
		opl_first = (port & 1) << 8 | (reg & 255);
	opl_n++;
	opl_last_reg = (port & 1) << 8 | (reg & 255);
	opl_last_val = val;
	opl_seen[(port & 1) << 8 | (reg & 255)] = val;
}

static int last(int reg)
{
	int i;
	for (i = nlog - 1; i >= 0; i--)
		if (log_reg[i] == reg)
			return log_val[i];
	return -1;
}

static long frd(void *u, long at, unsigned char *buf, long n)
{
	FILE *fp = u;
	if (fseek(fp, at, SEEK_SET))
		return -1;
	return (long)fread(buf, 1, (size_t)n, fp);
}

static void *al(long n)
{
	return malloc((size_t)n);
}

/* the file as a VGM and as a VGZ (gzip's) in dir: both read whole, the
   VGZ unpacked to the same bytes; the list's length and tag of each */
static void files(const char *dir)
{
	char p[300], cmd[700];
	const char *ext[2] = { "T.VGM", "T.VGZ" };
	FILE *fp;
	int k;
	snprintf(p, sizeof(p), "%s/T.VGM", dir);
	fp = fopen(p, "wb");
	fwrite(f, 1, (size_t)fn, fp);
	fclose(fp);
	snprintf(cmd, sizeof(cmd), "gzip -n -c %s/T.VGM > %s/T.VGZ", dir, dir);
	if (system(cmd)) {
		printf("FAIL gzip\n");
		bad = 1;
		return;
	}
	for (k = 0; k < 2; k++) {
		TAGS t;
		long size, n, ms;
		const char *why;
		unsigned char *b;
		snprintf(p, sizeof(p), "%s/%s", dir, ext[k]);
		fp = fopen(p, "rb");
		fseek(fp, 0, SEEK_END);
		size = ftell(fp);
		b = vgm_load(frd, fp, size, &n, al, free, &why);
		if (!b || n != fn || memcmp(b, f, (size_t)fn)) {
			printf("FAIL %s: loaded %ld bytes (%s)\n", ext[k], n, b ? "differ" : why);
			bad = 1;
		}
		free(b);
		ms = vgm_probe(frd, fp, size, &t, al, free);
		same_n(ext[k], ms, (735 + 2 * LOOP) * 1000 / VGM_RATE);
		same_s("its title", t.title, "Green Hill");
		same_s("its album", t.album, "A Game");
		fclose(fp);
	}
	/* a gzipped file cut short: damaged, never read past */
	snprintf(p, sizeof(p), "%s/T.VGZ", dir);
	fp = fopen(p, "rb");
	{
		long size, n;
		const char *why;
		fseek(fp, 0, SEEK_END);
		size = ftell(fp);
		for (long cut = 0; cut < size; cut += 7) {
			unsigned char *b = vgm_load(frd, fp, cut, &n, al, free, &why);
			free(b);
		}
	}
	fclose(fp);
}

int main(int argc, char **argv)
{
	VGMHEAD h;
	VGMRUN r;
	char t[64], al[64], ar[64];
	int i, k;
	long len;
	make();
	same_n("head", vgm_head(f, fn, &h), 1);
	same_n("data_at", h.data_at, 0x100);
	same_n("loop_at", h.loop_at, loop_at);
	same_n("ay_clock", (long)h.ay_clock, 1773400);
	same_n("other", h.other, 0);
	same_n("length", vgm_length(&h), 735 + 2 * LOOP);
	vgm_gd3(f, fn, &h, t, al, ar, sizeof(t));
	same_s("title", t, "Green Hill");
	same_s("album", al, "A Game");
	same_s("artist (the other's)", ar, "Composer JP");

	vgm_begin(&r, f, fn, &h, ay, 0, 0);
	nlog = 0;
	same_n("run 1", vgm_run(&r, 1), 1);
	/* 0x100 at 1.7734 MHz: 289 at 2 MHz (0x121) */
	same_n("reg 0", last(0), 0x21);
	same_n("reg 1", last(1), 0x01);
	same_n("reg 8", last(8), 15);
	same_n("reg 2 not yet", last(2), -1);
	same_n("run to 735", vgm_run(&r, 734), 1);
	same_n("reg 2 at 735", last(2), -1);
	vgm_run(&r, 1);
	same_n("reg 2 (0x55 -> 0x60)", last(2), 0x60);
	same_n("reg 14 left out", last(14), -1);
	/* the loop once more: reg 2 written again at 735 + LOOP */
	nlog = 0;
	vgm_run(&r, LOOP);
	same_n("the loop again", last(2), 0x60);
	/* the fade: 4 s at the end, the volume down */
	nlog = 0;
	same_n("near the end", vgm_run(&r, LOOP - VGM_RATE * 2), 1);
	k = last(8);
	if (k < 0 || k >= 15) {
		printf("FAIL fade: reg 8 %d\n", k);
		bad = 1;
	}
	same_n("the end", vgm_run(&r, VGM_RATE * 3), 0);

	/* a seek: into the loop, every register written as it is there */
	vgm_begin(&r, f, fn, &h, ay, 0, 0);
	nlog = 0;
	vgm_seek(&r, 1000);
	same_n("seek reg 2", last(2), 0x60);
	same_n("seek reg 0", last(0), 0x21);
	same_n("seek at", r.at, 1000);

	/* a 2 MHz AY: the periods as they are */
	put32(0x74, 2000000);
	vgm_head(f, fn, &h);
	vgm_begin(&r, f, fn, &h, ay, 0, 0);
	nlog = 0;
	vgm_run(&r, 1);
	same_n("2 MHz reg 0", last(0), 0x00);
	same_n("2 MHz reg 1", last(1), 0x01);

	/* the YM3812 (0x5A, its clock at 0x50): its writes passed on, kept,
	   and a seek writing them again, the keys last */
	{
		VGM_OPL keep = opl_log;
		memset(f, 0, sizeof(f));
		memcpy(f, "Vgm ", 4);
		put32(0x08, 0x151);
		put32(0x34, 0x100 - 0x34);
		put32(0x50, 3579545);
		fn = 0x100;
		b(0x5a); b(0x20); b(0x01);
		b(0x5a); b(0xa0); b(0x44);
		b(0x5a); b(0xb0); b(0x32);
		b(0x62);
		b(0x5a); b(0xbd); b(0x30);
		b(0x62);
		b(0x66);
		put32(0x04, (unsigned long)(fn - 4));
		put32(0x18, 1470);
		same_n("opl head", vgm_head(f, fn, &h), 1);
		same_n("opl clock", (long)h.ym3812_clock, 3579545);
		same_n("opl other", h.other, 0);
		vgm_begin(&r, f, fn, &h, 0, 0, 0);
		r.opl_fn = keep;
		opl_n = 0;
		vgm_run(&r, 1);
		same_n("opl writes", opl_n, 3);
		same_n("opl last", opl_last_reg * 256 + opl_last_val, 0xb032);
		vgm_run(&r, 735);
		same_n("opl drums", opl_last_reg * 256 + opl_last_val, 0xbd30);
		/* a seek past both: every register again, $BD's drums last */
		vgm_begin(&r, f, fn, &h, 0, 0, 0);
		r.opl_fn = keep;
		opl_n = 0;
		vgm_seek(&r, 1000);
		same_n("opl seek: the last", opl_last_reg * 256 + opl_last_val, 0xbd30);
		same_n("opl seek: $20", opl_seen[0x20], 0x01);
		same_n("opl seek: $A0", opl_seen[0xa0], 0x44);
		same_n("opl seek: $B0", opl_seen[0xb0], 0x32);
	}

	/* the YMF262 (0x5E, 0x5F: its two ports; its clock at 0x5C): passed on
	   with the port, the YM3812's (0x5A) left out in a file with both; a
	   seek writes NEW first, the second bank's keys; the scan sees the
	   second bank keyed */
	{
		VGM_OPL keep = opl_log;
		memset(f, 0, sizeof(f));
		memcpy(f, "Vgm ", 4);
		put32(0x08, 0x151);
		put32(0x34, 0x100 - 0x34);
		put32(0x50, 3579545);
		put32(0x5c, 14318180);
		fn = 0x100;
		b(0x5f); b(0x05); b(0x01);
		b(0x5e); b(0x20); b(0x01);
		b(0x5f); b(0x20); b(0x02);
		b(0x5a); b(0x40); b(0x3f);
		b(0x5f); b(0xc0); b(0x30);
		b(0x62);
		b(0x5f); b(0xa0); b(0x44);
		b(0x5f); b(0xb0); b(0x0a);	/* (no key) */
		b(0x62);
		b(0x5e); b(0xbd); b(0x20);
		b(0x62);
		b(0x66);
		put32(0x04, (unsigned long)(fn - 4));
		put32(0x18, 2205);
		same_n("opl3 head", vgm_head(f, fn, &h), 1);
		same_n("opl3 clock", (long)h.ymf262_clock, 14318180);
		same_n("opl3 other", h.other, 0);
		same_n("opl3 bank 2 not keyed", vgm_opl3_bank2(f, fn, &h), 0);
		vgm_begin(&r, f, fn, &h, 0, 0, 0);
		r.opl_fn = keep;
		opl_n = 0;
		memset(opl_seen, 0, sizeof(opl_seen));
		vgm_run(&r, 1);
		same_n("opl3 writes (0x5A left out)", opl_n, 4);
		same_n("opl3 port 1", opl_seen[0x120], 0x02);
		same_n("opl3 port 0", opl_seen[0x020], 0x01);
		same_n("opl3 0x5A out", opl_seen[0x040], 0);
		vgm_begin(&r, f, fn, &h, 0, 0, 0);
		r.opl_fn = keep;
		opl_n = 0;
		memset(opl_seen, 0, sizeof(opl_seen));
		vgm_seek(&r, 1500);
		same_n("opl3 seek: NEW first", opl_first, 0x105);
		same_n("opl3 seek: the last", opl_last_reg * 256 + opl_last_val, 0xbd20);
		same_n("opl3 seek: $1B0", opl_seen[0x1b0], 0x0a);
		same_n("opl3 seek: $1C0", opl_seen[0x1c0], 0x30);
		same_n("opl3 seek: $1A0", opl_seen[0x1a0], 0x44);
		f[0x100 + 21] = 0x2a;	/* (the second bank's B0: keyed) */
		same_n("opl3 bank 2 keyed", vgm_opl3_bank2(f, fn, &h), 1);
		f[0x100 + 20] = 0xb9;	/* (no channel) */
		same_n("opl3 B9 no channel", vgm_opl3_bank2(f, fn, &h), 0);
		f[0x100 + 20] = 0xb0;
		same_n("opl3 cut short", vgm_opl3_bank2(f, 0x100 + 21, &h), 0);
	}

	/* phase 11: the SN76489 made on the CPU (vgmcpu.c, sncpu.c): a tone of
	   period 112 at 3,579,545 Hz (998.8 Hz) at the loudest for a second,
	   then silent; made at 49,170 Hz, its pitch by the crossings, its
	   level sn.a56's (6,553 of 16 bits: 0.2 of full scale) */
	{
		static VGMCPU cpu;
		static short lr[2 * 70000];
		long got = 0, k, cross = 0, nz = 0;
		double sum = 0;
		memset(f, 0, sizeof(f));
		memcpy(f, "Vgm ", 4);
		put32(0x08, 0x151);
		put32(0x34, 0x100 - 0x34);
		put32(0x0c, 3579545);
		fn = 0x100;
		b(0x50); b(0x80); b(0x50); b(0x07);	/* tone 0: period 112 */
		b(0x50); b(0x90);			/* its volume: the loudest */
		b(0x61); b(44100 & 255); b(44100 >> 8);	/* a second */
		b(0x50); b(0x9f);			/* silent */
		b(0x61); b(4410 & 255); b(4410 >> 8);
		b(0x66);
		put32(0x04, (unsigned long)(fn - 4));
		put32(0x18, 44100 + 4410);
		same_n("cpu head", vgm_head(f, fn, &h), 1);
		vgm_begin(&r, f, fn, &h, 0, vgmcpu_sn, &cpu);
		vgmcpu_init(&cpu, &h, 49170, 0, 0, 0);
		for (;;) {
			int m = vgmcpu_render(&cpu, &r, lr + 2 * got, 1000);
			got += m;
			if (m < 1000 || got > 60000)
				break;
		}
		same_n("cpu frames (1.1 s at 49,170 Hz)", got > 54000 && got < 54200, 1);
		for (k = 2000; k < 49170; k++) {
			if ((lr[2 * k] >= 0) != (lr[2 * k - 2] >= 0))
				cross++;
			sum += (double)lr[2 * k] * lr[2 * k];
			if (lr[2 * k] != lr[2 * k + 1])
				nz++;
		}
		{
			double hz = cross / 2.0 * 49170 / (49170 - 2000), rms = sqrt(sum / (49170 - 2000));
			same_n("cpu tone (998.8 Hz, within 3)", fabs(hz - 998.8) < 3, 1);
			same_n("cpu level (sn.a56's 6,553)", fabs(rms - 6553) < 50, 1);
			same_n("cpu left and right the same", nz, 0);
		}
		for (k = 49170 + 200, nz = 0; k < got; k++)
			nz += lr[2 * k] != 0;
		same_n("cpu silent after", nz, 0);
	}

	/* the YM2612 made on the CPU (opncpu.c, through vgmcpu.c): algorithm
	   7's operator 4 alone at full level, F-number 1230, block 5 at the
	   NTSC clock (999.7 Hz), a second at 24,585 Hz, then keyed off */
	{
		static VGMCPU cpu;
		static OPNF tabs;
		static short lr[2 * 40000];
		static const unsigned char regs[][2] = {
			{ 0x22, 0 }, { 0x27, 0 }, { 0x2b, 0 }, { 0xb0, 0x07 }, { 0xb4, 0xc0 } };
		static const unsigned char slots[4][2] = { { 0x00, 127 }, { 0x04, 127 }, { 0x08, 127 }, { 0x0c, 0 } };
		long got = 0, k, cross = 0, nz = 0;
		double sum = 0;
		unsigned i;
		memset(f, 0, sizeof(f));
		memcpy(f, "Vgm ", 4);
		put32(0x08, 0x151);
		put32(0x34, 0x100 - 0x34);
		put32(0x2c, 7670453);
		fn = 0x100;
		for (i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
			b(0x52); b(regs[i][0]); b(regs[i][1]);
		}
		for (i = 0; i < 4; i++) {
			int sl = slots[i][0];
			b(0x52); b(0x30 + sl); b(0x01);
			b(0x52); b(0x40 + sl); b(slots[i][1]);
			b(0x52); b(0x50 + sl); b(0x1f);
			b(0x52); b(0x60 + sl); b(0);
			b(0x52); b(0x70 + sl); b(0);
			b(0x52); b(0x80 + sl); b(0x0f);
		}
		b(0x52); b(0xa4); b((5 << 3) | (1230 >> 8));
		b(0x52); b(0xa0); b(1230 & 255);
		b(0x52); b(0x28); b(0xf0);
		b(0x61); b(44100 & 255); b(44100 >> 8);
		b(0x52); b(0x28); b(0x00);		/* (keyed off) */
		b(0x61); b(8820 & 255); b(8820 >> 8);
		b(0x66);
		put32(0x04, (unsigned long)(fn - 4));
		put32(0x18, 44100 + 8820);
		same_n("opn cpu head", vgm_head(f, fn, &h), 1);
		opnf_init(&tabs);	/* (its tables: build/opntab.h's on the Atari) */
		vgm_begin(&r, f, fn, &h, 0, vgmcpu_sn, &cpu);
		vgmcpu_init(&cpu, &h, 24585, (const unsigned long *)tabs.sin_tab, (const unsigned long *)tabs.gain_tab, 0);
		vgmcpu_attach(&cpu, &r);
		for (;;) {
			int m = vgmcpu_render(&cpu, &r, lr + 2 * got, 1000);
			got += m;
			if (m < 1000 || got > 38000)
				break;
		}
		same_n("opn cpu frames (1.2 s at 24,585 Hz)", got > 29400 && got < 29600, 1);
		for (k = 2000; k < 24585; k++) {
			if ((lr[2 * k] >= 0) != (lr[2 * k - 2] >= 0))
				cross++;
			sum += (double)lr[2 * k] * lr[2 * k];
			nz += lr[2 * k] != lr[2 * k + 1];
		}
		{
			double hz = cross / 2.0 * 24585 / (24585 - 2000), rms = sqrt(sum / (24585 - 2000));
			same_n("opn cpu tone (999.7 Hz, within 3)", fabs(hz - 999.7) < 3, 1);
			same_n("opn cpu level (a channel at full: some 4,000 rms)", rms > 3000 && rms < 6000, 1);
			same_n("opn cpu left and right the same", nz, 0);
		}
		for (k = 24585 + 6000, nz = 0; k < got; k++)
			nz += lr[2 * k] != 0;
		same_n("opn cpu silent once released", nz, 0);
	}

	/* the YM3812 made on the CPU (oplcpu.c, through vgmcpu.c): channel 0's
	   carrier alone (its modulator silent), F-number 577, block 4 at the
	   chip's 3,579,545 Hz (437.7 Hz), a second at 24,585 Hz, then keyed
	   off; its tables the Atari's (build/opltab.h) */
	{
		static VGMCPU cpu;
		static short lr[2 * 40000];
		static const OPLC_TABS tabs = { opl_gain, opl_am, opl_pmw, opl_sinq, opl_rq, OPL_GAIN_N };
		static const unsigned char regs[][2] = {
			{ 0x01, 0x20 }, { 0x20, 0x01 }, { 0x23, 0x21 }, { 0x40, 0x3f }, { 0x43, 0x00 },
			{ 0x60, 0xf0 }, { 0x63, 0xf0 }, { 0x80, 0x0f }, { 0x83, 0x0f }, { 0xc0, 0x00 },
			{ 0xa0, 577 & 255 }, { 0xb0, 0x20 | 4 << 2 | 577 >> 8 } };
		long got = 0, k, cross = 0, nz = 0;
		double sum = 0;
		unsigned i;
		memset(f, 0, sizeof(f));
		memcpy(f, "Vgm ", 4);
		put32(0x08, 0x151);
		put32(0x34, 0x100 - 0x34);
		put32(0x50, 3579545);
		fn = 0x100;
		for (i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
			b(0x5a); b(regs[i][0]); b(regs[i][1]);
		}
		b(0x61); b(44100 & 255); b(44100 >> 8);
		b(0x5a); b(0xb0); b(4 << 2 | 577 >> 8);		/* (keyed off) */
		b(0x61); b(8820 & 255); b(8820 >> 8);
		b(0x66);
		put32(0x04, (unsigned long)(fn - 4));
		put32(0x18, 44100 + 8820);
		same_n("opl cpu head", vgm_head(f, fn, &h), 1);
		vgm_begin(&r, f, fn, &h, 0, vgmcpu_sn, &cpu);
		vgmcpu_init(&cpu, &h, 24585, 0, 0, &tabs);
		same_n("opl cpu: the OPL made, not the SN76489", cpu.has_opl && !cpu.has_opn && !cpu.has_sn, 1);
		vgmcpu_attach(&cpu, &r);
		for (;;) {
			int m = vgmcpu_render(&cpu, &r, lr + 2 * got, 1000);
			got += m;
			if (m < 1000 || got > 38000)
				break;
		}
		same_n("opl cpu frames (1.2 s at 24,585 Hz)", got > 29400 && got < 29600, 1);
		for (k = 2000; k < 24585; k++) {
			if ((lr[2 * k] >= 0) != (lr[2 * k - 2] >= 0))
				cross++;
			sum += (double)lr[2 * k] * lr[2 * k];
			nz += lr[2 * k] != lr[2 * k + 1];
		}
		{
			double hz = cross / 2.0 * 24585 / (24585 - 2000), rms = sqrt(sum / (24585 - 2000));
			same_n("opl cpu tone (437.7 Hz, within 3)", fabs(hz - 437.7) < 3, 1);
			same_n("opl cpu level (an operator at full: some 2,900 rms)", rms > 2000 && rms < 4000, 1);
			same_n("opl cpu left and right the same", nz, 0);
		}
		for (k = 24585 + 6000, nz = 0; k < got; k++)
			nz += lr[2 * k] != 0;
		same_n("opl cpu silent once released", nz, 0);
	}

	/* not a VGM */
	same_n("not one", vgm_head((const unsigned char *)"RIFF", 4, &h), 0);

	/* every length, and random changes: nothing read past the file */
	make();
	for (len = 0; len <= fn; len++) {
		unsigned char *c = malloc((size_t)len + 1);
		memcpy(c, f, (size_t)len);
		if (vgm_head(c, len, &h)) {
			vgm_gd3(c, len, &h, t, al, ar, sizeof(t));
			vgm_begin(&r, c, len, &h, ay, 0, 0);
			for (k = 0; k < 10 && vgm_run(&r, 300000); k++)
				;
			vgm_seek(&r, 500000);
		}
		free(c);
	}
	srand(7);
	for (i = 0; i < 20000; i++) {
		unsigned char *c = malloc((size_t)fn);
		memcpy(c, f, (size_t)fn);
		for (k = 0; k < 4; k++)
			c[rand() % fn] = (unsigned char)rand();
		if (vgm_head(c, fn, &h)) {
			vgm_gd3(c, fn, &h, t, al, ar, sizeof(t));
			vgm_begin(&r, c, fn, &h, ay, 0, 0);
			for (k = 0; k < 10 && vgm_run(&r, 30000); k++)
				;
		}
		free(c);
	}
	make();
	files(argc > 1 ? argv[1] : ".");
	printf(bad ? "test_vgm: FAILED\n" : "test_vgm: all passed\n");
	return bad;
}
