/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * lib.c - the library's code the same for every platform (lib.h): the
 * platforms, the song lines, the text of the requests.
 */
#include <string.h>
#include "charset.h"
#include "lib.h"
#include "libtext.h"

static const LIB_PLATFORM *const platforms[] = { &lib_jellyfin, 0 };

const LIB_PLATFORM *lib_platform(int i)
{
	return i >= 0 && i < (int)(sizeof(platforms) / sizeof(platforms[0])) ? platforms[i] : 0;
}

const LIB_PLATFORM *lib_by_tag(const char *tag, int n)
{
	int i;
	for (i = 0; platforms[i]; i++)
		if ((int)strlen(platforms[i]->tag) == n && !memcmp(platforms[i]->tag, tag, (size_t)n))
			return platforms[i];
	return 0;
}

int lt_s(char *out, int k, int n, const char *s)
{
	if (k < 0)
		return -1;
	while (*s) {
		if (k >= n - 1)
			return out[k] = 0, -1;
		out[k++] = *s++;
	}
	out[k] = 0;
	return k;
}

int lt_n(char *out, int k, int n, long v)
{
	char b[12];
	int i = 11;
	b[i] = 0;
	if (v < 0)
		v = 0;
	do
		b[--i] = (char)('0' + v % 10);
	while ((v /= 10) && i > 0);
	return lt_s(out, k, n, b + i);
}

int lib_line(const LIB_PLATFORM *p, const char *id, char *out, int n)
{
	int k = lt_s(out, 0, n, p->tag);
	k = lt_s(out, k, n, ":");
	return lt_s(out, k, n, id);
}

const LIB_PLATFORM *lib_line_id(const char *line, char *id)
{
	const char *c = strchr(line, ':'), *s;
	const LIB_PLATFORM *p;
	int k = 0;
	if (!c || !(p = lib_by_tag(line, (int)(c - line))))
		return 0;
	for (s = c + 1; *s; s++) {
		/* (an id is letters, digits and '-' only: it goes into URLs as it is) */
		if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == '-') ||
		    k >= LIB_ID - 1)
			return 0;
		id[k++] = *s;
	}
	id[k] = 0;
	return k ? p : 0;
}

int lib_enc(char *out, int k, int n, const char *atari)
{
	static const char hex[] = "0123456789ABCDEF";
	if (k < 0)
		return -1;
	for (; *atari; atari++) {
		unsigned long u = atari_to_uni((unsigned char)*atari);
		unsigned char b[4];
		int m, i;
		if (u < 0x80)
			m = (b[0] = (unsigned char)u, 1);
		else if (u < 0x800)
			m = (b[0] = (unsigned char)(0xc0 | u >> 6), b[1] = (unsigned char)(0x80 | (u & 63)), 2);
		else
			m = (b[0] = (unsigned char)(0xe0 | u >> 12), b[1] = (unsigned char)(0x80 | ((u >> 6) & 63)),
			     b[2] = (unsigned char)(0x80 | (u & 63)), 3);
		for (i = 0; i < m; i++) {
			unsigned char c = b[i];
			if (k >= n - 4)
				return out[k] = 0, -1;
			if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
			    c == '.' || c == '_' || c == '~')
				out[k++] = (char)c;
			else {
				out[k++] = '%';
				out[k++] = hex[c >> 4];
				out[k++] = hex[c & 15];
			}
		}
	}
	out[k] = 0;
	return k;
}
