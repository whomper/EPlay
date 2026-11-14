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
