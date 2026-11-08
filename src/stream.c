/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * stream.c - web radio and audio streams' parsing (stream.h). Nothing here
 * allocates or trusts a length from the network: every one is checked
 * against what came and what fits.
 */
#include <string.h>
#include "stream.h"

static int low(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* s starts with p, case ignored */
static int starts(const char *s, const char *p)
{
	while (*p)
		if (low((unsigned char)*s++) != low((unsigned char)*p++))
			return 0;
	return 1;
}

int surl_is(const char *s)
{
	return starts(s, "http://") || starts(s, "https://");
}

int surl_parse(const char *u, SURL *o)
{
	const char *p, *h;
	long n, port = 0;
	memset(o, 0, sizeof(*o));
	if (starts(u, "http://"))
		p = u + 7;
	else if (starts(u, "https://")) {
		p = u + 8;
		o->tls = 1;
	} else
		return 0;
	for (h = u; *h; h++)
		if ((unsigned char)*h <= ' ' || *h == 127)
			return 0;		/* (spaces and control bytes: not a URL) */
	h = p;
	if (*h == '[')
		return 0;			/* (IPv6: no stack of the Atari's takes it) */
	while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#' && *p != '@')
		p++;
	if (*p == '@')
		return 0;			/* (a login in the URL: not taken) */
	n = p - h;
	if (n <= 0 || n >= SURL_HOST)
		return 0;
	memcpy(o->host, h, (size_t)n);
	o->host[n] = 0;
	if (*p == ':') {
		p++;
		if (*p < '0' || *p > '9')
			return 0;
		while (*p >= '0' && *p <= '9') {
			port = port * 10 + (*p++ - '0');
			if (port > 65535)
				return 0;
		}
		if (port == 0 || (*p && *p != '/' && *p != '?' && *p != '#'))
			return 0;
	}
	o->port = (unsigned short)(port ? port : o->tls ? 443 : 80);
	if (*p == '#' || !*p)
		strcpy(o->path, "/");
	else {
		const char *e = p;
		while (*e && *e != '#')
			e++;			/* (the fragment is the browser's, not sent) */
		n = e - p;
		if (*p == '?') {
			if (n + 1 >= SURL_PATH)
				return 0;
			o->path[0] = '/';
			memcpy(o->path + 1, p, (size_t)n);
			o->path[n + 1] = 0;
		} else {
			if (n >= SURL_PATH)
				return 0;
			memcpy(o->path, p, (size_t)n);
			o->path[n] = 0;
		}
	}
	return 1;
}

/* the value of a header line "name: value" at l (n bytes) into v (vmax
   with its 0), spaces around it cut. 1 if l is that header */
static int header(const char *l, long n, const char *name, char *v, long vmax)
{
	long k = (long)strlen(name), i, e;
	if (n <= k || !starts(l, name) || l[k] != ':')
		return 0;
	i = k + 1;
	while (i < n && (l[i] == ' ' || l[i] == '\t'))
		i++;
	e = n;
	while (e > i && (l[e - 1] == ' ' || l[e - 1] == '\t'))
		e--;
	if (e - i >= vmax)
		e = i + vmax - 1;		/* (cut to fit: a name, a type) */
	memcpy(v, l + i, (size_t)(e - i));
	v[e - i] = 0;
	return 1;
}

/* a decimal number of at most 9 digits at s, else -1 */
static long number(const char *s)
{
	long v = 0;
	int d = 0;
	if (*s < '0' || *s > '9')
		return -1;
	while (*s >= '0' && *s <= '9') {
		if (++d > 9)
			return -1;
		v = v * 10 + (*s++ - '0');
	}
	return *s ? -1 : v;
}

long shead_parse(const char *t, long n, SHEAD *h)
{
	long i = 0, first = 1;
	char v[SURL_PATH];
	memset(h, 0, sizeof(*h));
	while (i < n) {
		long s = i, e, len;
		while (i < n && t[i] != '\n')
			i++;
		if (i >= n)
			return i - s > 1024 ? -1 : 0;	/* (the line not ended yet) */
		e = i++;			/* (past the LF) */
		if (e > s && t[e - 1] == '\r')
			e--;
		len = e - s;
		if (len > 1024)
			return -1;
		if (first) {
			/* "ICY 200 OK", "HTTP/1.0 200 OK", "HTTP/1.1 302 Found" */
			const char *l = t + s;
			long k;
			if (len >= 4 && starts(l, "ICY "))
				k = 4;
			else if (len >= 9 && starts(l, "HTTP/1.") && l[8] == ' ')
				k = 9;
			else
				return -1;
			if (k + 3 > len || l[k] < '1' || l[k] > '5' || l[k + 1] < '0' || l[k + 1] > '9' ||
			    l[k + 2] < '0' || l[k + 2] > '9' || (k + 3 < len && l[k + 3] != ' '))
				return -1;
			h->status = (l[k] - '0') * 100 + (l[k + 1] - '0') * 10 + (l[k + 2] - '0');
			first = 0;
			continue;
		}
		if (len == 0)
			return i;		/* the blank line: the audio follows */
		if (header(t + s, len, "icy-metaint", v, sizeof(v))) {
			long m = number(v);
			if (m < 1 || m > 65535)
				return -1;
			h->metaint = m;
		} else if (header(t + s, len, "icy-br", v, sizeof(v))) {
			long b = number(v);	/* ("128", or "128,128" from some: the first) */
			char *c = strchr(v, ',');
			if (c) {
				*c = 0;
				b = number(v);
			}
			h->kbps = b > 0 && b < 10000 ? b : 0;
		} else if (header(t + s, len, "icy-name", h->name, sizeof(h->name)))
			;
		else if (header(t + s, len, "content-type", v, sizeof(v))) {
			long k;
			for (k = 0; v[k] && v[k] != ';' && v[k] != ' ' && k < SHEAD_TYPE - 1; k++)
				h->type[k] = (char)low((unsigned char)v[k]);
			h->type[k] = 0;
		} else if (header(t + s, len, "location", v, sizeof(v)))
			strcpy(h->location, v);
		else if (header(t + s, len, "transfer-encoding", v, sizeof(v)))
			h->chunked = starts(v, "chunked");
	}
	return 0;
}

int stype(const char *type)
{
	static const struct {
		const char *t;
		int k;
	} m[] = {
		{"audio/mpeg", STYPE_MP3}, {"audio/mp3", STYPE_MP3}, {"audio/x-mpeg", STYPE_MP3},
		{"audio/mpeg3", STYPE_MP3}, {"audio/x-mp3", STYPE_MP3},
		{"audio/aac", STYPE_AAC}, {"audio/aacp", STYPE_AAC}, {"audio/x-aac", STYPE_AAC},
		{"audio/mp4", STYPE_AAC}, {"audio/ogg", STYPE_OGG}, {"application/ogg", STYPE_OGG},
		{"audio/x-scpls", STYPE_PLAYLIST}, {"audio/x-mpegurl", STYPE_PLAYLIST},
		{"audio/mpegurl", STYPE_PLAYLIST}, {"application/vnd.apple.mpegurl", STYPE_PLAYLIST},
		{"application/x-mpegurl", STYPE_PLAYLIST},
	};
	unsigned i;
	for (i = 0; i < sizeof(m) / sizeof(m[0]); i++)
		if (!strcmp(type, m[i].t))
			return m[i].k;
	return STYPE_UNKNOWN;
}

void icy_init(ICY *s, long metaint)
{
	memset(s, 0, sizeof(*s));
	s->metaint = metaint;
	s->left = metaint;
	s->mlen = -1;			/* (the length byte is next once left is 0) */
}

/* StreamTitle='...'; out of the block: the title's quote ends at "';" (a
   title may hold a ' of its own) or at the block's end */
static void take_title(ICY *s)
{
	const char *m = s->meta, *p = strstr(m, "StreamTitle='"), *e;
	char t[ICY_TITLE];
	long n;
	if (!p)
		return;
	p += 13;
	e = strstr(p, "';");
	if (!e)
		e = p + strlen(p);
	n = e - p;
	if (n >= ICY_TITLE)
		n = ICY_TITLE - 1;
	memcpy(t, p, (size_t)n);
	t[n] = 0;
	{
		long i;			/* (control bytes out: the bar draws what's left) */
		for (i = 0; i < n; i++)
			if ((unsigned char)t[i] < ' ')
				t[i] = ' ';
	}
	if (strcmp(t, s->title)) {
		strcpy(s->title, t);
		s->new_title = 1;
	}
}

long icy_feed(ICY *s, unsigned char *b, long n)
{
	long r = 0, w = 0;
	if (s->metaint <= 0)
		return n;
	while (r < n) {
		if (s->left > 0) {
			long k = n - r < s->left ? n - r : s->left;
			if (w != r)
				memmove(b + w, b + r, (size_t)k);
			w += k;
			r += k;
			s->left -= k;
			continue;
		}
		if (s->mlen < 0) {		/* the length byte */
			s->mlen = b[r++] * 16;
			s->mgot = 0;
			if (s->mlen == 0) {
				s->mlen = -1;
				s->left = s->metaint;
			}
			continue;
		}
		{
			long k = n - r < s->mlen - s->mgot ? n - r : s->mlen - s->mgot;
			memcpy(s->meta + s->mgot, b + r, (size_t)k);
			s->mgot += (int)k;
			r += k;
			if (s->mgot == s->mlen) {
				s->meta[s->mlen] = 0;
				take_title(s);
				s->mlen = -1;
				s->left = s->metaint;
			}
		}
	}
	return w;
}

/* the line at t[i..] (to n): its start and length, i moved past it */
static long line(const char *t, long n, long *i, const char **l)
{
	long s = *i, e;
	while (*i < n && t[*i] != '\n')
		(*i)++;
	e = *i;
	if (*i < n)
		(*i)++;
	if (e > s && t[e - 1] == '\r')
		e--;
	*l = t + s;
	return e - s;
}

/* "FileN=" or "TitleN=" at l: N (1..9999), the value's start in *v */
static long pls_key(const char *l, long len, const char *key, const char **v)
{
	long k = (long)strlen(key), i = k, num = 0;
	if (len <= k || !starts(l, key))
		return 0;
	while (i < len && l[i] >= '0' && l[i] <= '9' && i - k < 4)
		num = num * 10 + (l[i++] - '0');
	if (i == k || i >= len || l[i] != '=' || num == 0)
		return 0;
	*v = l + i + 1;
	return num;
}

int pls_read(const char *t, long n, void (*add)(const char *url, const char *title, void *ctx), void *ctx)
{
	long i = 0, given = 0;
	if (n >= 3 && (unsigned char)t[0] == 0xef && (unsigned char)t[1] == 0xbb && (unsigned char)t[2] == 0xbf)
		i = 3;			/* (a UTF-8 BOM) */
	while (i < n) {
		const char *l, *v;
		long len = line(t, n, &i, &l), num = pls_key(l, len, "File", &v), vl;
		char url[SURL_PATH], title[ICY_TITLE];
		long j = 0;
		if (!num)
			continue;
		vl = len - (v - l);
		if (vl <= 0 || vl >= SURL_PATH)
			continue;
		memcpy(url, v, (size_t)vl);
		url[vl] = 0;
		title[0] = 0;
		while (j < n) {		/* its TitleN, wherever it is */
			const char *tl, *tv;
			long tlen = line(t, n, &j, &tl);
			if (pls_key(tl, tlen, "Title", &tv) == num) {
				long k = tlen - (tv - tl);
				if (k >= ICY_TITLE)
					k = ICY_TITLE - 1;
				if (k > 0) {
					memcpy(title, tv, (size_t)k);
					title[k] = 0;
				}
				break;
			}
		}
		add(url, title, ctx);
		given++;
	}
	return (int)given;
}
