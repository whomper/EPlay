/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_tags.c - src/tags.c on tags made here, byte by byte: ID3v2.2, 2.3
 * and 2.4 in each text encoding, the front cover chosen over another
 * picture, unsynchronisation (the whole tag, a frame), an extended header,
 * a data length indicator, the album artist, ID3v1 after ID3v2, a WAV's
 * LIST INFO and its "id3 " chunk, an AIFF's NAME and AUTH; then every tag
 * cut short at every length, and bytes changed at random, under ASan.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "tags.h"

static int bad;

typedef struct {
	unsigned char b[8192];
	long n;
} BUF;

static long mem_read(void *u, long at, unsigned char *buf, long n)
{
	BUF *f = u;
	if (at < 0 || at >= f->n)
		return 0;
	if (n > f->n - at)
		n = f->n - at;
	memcpy(buf, f->b + at, (size_t)n);
	return n;
}

static void put(BUF *f, const void *p, long n)
{
	memcpy(f->b + f->n, p, (size_t)n);
	f->n += n;
}

static void put1(BUF *f, int c)
{
	f->b[f->n++] = (unsigned char)c;
}

static void put_be(BUF *f, long v, int bytes, int safe)
{
	int i;
	for (i = bytes - 1; i >= 0; i--)
		put1(f, safe ? (int)((v >> (7 * i)) & 127) : (int)((v >> (8 * i)) & 255));
}

static void put_le(BUF *f, long v, int bytes)
{
	int i;
	for (i = 0; i < bytes; i++)
		put1(f, (int)((v >> (8 * i)) & 255));
}

/* an ID3v2 frame of version ver; flags (2.3, 2.4) */
static void frame(BUF *f, int ver, const char *id, const void *d, long n, int flags)
{
	put(f, id, ver == 2 ? 3 : 4);
	if (ver == 2)
		put_be(f, n, 3, 0);
	else {
		put_be(f, n, 4, ver == 4);
		put_be(f, flags, 2, 0);
	}
	put(f, d, n);
}

/* the tag's header, its size filled in by end_tag */
static long begin_tag(BUF *f, int ver, int flags)
{
	put(f, "ID3", 3);
	put1(f, ver);
	put1(f, 0);
	put1(f, flags);
	put_be(f, 0, 4, 1);
	return f->n;
}

static void end_tag(BUF *f, long body, int padding)
{
	long size;
	int i;
	for (i = 0; i < padding; i++)
		put1(f, 0);
	size = f->n - body;
	f->b[body - 4] = (unsigned char)((size >> 21) & 127);
	f->b[body - 3] = (unsigned char)((size >> 14) & 127);
	f->b[body - 2] = (unsigned char)((size >> 7) & 127);
	f->b[body - 1] = (unsigned char)(size & 127);
}

/* text with its encoding byte first */
static long text(unsigned char *out, int enc, const char *s, long n)
{
	out[0] = (unsigned char)enc;
	memcpy(out + 1, s, (size_t)n);
	return n + 1;
}

/* the Atari text for Unicode code points (charset.c's own mapping) */
static void atari(char *out, const unsigned long *u, int n)
{
	int k = 0, i;
	for (i = 0; i < n; i++)
		k += uni_to_atari(u[i], out + k);
	out[k] = 0;
}

static void same(const char *what, const char *got, const char *want)
{
	if (strcmp(got, want)) {
		printf("FAIL %s: \"%s\", wanted \"%s\"\n", what, got, want);
		bad = 1;
	}
}

static void same_n(const char *what, long got, long want)
{
	if (got != want) {
		printf("FAIL %s: %ld, wanted %ld\n", what, got, want);
		bad = 1;
	}
}

static void ok(const char *what)
{
	if (!bad)
		printf("ok   %s\n", what);
}

/* every length cut short, and bytes changed: nothing read outside the
   file, every text ended within its room */
static void rough(const char *what, const BUF *f0)
{
	static BUF f;
	TAGS t;
	long cut;
	int i, k;
	for (cut = 0; cut <= f0->n; cut++) {
		memcpy(f.b, f0->b, (size_t)f0->n);
		f.n = cut;
		tags_read(&t, mem_read, &f, f.n);
		if (memchr(t.title, 0, TAG_TEXT) == 0 || memchr(t.artist, 0, TAG_TEXT) == 0 ||
		    memchr(t.album, 0, TAG_TEXT) == 0 || (t.pic_end && (t.pic_at > t.pic_end || t.pic_end > f.n))) {
			printf("FAIL %s cut at %ld\n", what, cut);
			bad = 1;
			return;
		}
	}
	srand(1);
	for (k = 0; k < 3000; k++) {
		memcpy(f.b, f0->b, (size_t)f0->n);
		f.n = f0->n;
		for (i = 0; i < 1 + k % 6; i++)
			f.b[rand() % f.n] = (unsigned char)rand();
		tags_read(&t, mem_read, &f, f.n);
		if (memchr(t.title, 0, TAG_TEXT) == 0 || memchr(t.artist, 0, TAG_TEXT) == 0 ||
		    memchr(t.album, 0, TAG_TEXT) == 0 || (t.pic_end && (t.pic_at > t.pic_end || t.pic_end > f.n))) {
			printf("FAIL %s changed (%d)\n", what, k);
			bad = 1;
			return;
		}
	}
	printf("ok   %s: cut at every length, and changed 3,000 times\n", what);
}

/* a file made by another program (tests/tags/run.sh: lame, ffmpeg): its
   tags as wanted, and its picture a whole JPEG ($FF $D8 to $FF $D9) */
static long file_read(void *u, long at, unsigned char *buf, long n)
{
	FILE *fp = u;
	if (fseek(fp, at, SEEK_SET))
		return 0;
	return (long)fread(buf, 1, (size_t)n, fp);
}

static int check_file(char **a)
{
	FILE *fp = fopen(a[0], "rb");
	TAGS t;
	long size;
	unsigned char b0[2], b1[2];
	if (!fp) {
		printf("FAIL %s can't be opened\n", a[0]);
		return 1;
	}
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	tags_read(&t, file_read, fp, size);
	same(a[0], t.title, a[1]);
	same(a[0], t.artist, a[2]);
	same(a[0], t.album, a[3]);
	if (atoi(a[4])) {
		if (!t.pic_end || file_read(fp, t.pic_at, b0, 2) != 2 || file_read(fp, t.pic_end - 2, b1, 2) != 2 ||
		    b0[0] != 0xff || b0[1] != 0xd8 || b1[0] != 0xff || b1[1] != 0xd9) {
			printf("FAIL %s: its picture not found whole (%ld to %ld)\n", a[0], t.pic_at, t.pic_end);
			bad = 1;
		}
	} else
		same_n(a[0], t.pic_end, 0);
	fclose(fp);
	if (!bad)
		printf("ok   %s: \"%s\", \"%s\", \"%s\"%s\n", a[0], t.title, t.artist, t.album,
		       t.pic_end ? ", its picture" : "");
	return bad;
}

int main(int argc, char **argv)
{
	static BUF f;
	TAGS t;
	unsigned char d[600];
	char want[TAG_TEXT];
	long body, n, pic;

	if (argc == 6)
		return check_file(argv + 1);

	/* 2.3, Latin-1: "Café" */
	f.n = 0;
	body = begin_tag(&f, 3, 0);
	n = text(d, 0, "Caf\xe9", 4);
	frame(&f, 3, "TIT2", d, n, 0);
	n = text(d, 0, "Erik Satie", 10);
	frame(&f, 3, "TPE1", d, n, 0);
	n = text(d, 0, "Trois Gymnop\xe9" "dies", 16);
	frame(&f, 3, "TALB", d, n, 0);
	end_tag(&f, body, 20);
	put(&f, "\xff\xfb\x90\x00", 4);
	tags_read(&t, mem_read, &f, f.n);
	{
		unsigned long u[] = { 'C', 'a', 'f', 0xe9 };
		atari(want, u, 4);
	}
	same("2.3 Latin-1 title", t.title, want);
	same("2.3 artist", t.artist, "Erik Satie");
	same_n("2.3 the tag's length", t.id3_len, f.n - 4);
	same_n("2.3 no picture", t.pic_end, 0);
	ok("2.3, Latin-1, its length");
	rough("2.3", &f);

	/* 2.4, UTF-8 Hebrew; two pictures, the front cover second */
	f.n = 0;
	body = begin_tag(&f, 4, 0);
	n = text(d, 3, "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d", 8);	/* shalom */
	frame(&f, 4, "TIT2", d, n, 0);
	memcpy(d, "\x00image/png\x00\x00\x00PNGDATA", 20);
	frame(&f, 4, "APIC", d, 20, 0);
	memcpy(d, "\x03image/jpeg\x00\x03" "cover\x00", 19);
	memcpy(d + 19, "\xff\xd8JPEGDATA\xff\xd9", 12);
	frame(&f, 4, "APIC", d, 31, 0);
	pic = f.n - 12;
	end_tag(&f, body, 0);
	tags_read(&t, mem_read, &f, f.n);
	{
		unsigned long u[] = { 0x5e9, 0x5dc, 0x5d5, 0x5dd };
		atari(want, u, 4);
	}
	same("2.4 UTF-8 Hebrew title", t.title, want);
	same_n("2.4 front cover's start", t.pic_at, pic);
	same_n("2.4 front cover's end", t.pic_end, pic + 12);
	same_n("2.4 picture type", t.pic_type, 3);
	ok("2.4, UTF-8 Hebrew, the front cover over another picture");
	rough("2.4", &f);

	/* 2.3, UTF-16 with a little-endian BOM, a character past 16 bits; the
	   album artist where there is no artist */
	f.n = 0;
	body = begin_tag(&f, 3, 0);
	memcpy(d, "\x01\xff\xfeH\x00i\x00\x3d\xd8\x00\xde\x00\x00", 13);
	frame(&f, 3, "TIT2", d, 13, 0);
	n = text(d, 0, "The Band", 8);
	frame(&f, 3, "TPE2", d, n, 0);
	memcpy(d, "\x02\x00T\x00w\x00o", 7);	/* UTF-16 BE */
	frame(&f, 3, "TALB", d, 7, 0);
	end_tag(&f, body, 0);
	tags_read(&t, mem_read, &f, f.n);
	{
		unsigned long u[] = { 'H', 'i', 0x1f600 };
		atari(want, u, 3);
	}
	same("UTF-16 LE BOM, a surrogate pair", t.title, want);
	same("the album's artist", t.artist, "The Band");
	same("UTF-16 BE", t.album, "Two");
	ok("UTF-16 both ways, the album's artist");

	/* TPE1 after TPE2: the track's own artist */
	f.n = 0;
	body = begin_tag(&f, 3, 0);
	n = text(d, 0, "The Band", 8);
	frame(&f, 3, "TPE2", d, n, 0);
	n = text(d, 0, "Solo", 4);
	frame(&f, 3, "TPE1", d, n, 0);
	end_tag(&f, body, 0);
	tags_read(&t, mem_read, &f, f.n);
	same("TPE1 over TPE2", t.artist, "Solo");
	ok("the track's artist over the album's");

	/* 2.2: TT2 and PIC */
	f.n = 0;
	body = begin_tag(&f, 2, 0);
	n = text(d, 0, "Old", 3);
	frame(&f, 2, "TT2", d, n, 0);
	memcpy(d, "\x00JPG\x03\x00", 6);
	memcpy(d + 6, "\xff\xd8XY", 4);
	frame(&f, 2, "PIC", d, 10, 0);
	pic = f.n - 4;
	end_tag(&f, body, 8);
	tags_read(&t, mem_read, &f, f.n);
	same("2.2 title", t.title, "Old");
	same_n("2.2 picture", t.pic_at, pic);
	same_n("2.2 picture's end", t.pic_end, pic + 4);
	ok("2.2: TT2, PIC");
	rough("2.2", &f);

	/* 2.3 unsynchronised as a whole: "\xff" in the title, $FF $00 in the
	   picture; an extended header */
	f.n = 0;
	body = begin_tag(&f, 3, 0x80 | 0x40);
	put(&f, "\x00\x00\x00\x06\x00\x00\x00\x00\x00\x00", 10);	/* (its 6 bytes) */
	/* TIT2, size 3 (before unsynchronisation): 00 'A' FF -> 00 'A' FF 00 */
	put(&f, "TIT2\x00\x00\x00\x04\x00\x00", 10);
	put(&f, "\x00" "A\xff\x00" "B", 5);
	put(&f, "APIC\x00\x00\x00\x0b\x00\x00", 10);
	put(&f, "\x00\x00\x00\x03\x00", 5);	/* enc, mime "", type 3, desc "" */
	pic = f.n;
	put(&f, "\xff\x00\xd8\xff\x00\xe0\x01\x02", 8);	/* FF D8 FF E0 01 02 */
	end_tag(&f, body, 4);
	tags_read(&t, mem_read, &f, f.n);
	{
		unsigned long u[] = { 'A', 0xff, 'B' };
		atari(want, u, 3);
	}
	same("unsynchronised title", t.title, want);
	same_n("unsynchronised picture's start", t.pic_at, pic);
	same_n("unsynchronised picture's end", t.pic_end, pic + 8);
	same_n("unsynchronised picture", t.pic_unsync, 1);
	ok("2.3 unsynchronised, an extended header");
	rough("2.3 unsynchronised", &f);

	/* 2.4: a frame unsynchronised, with its data length */
	f.n = 0;
	body = begin_tag(&f, 4, 0);
	put(&f, "TIT2", 4);
	put_be(&f, 8, 4, 1);
	put_be(&f, 0x0003, 2, 0);	/* unsynchronised, a data length */
	put_be(&f, 4, 4, 1);
	put(&f, "\x00" "C\xff\x00", 4);	/* -> 00 'C' FF */
	put(&f, "TALB", 4);
	put_be(&f, 3, 4, 1);
	put_be(&f, 0, 2, 0);
	put(&f, "\x00" "Al", 3);
	end_tag(&f, body, 0);
	tags_read(&t, mem_read, &f, f.n);
	{
		unsigned long u[] = { 'C', 0xff };
		atari(want, u, 2);
	}
	same("2.4 frame unsynchronised", t.title, want);
	same("2.4 the frame after", t.album, "Al");
	ok("2.4: a frame unsynchronised, with its data length");

	/* a title longer than there is room for */
	f.n = 0;
	body = begin_tag(&f, 3, 0);
	memset(d, 'x', 200);
	d[0] = 0;
	frame(&f, 3, "TIT2", d, 200, 0);
	end_tag(&f, body, 0);
	tags_read(&t, mem_read, &f, f.n);
	same_n("a long title cut to its room", (long)strlen(t.title), TAG_TEXT - 1);
	ok("a long title cut to its room");

	/* ID3v1 after ID3v2: only what is still empty */
	f.n = 0;
	body = begin_tag(&f, 3, 0);
	n = text(d, 0, "From v2", 7);
	frame(&f, 3, "TIT2", d, n, 0);
	end_tag(&f, body, 0);
	put(&f, "\xff\xfb\x90\x00", 4);
	memset(d, 0, 128);
	memcpy(d, "TAG", 3);
	memcpy(d + 3, "From v1", 7);
	memcpy(d + 33, "Artist v1                     ", 30);
	memcpy(d + 63, "Album v1", 8);
	put(&f, d, 128);
	tags_read(&t, mem_read, &f, f.n);
	same("v2's title kept", t.title, "From v2");
	same("v1's artist, trimmed", t.artist, "Artist v1");
	same("v1's album", t.album, "Album v1");
	ok("ID3v1 fills only what ID3v2 left empty");

	/* a WAV: LIST INFO (an odd length padded), then an "id3 " chunk */
	f.n = 0;
	put(&f, "RIFF\x00\x00\x00\x00WAVE", 12);
	put(&f, "fmt ", 4);
	put_le(&f, 16, 4);
	memset(d, 0, 16);
	put(&f, d, 16);
	put(&f, "LIST", 4);
	put_le(&f, 4 + 8 + 5 + 1 + 8 + 4 + 8 + 9 + 1, 4);
	put(&f, "INFO", 4);
	put(&f, "INAM", 4);
	put_le(&f, 5, 4);
	put(&f, "Name\x00\x00", 6);
	put(&f, "IART", 4);
	put_le(&f, 4, 4);
	put(&f, "Art\x00", 4);
	put(&f, "IPRD", 4);
	put_le(&f, 9, 4);
	put(&f, "Alb \xc3\xa9t\xc3\xa9\x00", 10);	/* UTF-8 */
	put(&f, "data", 4);
	put_le(&f, 4, 4);
	put(&f, "\x00\x00\x00\x00", 4);
	tags_read(&t, mem_read, &f, f.n);
	same("WAV INAM", t.title, "Name");
	same("WAV IART", t.artist, "Art");
	{
		unsigned long u[] = { 'A', 'l', 'b', ' ', 0xe9, 't', 0xe9 };
		atari(want, u, 7);
	}
	same("WAV IPRD in UTF-8", t.album, want);
	ok("a WAV's LIST INFO");
	rough("WAV LIST INFO", &f);
	{
		long at = f.n;
		long id3;
		put(&f, "id3 ", 4);
		put_le(&f, 0, 4);
		id3 = f.n;
		body = begin_tag(&f, 3, 0);
		n = text(d, 0, "From id3", 8);
		frame(&f, 3, "TIT2", d, n, 0);
		memcpy(d, "\x00\x00\x03\x00", 4);
		memcpy(d + 4, "\xff\xd8ZZ", 4);
		frame(&f, 3, "APIC", d, 8, 0);
		pic = f.n - 4;
		end_tag(&f, body, 0);
		n = f.n - id3;
		f.b[at + 4] = (unsigned char)n;
		f.b[at + 5] = (unsigned char)(n >> 8);
		if (n & 1)
			put1(&f, 0);
		tags_read(&t, mem_read, &f, f.n);
		same("WAV id3 chunk's title over INAM", t.title, "From id3");
		same("WAV IART kept", t.artist, "Art");
		same_n("WAV id3 chunk's picture", t.pic_at, pic);
		same_n("an ID3 chunk isn't the file's start", t.id3_len, 0);
		ok("a WAV's id3 chunk");
		rough("WAV id3 chunk", &f);
	}

	/* an AIFF: NAME and AUTH */
	f.n = 0;
	put(&f, "FORM\x00\x00\x00\x00" "AIFF", 12);
	put(&f, "NAME\x00\x00\x00\x05" "Aname\x00", 14);
	put(&f, "AUTH\x00\x00\x00\x04" "Auth", 12);
	tags_read(&t, mem_read, &f, f.n);
	same("AIFF NAME", t.title, "Aname");
	same("AIFF AUTH", t.artist, "Auth");
	ok("an AIFF's NAME and AUTH");
	rough("AIFF", &f);

	/* nothing at all */
	f.n = 0;
	put(&f, "\xff\xfb\x90\x00\x00\x00", 6);
	tags_read(&t, mem_read, &f, f.n);
	same("no tags", t.title, "");
	same_n("no tags: no picture", t.pic_end, 0);
	ok("a file without tags");
	return bad;
}
