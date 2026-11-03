/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * art.c - the album art (art.h), a job in steps after the file opens, so
 * that no stretch between GEM's events is long (the guidelines' Speed):
 *   1. the picture's bytes read from the file, 32 KB a step (6.5), into TT
 *      RAM where there is some; an unsynchronised tag's $FF $00 made $FF;
 *   2. decoded at the size shown (common's pic.c: TJpgDec, at 1/2, 1/4
 *      or 1/8 where it is that much bigger, a row at a time: pic_jpeg_rows;
 *      PNG and the JPEGs TJpgDec won't take whole), each row made into the
 *      screen's pens as it comes (Floyd-Steinberg against the palette as it
 *      is). GEM has a whole turn every 40 ms meanwhile (from the decoder's
 *      call for the next bytes, some 512 at a time): on a stock Falcon a
 *      500 x 500 cover of 48 KB took 6.8 s to unpack, and held GEM for 1.7 s
 *      at a time when pic.c's poll (every 16 rows) was its only turn;
 *   3. then made into a small form of the screen's own
 *      kind in ST RAM, a pen at a time (vrt_cpyfm of a 1-plane mask, as
 *      common's About box draws its icon), a few dozen pens a step; in
 *      true colour its pixels go in as they are.
 * From then on the art is one vro_cpyfm, cut to the redraw by hand (a
 * raster copy isn't clipped).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "art.h"
#include "play.h"
#include "pic.h"
#include "elog.h"

enum { A_NONE, A_READ, A_DECODE, A_PENS, A_READY, A_FAILED };

#define ART_MAX_BYTES (4L * 1024 * 1024)	/* a bigger picture is left out */
#define SLICE 40

static int st = A_NONE, tries;
static char path_now[256];
static unsigned char *buf;
static long blen, bgot;
static unsigned char *pens;
static unsigned char used[256];
static int next_pen;
static PIC_PAL pal;
static int pal_made;
static unsigned short *form;		/* ST RAM: the art in the screen's kind */
static short side, wpl;
static char note[120];
static unsigned long t_slice;
static int (*turn_fn)(void);
static int busy, abandon;		/* decoding (turns inside); give it up */
static char deferred[256];		/* art_begin's while busy */
static long fed;			/* the decoder's bytes so far */
static PIC_DITHER *dt;
static unsigned long t_read, t_dec, t_dith, t_pens, t_polls, t_maxgap, t_last;	/* (the log's) */

static unsigned long now_ms(void)
{
	return tos_ms();
}

/* how fast this 68030 is: a fixed loop's turns in 10 steps of the 200 Hz
   clock (50 ms). Unpacking a cover is TJpgDec's 512 bytes at a time, each
   70 to 100 ms on a stock Falcon and up to 70 ms on Hatari's 32 MHz one:
   over the 40 ms GEM may be kept waiting (the guidelines' Speed). The
   turns measured: Hatari's stock Falcon 1,342, its 32 MHz one 3,312 (its
   pieces up to 70 ms); under 40 ms wants some 5,800, past a DFB1 (about
   5,200 at 50 MHz): a 68060's Falcon (CT60). Elsewhere EPlay's own
   picture stays (the DSP's way, for every Falcon, is the next part) */
static long cpu_turns;
#define FAST_TURNS 5800L
static int cpu_fast;

static long spin(void)
{
	volatile long k = 0;
	unsigned long t0 = tos_hz200(), t;
	while ((t = tos_hz200()) == t0)
		;			/* (from a step's start) */
	t0 = t;
	while (tos_hz200() - t0 < 10)
		k++;
	return k;
}

void art_init(short size)
{
	long n;
	cpu_turns = spin();
	cpu_fast = FAST_TURNS > 0 && cpu_turns >= FAST_TURNS;
	elog("the 68030: %ld turns in 50 ms (the album art unpacked here from %ld)", cpu_turns, FAST_TURNS);
	side = size;
	wpl = (short)((size + 15) / 16);
	n = (long)wpl * 2 * size * (planes > 8 ? 16 : planes);
	if (!form) {
		void *p = Mxalloc(n, 0);	/* (at the start, before any queue: 0.13's lesson) */
		form = (long)p > 0 ? p : 0;
	}
}

static void drop(void)
{
	if (buf) {
		Mfree(buf);
		buf = 0;
	}
	if (pens) {
		free(pens);
		pens = 0;
	}
}

void art_begin(const char *path)
{
	if (busy) {
		/* (a turn inside the decode opened a file: it is given up, and
		   this one begun once it has returned) */
		snprintf(deferred, sizeof(deferred), "%s", path);
		abandon = 1;
		return;
	}
	if (!strcmp(path, path_now) && (st == A_READY || st == A_FAILED || st == A_NONE) && path_now[0])
		return;			/* (the same file: what it has) */
	drop();
	snprintf(path_now, sizeof(path_now), "%s", path);
	st = A_NONE;
	note[0] = 0;
	tries = 0;
	blen = play_tags.pic_end - play_tags.pic_at;
	if (!form || side <= 0 || play_tags.pic_end <= 0 || blen <= 0)
		return;
	if (!cpu_fast) {
		snprintf(note, sizeof(note), "album art of %ld KB: left for a faster 68030 (EPlay's picture shown)",
			 blen / 1024);
		elog("%s", note);
		st = A_FAILED;
		return;
	}
	if (blen > ART_MAX_BYTES) {
		snprintf(note, sizeof(note), "album art of %ld KB: too big, left out", blen / 1024);
		st = A_FAILED;
		return;
	}
	buf = Mxalloc(blen + 16, 3);
	if ((long)buf <= 0) {
		buf = 0;
		snprintf(note, sizeof(note), "album art of %ld KB: no memory for it", blen / 1024);
		st = A_FAILED;
		return;
	}
	bgot = 0;
	t_read = t_dec = t_dith = t_pens = t_polls = t_maxgap = 0;
	st = A_READ;
}

int art_pending(void)
{
	return st == A_READ || st == A_DECODE || st == A_PENS;
}

int art_ready(void)
{
	return st == A_READY;
}

const char *art_note(void)
{
	return note;
}

static void fail(const char *why)
{
	snprintf(note, sizeof(note), "album art (%ld KB): %s", blen / 1024, why);
	drop();
	st = A_FAILED;
}

/* (pic.c's, every 16 rows: GEM's turn after a slice) */
static int poll(void)
{
	unsigned long n = now_ms();
	t_polls++;
	if (n - t_last > t_maxgap)
		t_maxgap = n - t_last;
	t_last = n;
	if (n - t_slice < SLICE)
		return 1;
	if (!turn_fn || !turn_fn())
		return 0;
	t_slice = now_ms();
	return 1;
}

static void make_pal(void)
{
	static unsigned char rgb[256][3];
	short i, n = planes >= 8 ? 256 : (short)(1 << planes), c[3];
	for (i = 0; i < n; i++) {
		vq_color(vdi_h, i, c);
		rgb[i][0] = (unsigned char)(c[0] * 255L / 1000);
		rgb[i][1] = (unsigned char)(c[1] * 255L / 1000);
		rgb[i][2] = (unsigned char)(c[2] * 255L / 1000);
	}
	pic_pal_init(&pal, n, (const unsigned char (*)[3])rgb);
	pal_made = 1;
}

/* the decoder's next bytes (pic_jpeg_rows), and GEM's turn */
static long feed(void *u, unsigned char *b, long n)
{
	(void)u;
	if (now_ms() - t_slice >= SLICE) {
		unsigned long g = now_ms() - t_last;
		if (g > t_maxgap)
			t_maxgap = g;
		t_polls++;
		if (!turn_fn || !turn_fn())
			abandon = 1;
		t_slice = t_last = now_ms();
	}
	if (abandon)
		return 0;
	if (n > blen - fed)
		n = blen - fed;
	memcpy(b, buf + fed, (size_t)n);
	fed += n;
	return n;
}

/* a row as it comes: into its pens, or in true colour into the form */
static void row(void *u, long y, const unsigned short *px, const unsigned char *alpha)
{
	(void)u;
	if (y < 0 || y >= side)
		return;
	if (planes == 16)
		memcpy(form + y * wpl * 16, px, (size_t)side * 2);
	else
		pic_dither_row(dt, px, alpha, pens + y * side);
}

static void decode(void)
{
	PIC *p = 0;
	int why = 0, kind, rc;
	long i;
	unsigned long a = now_ms();
	if (play_tags.pic_unsync) {	/* $FF $00 is $FF */
		long o = 0;
		for (i = 0; i < blen; i++) {
			buf[o++] = buf[i];
			if (buf[i] == 0xff && i + 1 < blen && buf[i + 1] == 0)
				i++;
		}
		blen = o;
		play_tags.pic_unsync = 0;
	}
	kind = pic_kind(buf, blen);
	if (planes != 16) {
		if (!pal_made)
			make_pal();
		pens = malloc((size_t)side * side);
		dt = pens ? pic_dither_open(side, &pal) : 0;
		if (!dt) {
			fail("no memory to dither it");
			return;
		}
		memset(pens, 0, (size_t)side * side);
	}
	busy = 1;
	abandon = 0;
	fed = 0;
	t_last = now_ms();
	rc = kind == PIC_JPEG ? pic_jpeg_rows(feed, 0, side, side, row, 0, &why) : -1;
	if (rc < 0 && !abandon) {
		/* PNG, or a JPEG TJpgDec won't take: whole, pic.c's poll the turn */
		pic_poll = poll;
		p = pic_decode(buf, blen, side, side, 16000000L, &why);
		pic_poll = 0;
		if (p) {
			short y;
			for (y = 0; y < side; y++)
				row(0, y, p->px + (long)y * p->w, p->alpha ? p->alpha + (long)y * p->w : 0);
			pic_free(p);
			rc = 1;
		} else
			rc = 0;
	}
	busy = 0;
	t_dec += now_ms() - a;
	if (dt) {
		pic_dither_close(dt);
		dt = 0;
	}
	if (abandon || deferred[0]) {
		char next[256];
		snprintf(next, sizeof(next), "%s", deferred);
		deferred[0] = 0;
		abandon = 0;
		drop();
		st = A_NONE;
		path_now[0] = 0;
		if (next[0])
			art_begin(next);	/* (the file a turn opened) */
		return;			/* (else EPlay quits) */
	}
	if (rc != 1) {
		fail(pic_why_text(why));
		return;
	}
	Mfree(buf);
	buf = 0;
	snprintf(note, sizeof(note), "album art: %s, %ld KB, shown %dx%d", pic_kind_name(kind), (blen + 1023) / 1024,
		 side, side);
	if (planes == 16) {
		st = A_READY;
		return;
	}
	memset(used, 0, sizeof(used));
	for (i = 0; i < (long)side * side; i++)
		used[pens[i]] = 1;
	memset(form, 0, (size_t)wpl * 2 * side * planes);
	next_pen = 0;
	st = A_PENS;
}

/* pen p's pixels of the art blitted into the form, in that pen */
static void blit_pen(int p)
{
	unsigned short *m = st_ram((long)wpl * 2 * side);
	MFDB src, dst;
	short pxy[8], colors[2], y, x;
	if (!m)
		return;
	memset(m, 0, (size_t)wpl * 2 * side);
	for (y = 0; y < side; y++)
		for (x = 0; x < side; x++)
			if (pens[y * side + x] == p)
				m[y * wpl + (x >> 4)] |= 0x8000 >> (x & 15);
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = m;
	src.fd_w = (short)(wpl * 16);
	src.fd_h = side;
	src.fd_wdwidth = wpl;
	src.fd_nplanes = 1;
	dst.fd_addr = form;
	dst.fd_w = (short)(wpl * 16);
	dst.fd_h = side;
	dst.fd_wdwidth = wpl;
	dst.fd_nplanes = planes;
	pxy[0] = pxy[4] = 0;
	pxy[1] = pxy[5] = 0;
	pxy[2] = pxy[6] = side - 1;
	pxy[3] = pxy[7] = side - 1;
	colors[0] = (short)p;
	colors[1] = 0;
	vrt_cpyfm(vdi_h, 2, pxy, &src, &dst, colors);	/* transparent */
}

int art_step(int (*turn)(void))
{
	unsigned long t0 = now_ms();
	turn_fn = turn;
	t_slice = t0;
	while (art_pending() && now_ms() - t0 < SLICE) {
		if (st == A_READ) {
			long n = blen - bgot, r;
			if (n > 32768L)
				n = 32768L;
			{
				unsigned long a = now_ms();
				r = play_read_at(play_tags.pic_at + bgot, buf + bgot, n);
				t_read += now_ms() - a;
			}
			if (r <= 0) {
				fail("not read");
				break;
			}
			bgot += r;
			if (bgot >= blen)
				st = A_DECODE;
		} else if (st == A_DECODE) {
			decode();
			break;			/* (a step of its own) */
		} else if (st == A_PENS) {
			while (next_pen < 256 && !used[next_pen])
				next_pen++;
			if (next_pen >= 256) {
				free(pens);
				pens = 0;
				st = A_READY;
				break;
			}
			{
				unsigned long a = now_ms();
				blit_pen(next_pen++);
				t_pens += now_ms() - a;
			}
		}
	}
	if (st == A_READY || st == A_FAILED) {
		if (note[0])
			elog("%s; read %lu ms, decoded %lu ms (%lu polls, the longest between %lu ms), dithered %lu ms, "
			     "its pens %lu ms", note, t_read, t_dec, t_polls, t_maxgap, t_dith, t_pens);
		return st == A_READY;
	}
	return 0;
}

void art_draw(short x, short y, const GRECT *clip)
{
	GRECT r;
	MFDB src, dst;
	short pxy[8];
	if (st != A_READY)
		return;
	r.x = x;
	r.y = y;
	r.w = side;
	r.h = side;
	if (!rc_intersect(clip, &r))
		return;
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = form;
	src.fd_w = (short)(wpl * 16);
	src.fd_h = side;
	src.fd_wdwidth = wpl;
	src.fd_nplanes = planes > 8 ? 16 : planes;
	pxy[0] = r.x - x;
	pxy[1] = r.y - y;
	pxy[2] = pxy[0] + r.w - 1;
	pxy[3] = pxy[1] + r.h - 1;
	pxy[4] = r.x;
	pxy[5] = r.y;
	pxy[6] = r.x + r.w - 1;
	pxy[7] = r.y + r.h - 1;
	vro_cpyfm(vdi_h, 3, pxy, &src, &dst);	/* S_ONLY */
}
