/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * netsrc.c - a station or stream as the queue's source (netsrc.h). HTTP/1.0
 * (no chunks), "Icy-MetaData: 1"; the answer's head read with stream.c's
 * shead_parse into a buffer of its own, what follows it the first audio.
 * A redirect (301, 302, 303, 307, 308) is followed, five at most; a
 * station list (.PLS, .M3U, or a Content-Type that says so) gives its
 * first station.
 */
#include <stdio.h>
#include <string.h>
#include "plat.h"
#include "conn.h"
#include "stream.h"
#include "netsrc.h"
#include "settings.h"
#include "elog.h"

extern const char app_version[];

#define HEAD_MAX 8192		/* the answer's head, and a station list's text */

static CONN *c;
static int net_ok = -1;
static ICY icy;
static SHEAD hd;
static char url_now[SURL_PATH], url_given[SURL_PATH];
static char name[SHEAD_NAME];
static char head[HEAD_MAX];
static long held_at, held_n;	/* audio come with the head, in head[] */

int ns_is_open(void)
{
	return c != 0;
}

void ns_close(void)
{
	if (c) {
		conn_close(c);
		c = 0;
	}
	held_n = 0;
}

/* a Location as a whole URL (it may be a path on the same server) */
static int resolve(const char *loc, const SURL *from, char *out, int omax)
{
	if (surl_is(loc))
		return snprintf(out, (size_t)omax, "%s", loc) < omax;
	if (loc[0] == '/')
		return snprintf(out, (size_t)omax, "%s://%s:%u%s", from->tls ? "https" : "http", from->host,
				from->port, loc) < omax;
	return 0;
}

/* a station list's first station */
static char first_url[SURL_PATH];

static void take_first(const char *u, const char *title, void *ctx)
{
	(void)title;
	(void)ctx;
	if (!first_url[0] && surl_is(u))
		snprintf(first_url, sizeof(first_url), "%s", u);
}

static void take_first_m3u(const char *line)
{
	if (!first_url[0] && surl_is(line))
		snprintf(first_url, sizeof(first_url), "%s", line);
}

/* the first http(s) line of an .M3U's text (m3u.c passes URLs over) */
static void m3u_first(const char *t, long n)
{
	long i = 0;
	while (i < n && !first_url[0]) {
		char l[SURL_PATH];
		long s = i, k;
		while (i < n && t[i] != '\n')
			i++;
		k = i - s;
		if (i < n)
			i++;
		while (k > 0 && (t[s + k - 1] == '\r' || t[s + k - 1] == ' '))
			k--;
		if (k <= 0 || k >= (long)sizeof(l))
			continue;
		memcpy(l, t + s, (size_t)k);
		l[k] = 0;
		take_first_m3u(l);
	}
}

static int ends_with(const char *s, const char *e)
{
	size_t a = strlen(s), b = strlen(e), i;
	const char *q = strchr(s, '?');
	if (q)
		a = (size_t)(q - s);
	if (a < b)
		return 0;
	for (i = 0; i < b; i++) {
		char x = s[a - b + i];
		if (x >= 'A' && x <= 'Z')
			x = (char)(x + 32);
		if (x != e[i])
			return 0;
	}
	return 1;
}

/* one request to u: 1 with the head read (hd), 2 when it was a redirect or
   a station list and u now holds where to go, 0 with err */
static int ask(char *u, char *err, int n)
{
	SURL s;
	char req[SURL_PATH + SURL_HOST + 200];
	long got = 0, h;
	if (!surl_parse(u, &s)) {
		snprintf(err, (size_t)n, "That isn't a station's address EPlay can open.");
		return 0;
	}
	ns_close();
	c = conn_open("HTTP", s.host, s.port, 0, s.tls ? SEC_TLS : SEC_PLAIN, err, n);
	if (!c) {
		/* (conn's own words to the log; the user's are the station's) */
		elog("the station %s:%u: %s", s.host, s.port, err[0] ? err : "no connection");
		snprintf(err, (size_t)n, "The station doesn't answer.");
		return 0;
	}
	snprintf(req, sizeof(req),
		 "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: EPlay/%s (Atari)\r\nAccept: */*\r\n"
		 "Icy-MetaData: 1\r\nConnection: close\r\n\r\n",
		 s.path, s.host, app_version);
	if (!conn_write(c, req, (long)strlen(req))) {
		snprintf(err, (size_t)n, "The station doesn't answer.");
		ns_close();
		return 0;
	}
	for (;;) {
		long r = conn_read(c, head + got, HEAD_MAX - got);
		if (r <= 0) {
			snprintf(err, (size_t)n, "The station closed the connection.");
			ns_close();
			return 0;
		}
		got += r;
		h = shead_parse(head, got, &hd);
		if (h < 0) {
			snprintf(err, (size_t)n, "The station's answer isn't one EPlay reads.");
			ns_close();
			return 0;
		}
		if (h > 0)
			break;
		if (got >= HEAD_MAX) {
			snprintf(err, (size_t)n, "The station's answer isn't one EPlay reads.");
			ns_close();
			return 0;
		}
	}
	if (hd.status >= 300 && hd.status < 400 && hd.location[0]) {
		char to[SURL_PATH];
		ns_close();
		if (!resolve(hd.location, &s, to, sizeof(to))) {
			snprintf(err, (size_t)n, "The station sent EPlay to an address it can't open.");
			return 0;
		}
		strcpy(u, to);
		return 2;
	}
	if (hd.status != 200) {
		snprintf(err, (size_t)n, hd.status == 404 ? "The station isn't there (404)." :
			 "The station answered %d.", hd.status);
		ns_close();
		return 0;
	}
	if (hd.chunked) {
		snprintf(err, (size_t)n, "The station's answer isn't one EPlay reads.");
		ns_close();
		return 0;
	}
	if (stype(hd.type) == STYPE_PLAYLIST || ends_with(u, ".pls") || ends_with(u, ".m3u") ||
	    ends_with(u, ".m3u8")) {
		/* a station list: its text (up to 8 KB), then its first station */
		long at = h;
		memmove(head, head + at, (size_t)(got - at));
		got -= at;
		while (got < HEAD_MAX) {
			long r = conn_read(c, head + got, HEAD_MAX - got);
			if (r <= 0)
				break;
			got += r;
		}
		ns_close();
		first_url[0] = 0;
		if (got >= 10 && !memcmp(head, "[playlist]", 10))
			pls_read(head, got, take_first, 0);
		else if (got >= 7 && !memcmp(head, "#EXTM3U", 7) && strstr(head, "#EXT-X-")) {
			snprintf(err, (size_t)n, "The station sends HLS: play it through the Gateway.");
			return 0;
		} else
			m3u_first(head, got);
		if (!first_url[0]) {
			snprintf(err, (size_t)n, "The station list holds no station EPlay can open.");
			return 0;
		}
		strcpy(u, first_url);
		return 2;
	}
	switch (stype(hd.type)) {
	case STYPE_AAC:
		snprintf(err, (size_t)n, "The station sends AAC: play it through the Gateway.");
		ns_close();
		return 0;
	case STYPE_OGG:
		snprintf(err, (size_t)n, "The station sends Ogg: play it through the Gateway.");
		ns_close();
		return 0;
	}
	held_at = h;
	held_n = got - h;
	icy_init(&icy, hd.metaint);
	held_n = icy_feed(&icy, (unsigned char *)head + held_at, held_n);
	if (hd.name[0])
		snprintf(name, sizeof(name), "%s", hd.name);
	else
		snprintf(name, sizeof(name), "%s", s.host);
	return 1;
}

/* through the Gateway (the Options menu): the station asked of it, which
   fetches it (https, AAC and HLS too) and sends plain MP3 the same way
   (phase 7c: its service). The station's URL as a query, its bytes
   outside A-Z a-z 0-9 - . _ ~ as %XX */
static int via_gateway(const char *url, char *out, int omax, char *err, int n)
{
	static const char hex[] = "0123456789ABCDEF";
	const char *g = settings_gate();
	long port = settings_gateport();
	int k;
	if (!g[0] || port <= 0) {
		snprintf(err, (size_t)n, "Give the Gateway's address and port in Settings.");
		return 0;
	}
	k = snprintf(out, (size_t)omax, "http://%s:%ld/stream?url=", g, port);
	for (; *url && k < omax - 4; url++) {
		unsigned char c = (unsigned char)*url;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
		    c == '.' || c == '_' || c == '~')
			out[k++] = (char)c;
		else {
			out[k++] = '%';
			out[k++] = hex[c >> 4];
			out[k++] = hex[c & 15];
		}
	}
	if (*url) {
		snprintf(err, (size_t)n, "That isn't a station's address EPlay can open.");
		return 0;
	}
	out[k] = 0;
	return 1;
}

static int open_url(const char *url, char *err, int n)
{
	int hops, gw = settings_gateway();
	err[0] = 0;
	if (net_ok < 0)
		net_ok = net_init();
	if (!net_ok) {
		snprintf(err, (size_t)n, "There's no network: EPlay needs STinG or MiNTnet.");
		return 0;
	}
	if (gw) {
		if (!via_gateway(url, url_now, sizeof(url_now), err, n))
			return 0;
		{
			int r = ask(url_now, err, n);
			if (!r && !conn_stop && !strcmp(err, "The station doesn't answer."))
				snprintf(err, (size_t)n, "The Gateway doesn't answer: is it on?");
			if (r == 1 && !hd.name[0])
				snprintf(name, sizeof(name), "%s", url);	/* (not the Gateway's own address) */
			return r == 1;
		}
	}
	snprintf(url_now, sizeof(url_now), "%s", url);
	for (hops = 0; hops < 6; hops++) {
		int r = ask(url_now, err, n);
		if (r != 2)
			return r;
	}
	snprintf(err, (size_t)n, "The station sent EPlay from one address to another too often.");
	return 0;
}

int ns_open(const char *url, char *err, int n)
{
	snprintf(url_given, sizeof(url_given), "%s", url);
	return open_url(url, err, n);
}

int ns_reopen(char *err, int n)
{
	return open_url(url_given, err, n);
}

long ns_read(void *buf, long max, int wait)
{
	long r;
	if (held_n > 0) {
		long k = held_n < max ? held_n : max;
		memcpy(buf, head + held_at, (size_t)k);
		held_at += k;
		held_n -= k;
		return k;
	}
	if (!c)
		return -1;
	for (;;) {
		if (!wait && !conn_pending(c))
			return 0;
		r = conn_read(c, buf, max);
		if (r <= 0)
			return -1;		/* (closed, timed out or stopped) */
		r = icy_feed(&icy, buf, r);
		if (r > 0 || !wait)
			return r;		/* (all metadata: nothing for now) */
	}
}

const char *ns_name(void)
{
	return name;
}

long ns_kbps(void)
{
	return hd.kbps;
}

const char *ns_title(int *changed)
{
	if (changed)
		*changed = icy.new_title;
	icy.new_title = 0;
	return icy.title;
}

const char *ns_title_peek(void)
{
	return icy.title;
}

const char *ns_url(void)
{
	return url_now;
}
