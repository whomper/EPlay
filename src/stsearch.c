/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * stsearch.c - Find station's addresses and answers (stsearch.h). The JSON
 * reader takes only what it needs from each object (name, url_resolved or
 * url, countrycode, codec, bitrate) and passes over the rest, nested or
 * not; it never reads past len.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "charset.h"
#include "stsearch.h"

/* q %-encoded onto out at k: the new length, or -1 */
static int enc(char *out, int k, int n, const char *q)
{
	static const char hex[] = "0123456789ABCDEF";
	for (; *q; q++) {
		unsigned char c = (unsigned char)*q;
		if (k >= n - 4)
			return -1;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
		    c == '_' || c == '~')
			out[k++] = (char)c;
		else {
			out[k++] = '%';
			out[k++] = hex[c >> 4];
			out[k++] = hex[c & 15];
		}
	}
	out[k] = 0;
	return k;
}

/* "&key=value" (value %-encoded as UTF-8) when value is given */
static int arg(char *out, int k, int n, const char *key, const char *value)
{
	char u[200];
	long ul;
	char *utf;
	if (k < 0 || !value || !value[0])
		return k;
	k += snprintf(out + k, (size_t)(n - k), "%s%s=", strchr(out, '?') ? "&" : "?", key);
	if (k >= n)
		return -1;
	utf = atari_to_utf8(value, (long)strlen(value), &ul);
	if (utf) {
		snprintf(u, sizeof(u), "%.*s", (int)(ul < (long)sizeof(u) ? ul : (long)sizeof(u) - 1), utf);
		k = enc(out, k, n, u);
		free(utf);
	} else
		k = enc(out, k, n, value);
	return k;
}

int ss_url_direct(char *out, int n, const char *api, const SSQUERY *q)
{
	static const char *const order[] = { "votes&reverse=true", "clickcount&reverse=true", "name" };
	int k = snprintf(out, (size_t)n, "%s/json/stations/search?hidebroken=true&codec=MP3&limit=%d&order=%s", api,
			 SS_MAX, order[q->sort >= 0 && q->sort <= 2 ? q->sort : 0]);
	if (k >= n)
		return -1;
	k = arg(out, k, n, "name", q->q);
	k = arg(out, k, n, "tag", q->tag);
	k = arg(out, k, n, "countrycode", q->cc);
	k = arg(out, k, n, "language", q->lang);
	return k;
}

int ss_url_gateway(char *out, int n, const char *gate, const SSQUERY *q)
{
	static const char *const sort[] = { "votes", "clicks", "name" };
	int k = snprintf(out, (size_t)n, "%s/search?n=%d&sort=%s", gate, SS_MAX,
			 sort[q->sort >= 0 && q->sort <= 2 ? q->sort : 0]);
	if (k >= n)
		return -1;
	k = arg(out, k, n, "q", q->q);
	k = arg(out, k, n, "tag", q->tag);
	k = arg(out, k, n, "country", q->cc);
	k = arg(out, k, n, "language", q->lang);
	return k;
}

/* ---- text into the fields ---- */

/* a code point into out (n bytes), as Atari text; at k */
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

/* the UTF-8 bytes p..e as Atari text into out (n bytes) */
static void utf8_field(const char *p, const char *e, char *out, int n)
{
	int k = 0;
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

/* an ASCII field (a URL, a code) as it is, n bytes at most; 0 if it
   doesn't fit or has a control character */
static int plain(const char *p, const char *e, char *out, int n)
{
	int k = 0;
	for (; p < e; p++) {
		if ((unsigned char)*p < 33 || k >= n - 1)
			return 0;
		out[k++] = *p;
	}
	out[k] = 0;
	return 1;
}

static int is_url(const char *u)
{
	return !strncmp(u, "http://", 7) || !strncmp(u, "https://", 8);
}

int ss_parse_lines(const char *text, long len, SSTATION *st, int max)
{
	const char *p = text, *e = text + len;
	int n = 0;
	while (p < e && n < max) {
		const char *f[5], *fe[5], *le = p;
		int i;
		long kb = 0;
		while (le < e && *le != '\n')
			le++;
		if (le == e)
			break;			/* (cut short: the line left out) */
		for (i = 0; i < 5; i++) {
			f[i] = p;
			while (p < le && *p != '\t')
				p++;
			fe[i] = p;
			if (p < le)
				p++;
		}
		if (fe[4] > f[4] && fe[4][-1] == '\r')
			fe[4]--;
		p = le + 1;
		memset(&st[n], 0, sizeof(st[n]));
		utf8_field(f[0], fe[0], st[n].name, sizeof(st[n].name));
		plain(f[1], fe[1], st[n].cc, sizeof(st[n].cc));
		plain(f[2], fe[2], st[n].codec, sizeof(st[n].codec));
		for (; f[3] < fe[3] && *f[3] >= '0' && *f[3] <= '9' && kb < 10000; f[3]++)
			kb = kb * 10 + (*f[3] - '0');
		st[n].kbps = (int)kb;
		if (plain(f[4], fe[4], st[n].url, sizeof(st[n].url)) && is_url(st[n].url) && st[n].name[0])
			n++;
	}
	return n;
}

/* ---- JSON ---- */

typedef struct {
	const char *p, *e;
	int bad;
} JS;

static void ws(JS *j)
{
	while (j->p < j->e && (*j->p == ' ' || *j->p == '\t' || *j->p == '\r' || *j->p == '\n'))
		j->p++;
}

static int hexv(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* a string at p (its opening quote): into out (n bytes, raw UTF-8 with the
   escapes undone, 0 for none: passed over) */
static int jstring(JS *j, char *out, int n)
{
	int k = 0;
	if (j->p >= j->e || *j->p != '"')
		return j->bad = 1, 0;
	j->p++;
	while (j->p < j->e && *j->p != '"') {
		unsigned long u = (unsigned char)*j->p++;
		if (u == '\\') {
			int c;
			if (j->p >= j->e)
				break;
			c = *j->p++;
			if (c == 'u') {
				int i, h;
				u = 0;
				for (i = 0; i < 4; i++) {
					if (j->p >= j->e || (h = hexv(*j->p)) < 0)
						return j->bad = 1, 0;
					u = u << 4 | (unsigned long)h;
					j->p++;
				}
				if (u >= 0xd800 && u < 0xdc00 && j->e - j->p >= 6 && j->p[0] == '\\' && j->p[1] == 'u') {
					unsigned long lo = 0;
					int ok = 1;
					for (i = 2; i < 6; i++) {
						if ((h = hexv(j->p[i])) < 0)
							ok = 0;
						lo = lo << 4 | (unsigned long)(h < 0 ? 0 : h);
					}
					if (ok && lo >= 0xdc00 && lo < 0xe000) {
						u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
						j->p += 6;
					}
				}
			} else
				u = c == 'n' || c == 't' || c == 'r' || c == 'b' || c == 'f' ? ' ' : (unsigned long)c;
			/* (as UTF-8 again, for the one way into Atari text) */
			if (out) {
				char b[4];
				int m = u < 0x80 ? (b[0] = (char)u, 1) :
				    u < 0x800 ? (b[0] = (char)(0xc0 | u >> 6), b[1] = (char)(0x80 | (u & 63)), 2) :
				    u < 0x10000 ? (b[0] = (char)(0xe0 | u >> 12), b[1] = (char)(0x80 | ((u >> 6) & 63)),
						   b[2] = (char)(0x80 | (u & 63)), 3) :
						  (b[0] = (char)(0xf0 | u >> 18), b[1] = (char)(0x80 | ((u >> 12) & 63)),
						   b[2] = (char)(0x80 | ((u >> 6) & 63)), b[3] = (char)(0x80 | (u & 63)), 4);
				int i;
				for (i = 0; i < m && k < n - 1; i++)
					out[k++] = b[i];
			}
			continue;
		}
		if (out && k < n - 1)
			out[k++] = (char)u;
	}
	if (j->p >= j->e)
		return j->bad = 1, 0;
	j->p++;
	if (out)
		out[k] = 0;
	return k;
}

/* any value, passed over (nested to 64 deep at most) */
static void jskip(JS *j, int depth)
{
	ws(j);
	if (j->p >= j->e || depth > 64) {
		j->bad = 1;
		return;
	}
	if (*j->p == '"') {
		jstring(j, 0, 0);
		return;
	}
	if (*j->p == '{' || *j->p == '[') {
		char close = *j->p == '{' ? '}' : ']';
		j->p++;
		ws(j);
		if (j->p < j->e && *j->p == close) {
			j->p++;
			return;
		}
		while (!j->bad) {
			if (close == '}') {
				ws(j);
				jstring(j, 0, 0);
				ws(j);
				if (j->p >= j->e || *j->p != ':') {
					j->bad = 1;
					return;
				}
				j->p++;
			}
			jskip(j, depth + 1);
			ws(j);
			if (j->p >= j->e) {
				j->bad = 1;
				return;
			}
			if (*j->p == ',') {
				j->p++;
				continue;
			}
			if (*j->p == close) {
				j->p++;
				return;
			}
			j->bad = 1;
			return;
		}
		return;
	}
	while (j->p < j->e && *j->p != ',' && *j->p != '}' && *j->p != ']' && *j->p != ' ' && *j->p != '\n' &&
	       *j->p != '\r' && *j->p != '\t')
		j->p++;			/* (a number, true, false, null) */
}

int ss_parse_json(const char *text, long len, SSTATION *st, int max)
{
	JS j;
	int n = 0;
	j.p = text;
	j.e = text + len;
	j.bad = 0;
	ws(&j);
	if (j.p >= j.e || *j.p != '[')
		return 0;
	j.p++;
	ws(&j);
	if (j.p < j.e && *j.p == ']')
		return 0;
	while (!j.bad && n < max) {
		char key[16], name[160], url[SS_URL + 8], url2[SS_URL + 8], val[24];
		long kb = 0;
		ws(&j);
		if (j.p >= j.e || *j.p != '{') {
			j.bad = 1;
			break;
		}
		j.p++;
		name[0] = url[0] = url2[0] = 0;
		memset(&st[n], 0, sizeof(st[n]));
		ws(&j);
		if (j.p < j.e && *j.p == '}')
			j.p++;
		else
			while (!j.bad) {
				ws(&j);
				jstring(&j, key, sizeof(key));
				ws(&j);
				if (j.bad || j.p >= j.e || *j.p != ':') {
					j.bad = 1;
					break;
				}
				j.p++;
				ws(&j);
				if (j.p < j.e && *j.p == '"' &&
				    (!strcmp(key, "name") || !strcmp(key, "url_resolved") || !strcmp(key, "url") ||
				     !strcmp(key, "countrycode") || !strcmp(key, "codec") || !strcmp(key, "bitrate"))) {
					char *dst = !strcmp(key, "name") ? name : !strcmp(key, "url_resolved") ? url :
						    !strcmp(key, "url") ? url2 : val;
					int sz = dst == name ? (int)sizeof(name) : dst == val ? (int)sizeof(val) : SS_URL + 8;
					jstring(&j, dst, sz);
					if (!strcmp(key, "countrycode"))
						plain(val, val + strlen(val), st[n].cc, sizeof(st[n].cc));
					else if (!strcmp(key, "codec"))
						plain(val, val + strlen(val), st[n].codec, sizeof(st[n].codec));
					else if (!strcmp(key, "bitrate")) {
						const char *v = val;
						for (kb = 0; *v >= '0' && *v <= '9' && kb < 10000; v++)
							kb = kb * 10 + (*v - '0');
					}
				} else if (!strcmp(key, "bitrate") && j.p < j.e && *j.p >= '0' && *j.p <= '9') {
					for (kb = 0; j.p < j.e && *j.p >= '0' && *j.p <= '9' && kb < 10000; j.p++)
						kb = kb * 10 + (*j.p - '0');
					jskip(&j, 1);	/* (anything left of it) */
				} else
					jskip(&j, 1);
				ws(&j);
				if (j.p >= j.e) {
					j.bad = 1;
					break;
				}
				if (*j.p == ',') {
					j.p++;
					continue;
				}
				if (*j.p == '}') {
					j.p++;
					break;
				}
				j.bad = 1;
			}
		if (j.bad)
			break;
		st[n].kbps = (int)kb;
		utf8_field(name, name + strlen(name), st[n].name, sizeof(st[n].name));
		if (!(url[0] && plain(url, url + strlen(url), st[n].url, SS_URL) && is_url(st[n].url)) &&
		    !(url2[0] && plain(url2, url2 + strlen(url2), st[n].url, SS_URL) && is_url(st[n].url)))
			st[n].url[0] = 0;
		if (st[n].url[0] && st[n].name[0])
			n++;
		ws(&j);
		if (j.p < j.e && *j.p == ',') {
			j.p++;
			continue;
		}
		break;
	}
	return n;
}
