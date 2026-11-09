/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tags.c - a file's tags (tags.h). ID3v2 is read as a stream of bytes, a
 * few hundred at a time through the callback: where it is unsynchronised
 * (the whole tag in 2.2 and 2.3, a frame in 2.4), each $FF $00 reads as
 * $FF. A 2.2 or 2.3 frame's size counts its bytes so read; a 2.4 frame's
 * counts them in the file. Text frames give at most TAG_TEXT - 1 bytes of
 * Atari text; compressed and encrypted frames are passed over.
 */
#include <string.h>
#include "charset.h"
#include "tags.h"

void tags_clear(TAGS *t)
{
	memset(t, 0, sizeof(*t));
	t->pic_type = -1;
	rg_clear(&t->rg);
}

static int same_ci(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		int x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a, y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
		if (x != y)
			return 0;
	}
	return *a == *b;
}

/* ---- the bytes, unsynchronised or not ---- */

typedef struct {
	TAG_READ rd;
	void *u;
	long raw, end;		/* the next byte in the file; the tag's end */
	int unsync, ff;		/* the $FF $00 rule on; the last byte was $FF */
	long bat, bn;		/* buf holds the file's [bat, bat + bn) */
	unsigned char buf[256];
} SRC;

static int rawb(SRC *s)
{
	if (s->raw >= s->end)
		return -1;
	if (s->raw < s->bat || s->raw >= s->bat + s->bn) {
		long n = s->end - s->raw;
		if (n > (long)sizeof(s->buf))
			n = sizeof(s->buf);
		n = s->rd(s->u, s->raw, s->buf, n);
		if (n <= 0) {
			s->end = s->raw;	/* (the file ends: so does the tag) */
			return -1;
		}
		s->bat = s->raw;
		s->bn = n;
	}
	return s->buf[s->raw++ - s->bat];
}

static int getb(SRC *s)
{
	int c;
	for (;;) {
		c = rawb(s);
		if (c < 0)
			return -1;
		if (s->unsync && s->ff && c == 0) {
			s->ff = 0;
			continue;
		}
		s->ff = c == 0xff;
		return c;
	}
}

/* a frame's bytes: left counts them as read (2.2, 2.3), or rend is where
   it ends in the file (2.4) */
typedef struct {
	SRC *s;
	long left, rend;
	int by_raw;
} FR;

static int fgetb(FR *f)
{
	int c;
	if (f->by_raw) {
		if (f->s->raw >= f->rend)
			return -1;
		c = getb(f->s);
		if (f->s->raw > f->rend) {
			/* (its last byte a $00 passed over: the next was the next frame's) */
			f->s->raw = f->rend;
			f->s->ff = 0;
			return -1;
		}
		return c;
	}
	if (f->left <= 0)
		return -1;
	f->left--;
	return getb(f->s);
}

/* to the frame's end */
static void fskip(FR *f)
{
	if (f->by_raw) {
		if (f->s->raw < f->rend)
			f->s->raw = f->rend;
		f->s->ff = 0;
	} else if (!f->s->unsync) {
		f->s->raw += f->left;
		f->left = 0;
	} else
		while (fgetb(f) >= 0)
			;
}

/* ---- text ---- */

static void put_uni(char *out, int *n, unsigned long c)
{
	char a[4];
	int k = uni_to_atari(c, a), i;
	if (*n + k > TAG_TEXT - 1)
		return;
	for (i = 0; i < k; i++)
		out[(*n)++] = a[i];
}

static void trim(char *out, int n)
{
	while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == 0))
		n--;
	out[n] = 0;
}

/* the frame's text, in its encoding (0 Latin-1, 1 UTF-16 with its BOM, 2
   UTF-16 BE, 3 UTF-8), to its first NUL (2.4's values are NUL apart) */
static void frame_text(FR *f, int enc, char *out)
{
	int n = 0, be = 1, c, c2, first = enc == 1;
	unsigned long u, pend = 0;
	if (enc == 1 || enc == 2) {
		/* (UTF-16 without a BOM: big-endian, as encoding 2) */
		for (;;) {
			c = fgetb(f);
			c2 = c < 0 ? -1 : fgetb(f);
			if (c2 < 0)
				break;
			u = be ? (unsigned long)(c << 8 | c2) : (unsigned long)(c2 << 8 | c);
			if (first) {
				first = 0;
				if (u == 0xfeff)
					continue;	/* the BOM: big-endian */
				if (u == 0xfffe) {
					be = 0;		/* little-endian */
					continue;
				}
			}
			if (u == 0)
				break;
			if (u >= 0xd800 && u < 0xdc00) {
				pend = u;
				continue;
			}
			if (u >= 0xdc00 && u < 0xe000) {
				if (pend)
					put_uni(out, &n, 0x10000 + ((pend - 0xd800) << 10) + (u - 0xdc00));
				pend = 0;
				continue;
			}
			pend = 0;
			put_uni(out, &n, u);
		}
	} else if (enc == 3) {
		while ((c = fgetb(f)) > 0) {
			int k = c < 0x80 ? 0 : c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : -1;
			if (k < 0)
				continue;		/* (a stray continuation byte) */
			u = k == 0 ? (unsigned long)c : (unsigned long)(c & (0x3f >> k));
			while (k-- > 0) {
				c2 = fgetb(f);
				if (c2 < 0)
					break;
				u = u << 6 | (c2 & 0x3f);
			}
			put_uni(out, &n, u);
		}
	} else
		while ((c = fgetb(f)) > 0)
			put_uni(out, &n, (unsigned long)c);
	trim(out, n);
}

/* a text that may be UTF-8 (a WAV's INFO, an AIFF's NAME), else Latin-1 */
static int valid_utf8(const unsigned char *p, long n)
{
	long i = 0;
	int any = 0;
	while (i < n) {
		int c = p[i], k = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : -1;
		if (k < 0)
			return 0;
		if (k)
			any = 1;
		if (i + k >= n + (k ? 0 : 1))
			return 0;
		while (k-- > 0)
			if ((p[++i] & 0xc0) != 0x80)
				return 0;
		i++;
	}
	return any;
}

static void plain_text(const unsigned char *p, long n, char *out)
{
	int k = 0, utf8;
	long i = 0, m;
	for (m = 0; m < n && p[m]; m++)
		;
	utf8 = valid_utf8(p, m);
	while (i < m) {
		int c = p[i];
		if (utf8 && c >= 0x80) {
			int more = (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : 3;
			unsigned long u = (unsigned long)(c & (0x3f >> more));
			while (more-- > 0 && i + 1 < m)
				u = u << 6 | (p[++i] & 0x3f);
			put_uni(out, &k, u);
		} else
			put_uni(out, &k, (unsigned long)c);
		i++;
	}
	trim(out, k);
}

/* ---- ID3v2 ---- */

static long syncsafe(const unsigned char *b)
{
	return (long)(b[0] & 127) << 21 | (long)(b[1] & 127) << 14 | (long)(b[2] & 127) << 7 | (b[3] & 127);
}

static long be32(const unsigned char *b)
{
	return (long)b[0] << 24 | (long)b[1] << 16 | (long)b[2] << 8 | b[3];
}

/* a picture frame (APIC, or 2.2's PIC): its bytes found, not read */
static void frame_pic(TAGS *t, FR *f, int v22)
{
	int enc = fgetb(f), c = 0, type, i;
	if (enc < 0)
		return;
	if (v22) {
		for (i = 0; i < 3; i++)
			if (fgetb(f) < 0)
				return;
	} else
		while ((c = fgetb(f)) > 0)
			;			/* the MIME type (pic.c tells the kind by its bytes) */
	type = fgetb(f);
	if (type < 0)
		return;
	if (enc == 1 || enc == 2) {
		for (;;) {
			int a = fgetb(f), b = a < 0 ? -1 : fgetb(f);
			if (b < 0)
				return;
			if (!a && !b)
				break;
		}
	} else
		while ((c = fgetb(f)) > 0)
			;
	if (c < 0 && !(enc == 1 || enc == 2))
		return;
	/* the front cover over any other; else the first */
	if (t->pic_end && (t->pic_type == 3 || type != 3))
		return;
	t->pic_at = f->s->raw;
	t->pic_end = f->by_raw ? f->rend : f->s->raw + f->left;	/* (2.3 unsynchronised: the end found below) */
	t->pic_unsync = f->s->unsync;
	t->pic_type = type;
	if (!f->by_raw && f->s->unsync) {
		fskip(f);
		t->pic_end = f->s->raw;
	}
}

int tags_id3v2(TAGS *t, TAG_READ rd, void *u, long base)
{
	static SRC src;
	static char band[TAG_TEXT];
	SRC *s = &src;
	unsigned char h[10];
	int ver, flags, idl, i;
	long size;
	if (rd(u, base, h, 10) != 10 || memcmp(h, "ID3", 3) || h[3] < 2 || h[3] > 4 || h[4] == 0xff ||
	    (h[6] | h[7] | h[8] | h[9]) & 0x80)
		return 0;
	ver = h[3];
	flags = h[5];
	size = syncsafe(h + 6);
	if (base == 0)
		t->id3_len = 10 + size + (ver == 4 && (flags & 0x10) ? 10 : 0);
	memset(s, 0, sizeof(*s));
	s->rd = rd;
	s->u = u;
	s->raw = base + 10;
	s->end = base + 10 + size;
	s->bn = 0;
	s->unsync = ver < 4 && (flags & 0x80);
	if (ver == 2 && (flags & 0x40))
		return 1;			/* (2.2's compression: no scheme was ever set) */
	if (flags & 0x40) {		/* an extended header: past it */
		unsigned char e[4];
		for (i = 0; i < 4; i++) {
			int c = getb(s);
			if (c < 0)
				return 1;
			e[i] = (unsigned char)c;
		}
		if (ver == 3) {
			long n = be32(e);
			while (n-- > 0 && getb(s) >= 0)
				;
		} else if (syncsafe(e) >= 4)
			s->raw += syncsafe(e) - 4;
		s->ff = 0;
	}
	idl = ver == 2 ? 3 : 4;
	band[0] = 0;
	while (s->raw + idl * 2 <= s->end) {
		unsigned char fh[10];
		char id[5];
		long fsize;
		int fflags = 0, n = ver == 2 ? 6 : 10, c;
		FR f;
		for (i = 0; i < n; i++) {
			c = getb(s);
			if (c < 0)
				break;
			fh[i] = (unsigned char)c;
		}
		if (i < n || !fh[0])
			break;			/* the tag's end, or its padding */
		memcpy(id, fh, (size_t)idl);
		id[idl] = 0;
		if (ver == 2)
			fsize = (long)fh[3] << 16 | (long)fh[4] << 8 | fh[5];
		else {
			fsize = ver == 4 ? syncsafe(fh + 4) : be32(fh + 4);
			fflags = fh[8] << 8 | fh[9];
		}
		if (fsize < 0)
			break;
		f.s = s;
		f.by_raw = ver == 4;
		f.left = fsize;
		f.rend = s->raw + fsize;
		if (f.by_raw && f.rend > s->end)
			f.rend = s->end;
		{
			int saved = s->unsync, skip = 0;
			if (ver == 3 && (fflags & 0x00c0))
				skip = 1;		/* compressed, encrypted */
			if (ver == 4) {
				if (fflags & 0x000c)
					skip = 1;
				if (fflags & 0x0002)
					s->unsync = 1;
				if (!skip && (fflags & 0x0001))
					for (i = 0; i < 4; i++)
						fgetb(&f);	/* the data length */
			}
			if (ver == 3 && (fflags & 0x0020))
				fgetb(&f);	/* the group */
			if (ver == 4 && (fflags & 0x0040))
				fgetb(&f);
			if (!skip) {
				char *dst = 0;
				if (!strcmp(id, "TIT2") || !strcmp(id, "TT2"))
					dst = t->title;
				else if (!strcmp(id, "TPE1") || !strcmp(id, "TP1"))
					dst = t->artist;
				else if (!strcmp(id, "TALB") || !strcmp(id, "TAL"))
					dst = t->album;
				else if (!strcmp(id, "TPE2") || !strcmp(id, "TP2"))
					dst = band;		/* the album's artist */
				if (dst && !dst[0]) {
					int enc = fgetb(&f);
					if (enc >= 0)
						frame_text(&f, enc, dst);
				} else if (!strcmp(id, "APIC") || !strcmp(id, "PIC"))
					frame_pic(t, &f, ver == 2);
				else if (!strcmp(id, "TXXX") || !strcmp(id, "TXX")) {
					/* ReplayGain: the description, then the value */
					char d[TAG_TEXT], v[TAG_TEXT];
					int enc = fgetb(&f);
					if (enc >= 0) {
						frame_text(&f, enc, d);
						frame_text(&f, enc, v);
						if (same_ci(d, "REPLAYGAIN_TRACK_GAIN") && t->rg.track == RG_NONE)
							t->rg.track = rg_parse_db(v);
						else if (same_ci(d, "REPLAYGAIN_ALBUM_GAIN") && t->rg.album == RG_NONE)
							t->rg.album = rg_parse_db(v);
						else if (same_ci(d, "REPLAYGAIN_TRACK_PEAK") && !t->rg.tpeak)
							t->rg.tpeak = rg_parse_peak(v);
						else if (same_ci(d, "REPLAYGAIN_ALBUM_PEAK") && !t->rg.apeak)
							t->rg.apeak = rg_parse_peak(v);
					}
				}
			}
			fskip(&f);
			s->unsync = saved;
			s->ff = 0;
		}
	}
	if (!t->artist[0])
		strcpy(t->artist, band);	/* (where the track names none of its own) */
	return 1;
}

/* ---- ID3v1 ---- */

int tags_id3v1(TAGS *t, TAG_READ rd, void *u, long size)
{
	unsigned char b[128];
	if (size < 128 || rd(u, size - 128, b, 128) != 128 || memcmp(b, "TAG", 3))
		return 0;
	if (!t->title[0])
		plain_text(b + 3, 30, t->title);
	if (!t->artist[0])
		plain_text(b + 33, 30, t->artist);
	if (!t->album[0])
		plain_text(b + 63, 30, t->album);
	return 1;
}

/* ---- WAV and AIFF ---- */

static void chunk_text(TAG_READ rd, void *u, long at, long n, char *dst)
{
	unsigned char b[TAG_TEXT * 2];
	if (dst[0] || n <= 0)
		return;
	if (n > (long)sizeof(b))
		n = sizeof(b);
	n = rd(u, at, b, n);
	if (n > 0)
		plain_text(b, n, dst);
}

int tags_chunks(TAGS *t, TAG_READ rd, void *u, long size)
{
	unsigned char h[12];
	long at, end;
	int riff;
	if (rd(u, 0, h, 12) != 12)
		return 0;
	riff = !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4);
	if (!riff && !(!memcmp(h, "FORM", 4) && (!memcmp(h + 8, "AIFF", 4) || !memcmp(h + 8, "AIFC", 4))))
		return 0;
	end = size;
	at = 12;
	while (at + 8 <= end) {
		unsigned char c[8];
		long n;
		if (rd(u, at, c, 8) != 8)
			break;
		n = riff ? (long)c[7] << 24 | (long)c[6] << 16 | (long)c[5] << 8 | c[4] : be32(c + 4);
		if (n < 0 || n > end - at - 8)
			n = end - at - 8;	/* (a last chunk cut short, or a stream's) */
		if (riff && !memcmp(c, "LIST", 4) && n >= 4) {
			unsigned char ty[4];
			if (rd(u, at + 8, ty, 4) == 4 && !memcmp(ty, "INFO", 4)) {
				long s = at + 12, e = at + 8 + n;
				while (s + 8 <= e) {
					unsigned char sc[8];
					long sn;
					if (rd(u, s, sc, 8) != 8)
						break;
					sn = (long)sc[7] << 24 | (long)sc[6] << 16 | (long)sc[5] << 8 | sc[4];
					if (sn < 0 || sn > e - s - 8)
						break;
					if (!memcmp(sc, "INAM", 4))
						chunk_text(rd, u, s + 8, sn, t->title);
					else if (!memcmp(sc, "IART", 4))
						chunk_text(rd, u, s + 8, sn, t->artist);
					else if (!memcmp(sc, "IPRD", 4))
						chunk_text(rd, u, s + 8, sn, t->album);
					s += 8 + sn + (sn & 1);
				}
			}
		} else if (!riff && !memcmp(c, "NAME", 4))
			chunk_text(rd, u, at + 8, n, t->title);
		else if (!riff && !memcmp(c, "AUTH", 4))
			chunk_text(rd, u, at + 8, n, t->artist);
		else if (!memcmp(c, "id3 ", 4) || !memcmp(c, "ID3 ", 4)) {
			TAGS t2;
			tags_clear(&t2);
			if (tags_id3v2(&t2, rd, u, at + 8)) {
				/* (the ID3's over the chunks' names: it has more) */
				if (t2.title[0])
					strcpy(t->title, t2.title);
				if (t2.artist[0])
					strcpy(t->artist, t2.artist);
				if (t2.album[0])
					strcpy(t->album, t2.album);
				t->rg = t2.rg;
				if (t2.pic_end) {
					t->pic_at = t2.pic_at;
					t->pic_end = t2.pic_end;
					t->pic_unsync = t2.pic_unsync;
					t->pic_type = t2.pic_type;
				}
			}
		}
		at += 8 + n + (n & 1);
	}
	return 1;
}

void tags_read(TAGS *t, TAG_READ rd, void *u, long size)
{
	tags_clear(t);
	if (!tags_chunks(t, rd, u, size)) {
		tags_id3v2(t, rd, u, 0);
		tags_id3v1(t, rd, u, size);
	}
	/* (a tag longer than the file: the picture to the file's end) */
	if (t->pic_end > size)
		t->pic_end = size;
	if (t->pic_at >= t->pic_end)
		t->pic_at = t->pic_end = 0;
}
