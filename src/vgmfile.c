/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * vgmfile.c - a VGM or VGZ file read whole into memory (vgmfile.h).
 */
#include <string.h>
#include "vgmfile.h"
#include "vgm.h"
#include "inflate.h"

int vgm_magic(const unsigned char *b, long n)
{
	if (n >= 4 && !memcmp(b, "Vgm ", 4))
		return 1;
	if (n >= 3 && b[0] == 0x1f && b[1] == 0x8b && b[2] == 8)
		return 2;
	return 0;
}

typedef struct {
	TAG_READ rd;
	void *u;
	long at, end;
} SRC;

static int get(void *v, unsigned char *buf, long n)
{
	SRC *s = v;
	long r;
	if (s->at >= s->end)
		return 0;
	if (n > s->end - s->at)
		n = s->end - s->at;
	r = s->rd(s->u, s->at, buf, n);
	if (r <= 0)
		return 0;
	s->at += r;
	return (int)r;
}

/* the gzip header's bytes (RFC 1952), 0 when it isn't one */
static long gz_head(TAG_READ rd, void *u, long size)
{
	unsigned char h[10], b[2];
	long at = 10;
	int fl;
	if (rd(u, 0, h, 10) != 10 || h[0] != 0x1f || h[1] != 0x8b || h[2] != 8)
		return 0;
	fl = h[3];
	if (fl & 4) {			/* FEXTRA */
		if (rd(u, at, b, 2) != 2)
			return 0;
		at += 2 + (b[0] | b[1] << 8);
	}
	for (int k = 3; k <= 4; k++)	/* FNAME, FCOMMENT: to their 0 */
		if (fl & (1 << k)) {
			for (;;) {
				if (at >= size || rd(u, at, b, 1) != 1)
					return 0;
				at++;
				if (!b[0])
					break;
			}
		}
	if (fl & 2)			/* FHCRC */
		at += 2;
	return at < size ? at : 0;
}

long vgm_need;

unsigned char *vgm_load(TAG_READ rd, void *u, long size, long *n, void *(*alloc)(long), void (*release)(void *),
			const char **why)
{
	unsigned char head[4], *b;
	long len;
	*n = 0;
	vgm_need = 0;
	*why = "isn't a VGM file";
	if (size < 4 || rd(u, 0, head, 4) != 4)
		return 0;
	if (vgm_magic(head, 4) == 1) {
		vgm_need = size;
		if (size > VGM_MAX) {
			*why = "is too big to play";
			return 0;
		}
		b = alloc(size);
		if (!b) {
			*why = "can't be played: there isn't the memory";
			return 0;
		}
		if (rd(u, 0, b, size) != size) {
			release(b);
			*why = "can't be read";
			return 0;
		}
		*n = size;
		return b;
	}
	if (vgm_magic(head, 4) == 2) {
		unsigned char t[4];
		long at = gz_head(rd, u, size), got = 0, r;
		SRC s;
		INFLATE *z;
		if (!at || size < at + 8 || rd(u, size - 4, t, 4) != 4) {
			*why = "is damaged";
			return 0;
		}
		len = (long)((unsigned long)t[0] | (unsigned long)t[1] << 8 | (unsigned long)t[2] << 16 |
			     (unsigned long)t[3] << 24);	/* (ISIZE: its size unpacked) */
		vgm_need = len;
		if (len < 64 || len > VGM_MAX) {
			*why = len > VGM_MAX ? "is too big to play" : "is damaged";
			return 0;
		}
		b = alloc(len);
		if (!b) {
			*why = "can't be played: there isn't the memory";
			return 0;
		}
		s.rd = rd;
		s.u = u;
		s.at = at;
		s.end = size - 8;
		z = inflate_open(get, &s);
		if (!z) {
			release(b);
			*why = "can't be played: there isn't the memory";
			return 0;
		}
		while (got < len && (r = inflate_read(z, b + got, len - got)) > 0)
			got += r;
		inflate_close(z);
		if (got != len || vgm_magic(b, len) != 1) {
			release(b);
			*why = got != len ? "is damaged" : "isn't a VGM file";
			return 0;
		}
		*n = len;
		return b;
	}
	return 0;
}

long vgm_probe(TAG_READ rd, void *u, long size, TAGS *t, void *(*alloc)(long), void (*release)(void *))
{
	unsigned char head[0x100];
	VGMHEAD h;
	long n, len;
	tags_clear(t);
	n = rd(u, 0, head, sizeof(head));
	if (n < 4 || !vgm_magic(head, n))
		return -1;
	if (vgm_magic(head, n) == 1) {
		/* a plain one: its header, and its GD3 read where it is */
		static unsigned char g[1024];
		long glen;
		if (!vgm_head_of(head, n, size, &h))
			return -1;
		len = vgm_length(&h);
		if (h.gd3_at) {
			/* (read into g from 4 on: vgm_gd3 finds it there) */
			VGMHEAD g2 = h;
			glen = rd(u, h.gd3_at, g + 4, sizeof(g) - 4);
			if (glen >= 12) {
				g2.gd3_at = 4;
				vgm_gd3(g, glen + 4, &g2, t->title, t->album, t->artist, TAG_TEXT);
			}
		}
	} else {
		const char *why;
		unsigned char *b = vgm_load(rd, u, size, &n, alloc, release, &why);
		if (!b)
			return vgm_magic(head, 4) == 2 ? -1 : 0;
		if (!vgm_head(b, n, &h)) {
			release(b);
			return -1;
		}
		len = vgm_length(&h);
		vgm_gd3(b, n, &h, t->title, t->album, t->artist, TAG_TEXT);
		release(b);
	}
	return len / VGM_RATE * 1000 + len % VGM_RATE * 1000 / VGM_RATE;
}
