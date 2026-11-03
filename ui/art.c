/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * art.c - the album art (art.h), a job in steps, so that no stretch
 * between GEM's events is long (the guidelines' Speed):
 *   1. the file's tags read (its own open: an MP3's art is made before
 *      play.c opens it), and the picture's length and first 4 KB made its
 *      key; a picture already in the cache is read from there (one record)
 *      and goes to 4;
 *   2. its bytes read, 32 KB a step (6.5), into TT RAM where there is some;
 *   3. unpacked at the size shown by common's pic.c on the DSP (dspjpeg:
 *      TJpgDec's headers, the scan on the DSP), GEM given its turn every
 *      40 ms; on a 68060's Falcon by the CPU (TJpgDec a row at a time). The
 *      DSP is taken for the picture alone and given back (the DSP rule); an
 *      MP3's music waits for it (Erez, 3 Nov 2026), a WAV's or AIFF's plays
 *      on the DMA meanwhile. The art made (5-6-5, its side square) goes into
 *      the cache: cache\art.pak, a record a picture, and cache\art.idx, the
 *      records' keys (the guidelines' 6.5: one pack, not a file each), so
 *      every track of an album with that cover finds it;
 *   4. made into the screen's pens (Floyd-Steinberg against the palette as
 *      it is, a few rows a step), then into a small form of the screen's own
 *      kind in ST RAM, a pen at a time (vrt_cpyfm of a 1-plane mask, as
 *      common's About box draws its icon); in true colour as it is.
 * From then on the art is one vro_cpyfm, cut to the redraw by hand (a
 * raster copy isn't clipped).
 *
 * Unpacked by a stock Falcon's 68030, a 500 x 500 cover of 48 KB took 6.8 s,
 * TJpgDec's 512 bytes at a time 70 to 100 ms each: over the 40 ms GEM may
 * be kept waiting. So the 68030 does it only where a 50 ms measure at the
 * start finds it some four times as fast (cpu_fast: a 68060's).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "art.h"
#include "tags.h"
#include "pic.h"
#include "dspjpeg.h"
#include "elog.h"

enum { A_NONE, A_READ, A_DECODE, A_DITHER, A_PENS, A_READY, A_FAILED };

#define ART_MAX_BYTES (4L * 1024 * 1024)	/* a bigger picture is left out */
#define SLICE 20	/* ms of work, then GEM's turn at the decoder's next poll (a poll comes every 8 MCUs, 15 to 25 ms: with 40 the turns came 60 to 70 ms apart) */
#define KEY_BYTES 4096
#define REC_SIDE 64				/* the cache's records: up to 64 x 64 */
#define REC_HDR 16
#define REC_SIZE (REC_HDR + 2L * REC_SIDE * REC_SIDE)
#define CACHE_MAX 4096				/* records, then the oldest is used again */

static int st = A_NONE, fh = -1, is_pcm, on_dsp;
static char path_now[256];
static TAGS tg;
static unsigned char *buf;
static long blen, bgot;
static unsigned long key;
static unsigned short *rgb;			/* the art, 5-6-5 */
static unsigned char *pens;
static unsigned char used[256];
static int next_pen, drow;
static PIC_PAL pal;
static int pal_made;
static PIC_DITHER *dt;
static unsigned short *form;			/* ST RAM: the art in the screen's kind */
static short side, wpl;
static char note[160];
static unsigned long t_slice, t_job;
static int (*turn_fn)(void);
static int busy, abandon;			/* unpacking (turns inside); give it up */
static char deferred[256];			/* art_begin's while busy */
static long fed;
static unsigned long t_dec0, t_feed1, t_gapmax;	/* (the log's: the unpacking's parts) */

static unsigned long now_ms(void)
{
	return tos_ms();
}

/* ---- how fast this 68030 is: a fixed loop's turns in 10 steps of the
   200 Hz clock (50 ms). Hatari's stock Falcon 1,342, its 32 MHz one 3,312
   (TJpgDec's pieces up to 70 ms there); under 40 ms wants some 5,800, past
   a DFB1 (about 5,200 at 50 MHz): a 68060's Falcon ---- */
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
	cpu_fast = cpu_turns >= FAST_TURNS;
	elog("the 68030: %ld turns in 50 ms (album art unpacked by it from %ld; else on the DSP%s)", cpu_turns,
	     FAST_TURNS, dspjpeg_present() ? "" : ", which this machine hasn't");
	side = size > REC_SIDE ? REC_SIDE : size;
	wpl = (short)((side + 15) / 16);
	n = (long)wpl * 2 * side * (planes > 8 ? 16 : planes);
	if (!form) {
		void *p = Mxalloc(n, 0);	/* (at the start, before any queue: 0.13's lesson) */
		form = (long)p > 0 ? p : 0;
	}
}

/* ---- the cache: cache\art.idx (EPLAYART, the records' count and the
   next to use, then a key and a length each) and cache\art.pak (records of
   REC_SIZE: ART1, the key, the length, the side, then the pixels) ---- */

typedef struct {
	unsigned long key, len;
} IENT;
static IENT *idx;
static long n_idx, next_slot;
static int idx_loaded;

static void cache_path(char *p, int n, const char *f)
{
	snprintf(p, (size_t)n, "%scache%s%s", app_dir(), f[0] ? "\\" : "", f);
}

static unsigned long be32(const unsigned char *b)
{
	return (unsigned long)b[0] << 24 | (unsigned long)b[1] << 16 | (unsigned long)b[2] << 8 | b[3];
}

static void put32(unsigned char *b, unsigned long v)
{
	b[0] = (unsigned char)(v >> 24);
	b[1] = (unsigned char)(v >> 16);
	b[2] = (unsigned char)(v >> 8);
	b[3] = (unsigned char)v;
}

static void idx_load(void)
{
	char p[220];
	unsigned char h[16];
	long h2, i;
	idx_loaded = 1;
	idx = calloc(CACHE_MAX, sizeof(IENT));
	if (!idx)
		return;
	cache_path(p, sizeof(p), "art.idx");
	h2 = Fopen(p, 0);
	if (h2 < 0)
		return;
	if (Fread((short)h2, 16, h) == 16 && !memcmp(h, "EPLAYART", 8)) {
		n_idx = (long)be32(h + 8);
		next_slot = (long)be32(h + 12);
		if (n_idx > CACHE_MAX || next_slot >= CACHE_MAX || next_slot > n_idx)
			n_idx = next_slot = 0;
		for (i = 0; i < n_idx; i++) {
			unsigned char e[8];
			if (Fread((short)h2, 8, e) != 8) {
				n_idx = next_slot = i;
				break;
			}
			idx[i].key = be32(e);
			idx[i].len = be32(e + 4);
		}
	}
	Fclose((short)h2);
}

static long cache_find(void)
{
	long i;
	if (!idx_loaded)
		idx_load();
	if (!idx)
		return -1;
	for (i = 0; i < n_idx; i++)
		if (idx[i].key == key && idx[i].len == (unsigned long)blen)
			return i;
	return -1;
}

static int cache_read(long slot)
{
	char p[220];
	unsigned char h[REC_HDR];
	long h2 = 0, n = 2L * side * side;
	int ok = 0;
	cache_path(p, sizeof(p), "art.pak");
	h2 = Fopen(p, 0);
	if (h2 < 0)
		return 0;
	if (Fseek(slot * REC_SIZE, (short)h2, 0) == slot * REC_SIZE && Fread((short)h2, REC_HDR, h) == REC_HDR &&
	    !memcmp(h, "ART1", 4) && be32(h + 4) == key && be32(h + 8) == (unsigned long)blen &&
	    (h[12] << 8 | h[13]) == side && Fread((short)h2, n, rgb) == n)
		ok = 1;
	Fclose((short)h2);
	return ok;
}

static void cache_store(void)
{
	char p[220];
	unsigned char h[REC_HDR];
	long h2, slot, n = 2L * side * side;
	if (!idx_loaded)
		idx_load();
	if (!idx)
		return;
	cache_path(p, sizeof(p), "");
	Dcreate(p);				/* (there already: an error, nothing more) */
	slot = next_slot;
	cache_path(p, sizeof(p), "art.pak");
	h2 = Fopen(p, 2);
	if (h2 < 0 && slot == 0)
		h2 = Fcreate(p, 0);
	if (h2 < 0)
		return;
	memset(h, 0, sizeof(h));
	memcpy(h, "ART1", 4);
	put32(h + 4, key);
	put32(h + 8, (unsigned long)blen);
	h[12] = (unsigned char)(side >> 8);
	h[13] = (unsigned char)side;
	/* (a record at the file's end or within it: TOS can't seek past it) */
	if (Fseek(slot * REC_SIZE, (short)h2, 0) != slot * REC_SIZE || Fwrite((short)h2, REC_HDR, h) != REC_HDR ||
	    Fwrite((short)h2, n, rgb) != n) {
		Fclose((short)h2);
		return;
	}
	if (n < REC_SIZE - REC_HDR) {
		static unsigned char zero[256];
		long left = REC_SIZE - REC_HDR - n;
		while (left > 0) {
			long k = left > (long)sizeof(zero) ? (long)sizeof(zero) : left;
			if (Fwrite((short)h2, k, zero) != k)
				break;
			left -= k;
		}
	}
	Fclose((short)h2);
	idx[slot].key = key;
	idx[slot].len = (unsigned long)blen;
	if (slot >= n_idx)
		n_idx = slot + 1;
	next_slot = (slot + 1) % CACHE_MAX;
	cache_path(p, sizeof(p), "art.idx");
	h2 = Fopen(p, 2);
	if (h2 < 0)
		h2 = Fcreate(p, 0);
	if (h2 < 0)
		return;
	{
		unsigned char hh[16], e[8];
		memcpy(hh, "EPLAYART", 8);
		put32(hh + 8, (unsigned long)n_idx);
		put32(hh + 12, (unsigned long)next_slot);
		put32(e, key);
		put32(e + 4, (unsigned long)blen);
		if (Fwrite((short)h2, 16, hh) == 16 && Fseek(16 + slot * 8, (short)h2, 0) == 16 + slot * 8)
			Fwrite((short)h2, 8, e);
	}
	Fclose((short)h2);
}

/* ---- the job ---- */

static void drop(void)
{
	if (fh >= 0) {
		Fclose((short)fh);
		fh = -1;
	}
	if (buf) {
		Mfree(buf);
		buf = 0;
	}
	if (pens) {
		free(pens);
		pens = 0;
	}
	if (dt) {
		pic_dither_close(dt);
		dt = 0;
	}
}

static long rd(void *u, long at, unsigned char *b, long n)
{
	(void)u;
	if (Fseek(at, (short)fh, 0) != at)
		return 0;
	return Fread((short)fh, n, b);
}

static void fail(const char *why)
{
	snprintf(note, sizeof(note), "album art (%ld KB): %s", blen / 1024, why);
	drop();
	st = A_FAILED;
}

int art_begin(const char *path)
{
	unsigned char head[12];
	long size, k, i, slot;
	if (path_now[0] && strcmp(path, path_now) && (busy || art_pending())) {
		/* another file while a picture is made: that job ends first, and
		   this one is begun after it (a cover is never made twice at
		   once: the hub, 3 Nov 2026; the same album's comes from the
		   cache then). Its music waits for that (ART_FIRST: an MP3's
		   picture may want the DSP; a WAV's plays once it is given back) */
		snprintf(deferred, sizeof(deferred), "%s", path);
		return ART_FIRST;
	}
	if (path_now[0] && !strcmp(path, path_now))	/* (the same file: what it has, or is making) */
		return art_needs_dsp() ? ART_FIRST : ART_NOW;
	drop();
	snprintf(path_now, sizeof(path_now), "%s", path);
	st = A_NONE;
	note[0] = 0;
	t_job = now_ms();
	if (!rgb)
		rgb = malloc((size_t)REC_SIDE * REC_SIDE * 2);
	if (!form || side <= 0 || !rgb)
		return ART_NOW;
	fh = (int)Fopen(path, 0);
	if (fh < 0) {
		fh = -1;
		return ART_NOW;
	}
	size = Fseek(0, (short)fh, 2);
	is_pcm = rd(0, 0, head, 12) == 12 && ((!memcmp(head, "RIFF", 4) && !memcmp(head + 8, "WAVE", 4)) ||
					   (!memcmp(head, "FORM", 4) && !memcmp(head + 8, "AIF", 3)));
	tags_read(&tg, rd, 0, size);
	blen = tg.pic_end - tg.pic_at;
	if (tg.pic_end <= 0 || blen <= 0) {
		drop();
		return ART_NOW;
	}
	if (blen > ART_MAX_BYTES) {
		fail("too big, left out");
		elog("%s", note);
		return ART_NOW;
	}
	/* the key: the length and the first 4 KB (FNV-1a) */
	k = blen < KEY_BYTES ? blen : KEY_BYTES;
	buf = Mxalloc(blen + 16, 3);
	if ((long)buf <= 0) {
		buf = 0;
		fail("no memory for it");
		elog("%s", note);
		return ART_NOW;
	}
	if (rd(0, tg.pic_at, buf, k) != k) {
		fail("not read");
		elog("%s", note);
		return ART_NOW;
	}
	key = 2166136261UL ^ (unsigned long)blen;
	for (i = 0; i < k; i++)
		key = (key ^ buf[i]) * 16777619UL;
	bgot = k;
	slot = cache_find();
	if (slot >= 0 && cache_read(slot)) {
		snprintf(note, sizeof(note), "album art (%ld KB): from the cache", blen / 1024);
		Fclose((short)fh);
		fh = -1;
		Mfree(buf);
		buf = 0;
		st = A_DITHER;
		return ART_NOW;
	}
	on_dsp = !cpu_fast;
	if (on_dsp && !dspjpeg_present()) {
		fail("no DSP here to unpack it, and the 68030 too slow");
		elog("%s", note);
		return ART_NOW;
	}
	st = bgot >= blen ? A_DECODE : A_READ;
	if (st == A_DECODE) {
		Fclose((short)fh);
		fh = -1;
	}
	/* an MP3's music waits for the DSP; a WAV's or AIFF's plays on the DMA */
	return on_dsp && !is_pcm ? ART_FIRST : ART_NOW;
}

int art_pending(void)
{
	return st == A_READ || st == A_DECODE || st == A_DITHER || st == A_PENS;
}

int art_needs_dsp(void)
{
	return on_dsp && !is_pcm && (st == A_READ || st == A_DECODE);
}

int art_ready(void)
{
	return st == A_READY;
}

/* (pic.c's, every few rows: GEM's turn after a slice) */
static int poll(void)
{
	unsigned long g = now_ms() - t_slice;
	if (g > t_gapmax)
		t_gapmax = g;
	if (!t_feed1)
		t_feed1 = now_ms();
	if (now_ms() - t_slice < SLICE)
		return 1;
	if (!turn_fn || !turn_fn())
		abandon = 1;
	t_slice = now_ms();
	return !abandon;
}

/* the decoder's next bytes (pic_jpeg_rows), and GEM's turn */
static long feed(void *u, unsigned char *b, long n)
{
	(void)u;
	if (!poll())
		return 0;
	if (n > blen - fed)
		n = blen - fed;
	memcpy(b, buf + fed, (size_t)n);
	fed += n;
	return n;
}

static void row(void *u, long y, const unsigned short *px, const unsigned char *alpha)
{
	(void)u;
	(void)alpha;
	if (y >= 0 && y < side)
		memcpy(rgb + y * side, px, (size_t)side * 2);
}

/* ---- the DSP's picture, rw x rh, averaged down to side x side. k = 1 is
   declined by pic.c on purpose (jpeg.a56 has no 1/8 path: the hub, 3 Nov
   2026); ask for k = 2 and average. (At 1/8 TJpgDec on the 68030 took a
   stock Falcon 6.8 s for a 500 x 500 cover: its Huffman codes are the
   cost, not the transform; the DSP at 1/4 0.9 s) ---- */
static long rw, rh, oy_at;
static unsigned long *acc;		/* side x 3 sums, and how many */
static unsigned long acc_n[REC_SIDE];
static long acc_rows;

static void row_big(void *u, long y, const unsigned short *px, const unsigned char *alpha)
{
	long oy = y * side / rh, x;
	(void)u;
	(void)alpha;
	if (oy != oy_at) {
		/* the output row before is whole */
		long i;
		if (oy_at >= 0 && oy_at < side && acc_rows > 0)
			for (i = 0; i < side; i++) {
				unsigned long n = acc_n[i] * (unsigned long)acc_rows;
				unsigned long r = (acc[3 * i] + n / 2) / n, g = (acc[3 * i + 1] + n / 2) / n,
					      b = (acc[3 * i + 2] + n / 2) / n;
				rgb[oy_at * side + i] = (unsigned short)(r << 11 | g << 5 | b);
			}
		memset(acc, 0, (size_t)side * 3 * sizeof(*acc));
		acc_rows = 0;
		oy_at = oy;
	}
	if (oy >= side)
		return;
	memset(acc_n, 0, sizeof(acc_n));
	for (x = 0; x < rw; x++) {
		long ox = x * side / rw;
		unsigned short v = px[x];
		acc[3 * ox] += v >> 11;
		acc[3 * ox + 1] += (v >> 5) & 63;
		acc[3 * ox + 2] += v & 31;
		acc_n[ox]++;
	}
	acc_rows++;
}

/* the DSP's port, counted (the log: whether the scan went to the DSP) */
static PIC_DSP port;
static long n_put16, n_get;
static void c_put16(const unsigned short *w, long n)
{
	n_put16 += n;
	dspjpeg_port_jpeg.put16(w, n);
}
static unsigned long c_get16(unsigned short *d, int cols, int rows, long pitch)
{
	n_get++;
	return dspjpeg_port_jpeg.get16(d, cols, rows, pitch);
}

static void make_pal(void)
{
	static unsigned char c3[256][3];
	short i, n = planes >= 8 ? 256 : (short)(1 << planes), c[3];
	for (i = 0; i < n; i++) {
		vq_color(vdi_h, i, c);
		c3[i][0] = (unsigned char)(c[0] * 255L / 1000);
		c3[i][1] = (unsigned char)(c[1] * 255L / 1000);
		c3[i][2] = (unsigned char)(c[2] * 255L / 1000);
	}
	pic_pal_init(&pal, n, (const unsigned char (*)[3])c3);
	pal_made = 1;
}

static void decode(void)
{
	PIC *p = 0;
	int why = 0, kind, rc;
	long i;
	if (tg.pic_unsync) {		/* $FF $00 is $FF */
		long o = 0;
		for (i = 0; i < blen; i++) {
			buf[o++] = buf[i];
			if (buf[i] == 0xff && i + 1 < blen && buf[i + 1] == 0)
				i++;
		}
		blen = o;
	}
	if (on_dsp) {
		/* the DSP free? (taken by another program, or by EPlay's own
		   music: then the 68030 would do it, too slowly for GEM) */
		if (trap14_w(104) == -1) {	/* Dsp_Lock */
			fail("the DSP is in use, so it is left out");
			return;
		}
		trap14_w(105);			/* Dsp_Unlock: dspjpeg takes it */
	}
	kind = pic_kind(buf, blen);
	t_dec0 = now_ms();
	t_feed1 = t_gapmax = 0;
	t_slice = t_dec0;
	port = dspjpeg_port_jpeg;
	port.put16 = c_put16;
	port.get16 = c_get16;
	n_put16 = n_get = 0;
	pic_dsp = on_dsp ? &port : 0;
	busy = 1;
	abandon = 0;
	fed = 0;
	pic_poll = poll;
	rc = -1;
	if (kind == PIC_JPEG && on_dsp && pic_size(buf, blen, &rw, &rh) && (rw + 7) / 8 + 1 > side &&
	    (rh + 7) / 8 + 1 > side && (acc = calloc((size_t)side * 3, sizeof(*acc))) != 0) {
		rw = (rw + 7) / 8 + 1;		/* (more than 1/8 as pic.c rounds it: the DSP's) */
		rh = (rh + 7) / 8 + 1;
		oy_at = -1;
		acc_rows = 0;
		rc = pic_jpeg_rows(feed, 0, (int)rw, (int)rh, row_big, 0, &why);
		if (rc == 1)
			row_big(0, rh, 0, 0);	/* (the last output row) */
		free(acc);
		acc = 0;
	} else if (kind == PIC_JPEG)
		rc = pic_jpeg_rows(feed, 0, side, side, row, 0, &why);
	if (rc < 0 && !abandon) {
		/* PNG, or a JPEG TJpgDec won't take: whole */
		p = pic_decode(buf, blen, side, side, 16000000L, &why);
		if (p) {
			short y;
			for (y = 0; y < side; y++)
				row(0, y, p->px + (long)y * p->w, 0);
			pic_free(p);
			rc = 1;
		} else
			rc = 0;
	}
	pic_poll = 0;
	busy = 0;
	if (abandon) {			/* (EPlay quits) */
		abandon = 0;
		drop();
		st = A_NONE;
		return;
	}
	if (rc != 1) {
		fail(pic_why_text(why));
		return;
	}
	Mfree(buf);
	buf = 0;
	snprintf(note, sizeof(note), "album art: %s, %ld KB, unpacked on the %s: read by %lu ms, its first poll %lu ms "
		 "later, done %lu ms after (the longest between polls %lu; %ld scan words, %ld MCUs)",
		 pic_kind_name(kind), (blen + 1023) / 1024, on_dsp ? pic_dsp_state() : "68030", t_dec0 - t_job,
		 t_feed1 ? t_feed1 - t_dec0 : 0, now_ms() - (t_feed1 ? t_feed1 : t_dec0), t_gapmax, n_put16, n_get);
	cache_store();
	st = A_DITHER;
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
			r = rd(0, tg.pic_at + bgot, buf + bgot, n);
			if (r <= 0) {
				fail("not read");
				break;
			}
			bgot += r;
			if (bgot >= blen) {
				Fclose((short)fh);
				fh = -1;
				st = A_DECODE;
			}
		} else if (st == A_DECODE) {
			decode();
			break;			/* (a step of its own) */
		} else if (st == A_DITHER) {
			if (planes == 16) {
				short y;
				for (y = 0; y < side; y++)
					memcpy(form + (long)y * wpl * 16, rgb + y * side, (size_t)side * 2);
				st = A_READY;
				break;
			}
			if (!dt) {
				if (!pal_made)
					make_pal();
				pens = malloc((size_t)side * side);
				dt = pens ? pic_dither_open(side, &pal) : 0;
				if (!dt) {
					fail("no memory to dither it");
					break;
				}
				drow = 0;
			}
			pic_dither_row(dt, rgb + drow * side, 0, pens + drow * side);
			if (++drow >= side) {
				long i;
				pic_dither_close(dt);
				dt = 0;
				memset(used, 0, sizeof(used));
				for (i = 0; i < (long)side * side; i++)
					used[pens[i]] = 1;
				memset(form, 0, (size_t)wpl * 2 * side * planes);
				next_pen = 0;
				st = A_PENS;
			}
		} else if (st == A_PENS) {
			while (next_pen < 256 && !used[next_pen])
				next_pen++;
			if (next_pen >= 256) {
				free(pens);
				pens = 0;
				st = A_READY;
				break;
			}
			blit_pen(next_pen++);
		}
	}
	if (st == A_READY || st == A_FAILED || (st == A_NONE && deferred[0])) {
		int ready = st == A_READY;
		if (note[0])
			elog("%s; shown after %lu ms", note, now_ms() - t_job);
		note[0] = 0;
		if (deferred[0]) {
			/* the file opened meanwhile: its own picture now */
			char next[256];
			snprintf(next, sizeof(next), "%s", deferred);
			deferred[0] = 0;
			path_now[0] = 0;
			art_begin(next);
			return 1;	/* (drawn again: the bar shows the new file's) */
		}
		return ready;
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
