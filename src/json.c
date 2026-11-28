/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * json.c - the JSON reader (json.h), from Find station's (stsearch.c),
 * made one that walks objects and arrays for the library.
 */
#include <string.h>
#include "charset.h"
#include "json.h"

void js_init(JS *j, const char *text, long len)
{
	j->p = text;
	j->e = text + (len > 0 ? len : 0);
	j->bad = !text;
	j->first = 0;
}

static void ws(JS *j)
{
	while (j->p < j->e && (*j->p == ' ' || *j->p == '\t' || *j->p == '\r' || *j->p == '\n'))
		j->p++;
}

int js_peek(JS *j)
{
	ws(j);
	return j->bad || j->p >= j->e ? 0 : (unsigned char)*j->p;
}

static int hexv(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static int utf8_out(unsigned long u, char *b)
{
	if (u < 0x80)
		return b[0] = (char)u, 1;
	if (u < 0x800)
		return b[0] = (char)(0xc0 | u >> 6), b[1] = (char)(0x80 | (u & 63)), 2;
	if (u < 0x10000)
		return b[0] = (char)(0xe0 | u >> 12), b[1] = (char)(0x80 | ((u >> 6) & 63)), b[2] = (char)(0x80 | (u & 63)), 3;
	b[0] = (char)(0xf0 | u >> 18);
	b[1] = (char)(0x80 | ((u >> 12) & 63));
	b[2] = (char)(0x80 | ((u >> 6) & 63));
	b[3] = (char)(0x80 | (u & 63));
	return 4;
}

/* 4 hex digits at p: the value, or -1 */
static long hex4(const char *p, const char *e)
{
	long u = 0;
	int i, h;
	if (e - p < 4)
		return -1;
	for (i = 0; i < 4; i++) {
		if ((h = hexv(p[i])) < 0)
			return -1;
		u = u << 4 | h;
	}
	return u;
}

/* the string at p (its quote): into out (n bytes, UTF-8, escapes undone;
   out 0: passed over); its length */
static int string(JS *j, char *out, int n)
{
	int k = 0;
	if (js_peek(j) != '"')
		return j->bad = 1, 0;
	j->p++;
	while (j->p < j->e && *j->p != '"') {
		unsigned long u = (unsigned char)*j->p++;
		if (u == '\\') {
			char b[4];
			int m, i;
			if (j->p >= j->e)
				break;
			u = (unsigned char)*j->p++;
			if (u == 'u') {
				long v = hex4(j->p, j->e), lo;
				if (v < 0)
					return j->bad = 1, 0;
				j->p += 4;
				if (v >= 0xd800 && v < 0xdc00 && j->e - j->p >= 6 && j->p[0] == '\\' && j->p[1] == 'u' &&
				    (lo = hex4(j->p + 2, j->e)) >= 0xdc00 && lo < 0xe000) {
					v = 0x10000 + ((v - 0xd800) << 10) + (lo - 0xdc00);
					j->p += 6;
				}
				u = (unsigned long)v;
			} else if (u == 'n' || u == 't' || u == 'r' || u == 'b' || u == 'f')
				u = ' ';
			if (!out)
				continue;
			m = utf8_out(u, b);
			if (k + m < n)
				for (i = 0; i < m; i++)
					out[k++] = b[i];
			continue;
		}
		if (out && k < n - 1)
			out[k++] = (char)u;
	}
	if (j->p >= j->e)
		return j->bad = 1, 0;
	j->p++;
	if (out && n > 0)
		out[k] = 0;
	return k;
}

static int take(JS *j, char c)
{
	if (js_peek(j) != c)
		return j->bad = 1, 0;
	j->p++;
	return 1;
}

/* the first member comes next (set at '{' and '[', cleared at a member
   and at a close, so the object or array around goes on with a ',') */
int js_obj(JS *j)
{
	return j->first = take(j, '{');
}

int js_arr(JS *j)
{
	return j->first = take(j, '[');
}

/* 1 if another member follows (its ',' taken), 0 at the close (taken) or
   a fault */
static int more(JS *j, char close)
{
	int c = js_peek(j), first = j->first;
	j->first = 0;
	if (j->bad)
		return 0;
	if (c == close) {
		j->p++;
		return 0;
	}
	if (!first) {
		if (c != ',')
			return j->bad = 1, 0;
		j->p++;
	}
	return js_peek(j) != 0 || (j->bad = 1, 0);
}

int js_key(JS *j, char *key, int n)
{
	if (!more(j, '}'))
		return 0;
	string(j, key, n);
	return take(j, ':');
}

int js_item(JS *j)
{
	return more(j, ']');
}

static void skip(JS *j, int depth)
{
	int c = js_peek(j);
	if (!c || depth > 64) {
		j->bad = 1;
		return;
	}
	if (c == '"') {
		string(j, 0, 0);
		return;
	}
	if (c == '{') {
		char key[2];
		js_obj(j);
		while (js_key(j, key, sizeof(key)))
			skip(j, depth + 1);
		return;
	}
	if (c == '[') {
		js_arr(j);
		while (js_item(j))
			skip(j, depth + 1);
		return;
	}
	if (c == ',' || c == '}' || c == ']' || c == ':') {
		j->bad = 1;
		return;
	}
	while (j->p < j->e && *j->p != ',' && *j->p != '}' && *j->p != ']' && *j->p != ' ' && *j->p != '\n' &&
	       *j->p != '\r' && *j->p != '\t')
		j->p++;			/* (a number, true, false, null) */
}

void js_skip(JS *j)
{
	skip(j, 0);
}

int js_str(JS *j, char *out, int n)
{
	if (n > 0)
		out[0] = 0;
	if (js_peek(j) != '"') {
		js_skip(j);
		return 0;
	}
	return string(j, out, n);
}

static int put(char *out, int k, int n, unsigned long u)
{
	char b[4];
	int m = uni_to_atari(u, b), i;
	if (u < 32)
		m = (b[0] = ' ', 1);	/* (a control character: a space) */
	if (k + m >= n)
		return k;
	for (i = 0; i < m; i++)
		out[k++] = b[i];
	return k;
}

void js_utf8_atari(const char *p, const char *e, char *out, int n)
{
	int k = 0;
	if (n <= 0)
		return;
	while (p < e) {
		unsigned char c = (unsigned char)*p++;
		unsigned long u;
		int more = c < 0x80 ? 0 : c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : -1;
		if (more < 0)
			continue;
		u = more ? (unsigned long)(c & (0x3f >> more)) : c;
		while (more-- > 0 && p < e)
			u = u << 6 | ((unsigned char)*p++ & 0x3f);
		k = put(out, k, n, u);
	}
	out[k] = 0;
	while (k > 0 && out[k - 1] == ' ')
		out[--k] = 0;
}

void js_text(JS *j, char *out, int n)
{
	char raw[256];
	int m = js_str(j, raw, sizeof(raw));
	js_utf8_atari(raw, raw + m, out, n);
}

long js_num(JS *j, int drop)
{
	const char *q;
	long v = 0;
	int neg = 0, nd;
	int c = js_peek(j);
	if (c != '-' && (c < '0' || c > '9')) {
		js_skip(j);
		return 0;
	}
	if (c == '-')
		neg = 1, j->p++;
	q = j->p;
	while (j->p < j->e && *j->p >= '0' && *j->p <= '9')
		j->p++;
	nd = (int)(j->p - q) - drop;
	for (; nd > 0; nd--, q++)
		v = v > 214748363L ? 0x7fffffffL : v * 10 + (*q - '0');
	while (j->p < j->e && ((*j->p >= '0' && *j->p <= '9') || *j->p == '.' || *j->p == 'e' || *j->p == 'E' ||
				*j->p == '+' || *j->p == '-'))
		j->p++;			/* (a fraction or an exponent: dropped) */
	return neg ? 0 : v;
}

int js_bool(JS *j)
{
	int t = js_peek(j) == 't';
	js_skip(j);
	return t;
}

int js_put(char *out, int k, int n, const char *s)
{
	static const char hex[] = "0123456789abcdef";
	if (k < 0 || k >= n - 2)
		return -1;
	out[k++] = '"';
	for (; *s; s++) {
		unsigned long u = atari_to_uni((unsigned char)*s);
		char b[6];
		int m, i;
		if (u == '"' || u == '\\')
			m = (b[0] = '\\', b[1] = (char)u, 2);
		else if (u < 32)
			m = (b[0] = '\\', b[1] = 'u', b[2] = '0', b[3] = '0', b[4] = hex[u >> 4], b[5] = hex[u & 15], 6);
		else
			m = utf8_out(u, b);
		if (k + m >= n - 1)
			return -1;
		for (i = 0; i < m; i++)
			out[k++] = b[i];
	}
	out[k++] = '"';
	out[k] = 0;
	return k;
}
