/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_stream.c - src/stream.c on the build machine, under AddressSanitizer:
 * URLs, the answer's head, ICY metadata cut at every place, .PLS.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void urls(void)
{
	SURL u;
	CHECK(surl_parse("http://radio.example.com/live", &u) && !u.tls && u.port == 80 &&
	      !strcmp(u.host, "radio.example.com") && !strcmp(u.path, "/live"));
	CHECK(surl_parse("HTTPS://radio.example.com:8443/a/b.mp3?x=1#t", &u) && u.tls && u.port == 8443 &&
	      !strcmp(u.path, "/a/b.mp3?x=1"));
	CHECK(surl_parse("http://192.168.1.10:8000", &u) && u.port == 8000 && !strcmp(u.path, "/"));
	CHECK(surl_parse("http://h?q", &u) && !strcmp(u.path, "/?q"));
	CHECK(!surl_parse("ftp://h/x", &u));
	CHECK(!surl_parse("http://", &u));
	CHECK(!surl_parse("http://h:0/", &u));
	CHECK(!surl_parse("http://h:65536/", &u));
	CHECK(!surl_parse("http://h:80x/", &u));
	CHECK(!surl_parse("http://user:pw@h/", &u));
	CHECK(!surl_parse("http://h/a b", &u));
	CHECK(!surl_parse("http://[::1]/", &u));
	{
		char big[700];
		memset(big, 'a', sizeof(big) - 1);
		big[sizeof(big) - 1] = 0;
		memcpy(big, "http://", 7);
		CHECK(!surl_parse(big, &u));	/* (the host too long) */
		big[20] = '/';
		CHECK(!surl_parse(big, &u));	/* (the path too long) */
	}
	CHECK(surl_is("Http://x") && surl_is("https://x") && !surl_is("C:\\MUSIC\\A.MP3"));
}

static void heads(void)
{
	SHEAD h;
	const char *a = "ICY 200 OK\r\nicy-name: The Station\r\nicy-br: 128,128\r\nicy-metaint: 16000\r\n"
			"Content-Type: audio/mpeg; charset=x\r\n\r\nAUDIO";
	long n = shead_parse(a, (long)strlen(a), &h);
	CHECK(n == (long)(strstr(a, "AUDIO") - a));
	CHECK(h.status == 200 && h.metaint == 16000 && h.kbps == 128 && !strcmp(h.name, "The Station") &&
	      !strcmp(h.type, "audio/mpeg") && stype(h.type) == STYPE_MP3);
	a = "HTTP/1.1 302 Found\nLocation: https://other.example/x\n\n";
	n = shead_parse(a, (long)strlen(a), &h);
	CHECK(n == (long)strlen(a) && h.status == 302 && !strcmp(h.location, "https://other.example/x"));
	/* not all come yet: every cut of the head before its blank line */
	a = "ICY 200 OK\r\nicy-metaint: 8192\r\n\r\n";
	for (n = 0; n < (long)strlen(a); n++)
		CHECK(shead_parse(a, n, &h) == 0);
	CHECK(shead_parse(a, (long)strlen(a), &h) == (long)strlen(a) && h.metaint == 8192);
	a = "SSH-2.0-x\r\n\r\n";
	CHECK(shead_parse(a, (long)strlen(a), &h) == -1);
	a = "ICY 200 OK\r\nicy-metaint: 0\r\n\r\n";
	CHECK(shead_parse(a, (long)strlen(a), &h) == -1);
	a = "ICY 200 OK\r\nicy-metaint: 99999999999\r\n\r\n";
	CHECK(shead_parse(a, (long)strlen(a), &h) == -1);
	a = "HTTP/1.1 2x0 OK\r\n\r\n";
	CHECK(shead_parse(a, (long)strlen(a), &h) == -1);
	{
		static char longl[3000];
		memcpy(longl, "ICY 200 OK\r\nX: ", 15);
		memset(longl + 15, 'x', 2000);
		/* (a header line over 1 KB, not ended: more wanted; the caller's
		   8 KB for the whole head bounds it) */
		CHECK(shead_parse(longl, 2015, &h) == 0);
	}
	CHECK(stype("audio/aacp") == STYPE_AAC && stype("audio/x-scpls") == STYPE_PLAYLIST &&
	      stype("text/html") == STYPE_UNKNOWN);
}

/* audio of metaint 10 with two metadata blocks, fed in every piece size */
static void icy(void)
{
	unsigned char in[200], buf[200];
	long n = 0, i, piece;
	const char *m1 = "StreamTitle='Artist - It''s a song';StreamUrl='';";
	long l1 = (long)strlen(m1), b1 = (l1 + 15) / 16;
	for (i = 0; i < 10; i++)
		in[n++] = (unsigned char)('a' + i);
	in[n++] = (unsigned char)b1;
	memset(in + n, 0, (size_t)(b1 * 16));
	memcpy(in + n, m1, (size_t)l1);
	n += b1 * 16;
	for (i = 0; i < 10; i++)
		in[n++] = (unsigned char)('A' + i);
	in[n++] = 0;			/* (no metadata this time) */
	for (i = 0; i < 5; i++)
		in[n++] = (unsigned char)('0' + i);
	for (piece = 1; piece <= n; piece++) {
		ICY s;
		long r = 0, out = 0;
		unsigned char got[200];
		icy_init(&s, 10);
		while (r < n) {
			long k = n - r < piece ? n - r : piece, a;
			memcpy(buf, in + r, (size_t)k);
			a = icy_feed(&s, buf, k);
			memcpy(got + out, buf, (size_t)a);
			out += a;
			r += k;
		}
		CHECK(out == 25 && !memcmp(got, "abcdefghijABCDEFGHIJ01234", 25));
		CHECK(!strcmp(s.title, "Artist - It''s a song") && s.new_title);
	}
	{
		ICY s;			/* no metaint: all audio */
		memcpy(buf, "xyz", 3);
		icy_init(&s, 0);
		CHECK(icy_feed(&s, buf, 3) == 3);
	}
}

static int npls;
static char pls_got[4][2][600];

static void add(const char *url, const char *title, void *ctx)
{
	(void)ctx;
	if (npls < 4) {
		strcpy(pls_got[npls][0], url);
		strcpy(pls_got[npls][1], title);
	}
	npls++;
}

static void pls(void)
{
	const char *t = "\xef\xbb\xbf[playlist]\r\nNumberOfEntries=2\r\nTitle2=Second\r\n"
			"File1=http://a.example/1\r\nTitle1=First\r\nFile2=http://b.example:8000/2\r\nFile0=x\r\n"
			"Version=2\r\n";
	CHECK(pls_read(t, (long)strlen(t), add, NULL) == 2 && npls == 2);
	CHECK(!strcmp(pls_got[0][0], "http://a.example/1") && !strcmp(pls_got[0][1], "First"));
	CHECK(!strcmp(pls_got[1][0], "http://b.example:8000/2") && !strcmp(pls_got[1][1], "Second"));
	npls = 0;
	CHECK(pls_read("File1=", 6, add, NULL) == 0 && npls == 0);
}

/* chunked: whole, and cut at every place; a bad frame refused; the
   last chunk ends it (its trailer passed over) */
static void chunks(void)
{
	const char *t = "4\r\nWiki\r\n6;ext=1\r\npedia \r\nE\r\nin \r\n\r\nchunks.\r\n0\r\nX-T: 1\r\n\r\n";
	const char *want = "Wikipedia in \r\n\r\nchunks.";
	long n = (long)strlen(t), cut;
	for (cut = 1; cut <= n; cut++) {
		CHUNKS c;
		unsigned char b[128], out[128];
		long i, o = 0;
		chunks_init(&c);
		for (i = 0; i < n; i += cut) {
			long k = n - i < cut ? n - i : cut, r;
			memcpy(b, t + i, (size_t)k);
			r = chunks_feed(&c, b, k);
			CHECK(r >= 0 && r <= k);
			if (r > 0) {
				memcpy(out + o, b, (size_t)r);
				o += r;
			}
		}
		CHECK(o == (long)strlen(want) && !memcmp(out, want, (size_t)o) && c.done);
	}
	{
		CHUNKS c;
		unsigned char b[16];
		chunks_init(&c);
		memcpy(b, "zz\r\n", 4);
		CHECK(chunks_feed(&c, b, 4) == -1);	/* (not a size) */
		chunks_init(&c);
		memcpy(b, "2\r\nabX", 7);
		CHECK(chunks_feed(&c, b, 7) == -1);	/* (no CRLF after its bytes) */
		chunks_init(&c);
		memcpy(b, "fffffffff\r\n", 11);
		CHECK(chunks_feed(&c, b, 11) == -1);	/* (too big a chunk) */
	}
}

/* a header line over 1 KB passed over (a CDN's Content-Security-Policy);
   a status line over 1 KB still refused */
static void long_lines(void)
{
	static char a[4096];
	SHEAD h;
	long n;
	strcpy(a, "HTTP/1.1 200 OK\r\ncontent-security-policy: ");
	memset(a + strlen(a), 'x', 2400);
	strcat(a, "\r\nContent-Type: audio/mpeg\r\nicy-metaint: 16000\r\n\r\nAUDIO");
	n = shead_parse(a, (long)strlen(a), &h);
	CHECK(n == (long)(strstr(a, "AUDIO") - a) && h.status == 200 && h.metaint == 16000 &&
	      !strcmp(h.type, "audio/mpeg"));
	CHECK(shead_parse(a, 1500, &h) == 0);	/* (the long line not ended yet: more wanted) */
	strcpy(a, "HTTP/1.1 200 ");
	memset(a + strlen(a), 'x', 1100);
	strcat(a, "\r\n\r\n");
	CHECK(shead_parse(a, (long)strlen(a), &h) == -1);
}

int main(void)
{
	chunks();
	long_lines();
	urls();
	heads();
	icy();
	pls();
	if (fails)
		return 1;
	printf("stream: URLs, heads (long lines), ICY metadata at every cut, PLS, chunks at every cut: ok\n");
	return 0;
}
