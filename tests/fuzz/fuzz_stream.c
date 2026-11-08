/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fuzz_stream.c - libFuzzer target for src/stream.c (make fuzz): the input
 * plays a station, its first byte choosing what it is read as (a URL, the
 * answer's head, ICY audio in pieces of the second byte's size, a .PLS;
 * with its bit 7, the ICY audio chunked first, as chunks_feed takes it);
 * built with replay.c instead, it runs tests/fuzz/found (make test).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "stream.h"

static void add(const char *url, const char *title, void *ctx)
{
	SURL u;
	(void)title;
	(void)ctx;
	surl_parse(url, &u);
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n);
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	if (n < 2 || n > 65536)
		return 0;
	switch (d[0] & 3) {
	case 0: {
		char *s = malloc(n);
		SURL u;
		memcpy(s, d + 1, n - 1);
		s[n - 1] = 0;
		surl_parse(s, &u);
		free(s);
		break;
	}
	case 1: {
		SHEAD h;
		char *s = malloc(n - 1);
		memcpy(s, d + 1, n - 1);
		if (shead_parse(s, (long)n - 1, &h) > 0)
			stype(h.type);
		free(s);
		break;
	}
	case 2: {
		static ICY s;
		static CHUNKS c;
		unsigned char *b = malloc(n);
		long piece = d[1] ? d[1] : 1, r = 2;
		int chunked = d[0] & 0x80;
		icy_init(&s, (long)((d[0] >> 2) & 31) + 1);
		chunks_init(&c);
		while (r < (long)n) {
			long k = (long)n - r < piece ? (long)n - r : piece, a = k;
			memcpy(b, d + r, (size_t)k);
			r += k;
			if (chunked) {
				a = chunks_feed(&c, b, k);
				if (a > k)
					abort();
				if (a < 0)
					break;
			}
			if (icy_feed(&s, b, a) > a)
				abort();
		}
		if (strlen(s.title) >= ICY_TITLE || strlen(s.url) >= ICY_URL || (s.url[0] && !surl_is(s.url)))
			abort();
		free(b);
		break;
	}
	default: {
		char *s = malloc(n - 1);
		memcpy(s, d + 1, n - 1);
		pls_read(s, (long)n - 1, add, NULL);
		free(s);
	}
	}
	return 0;
}
