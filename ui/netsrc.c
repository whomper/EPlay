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
#include "tls.h"
#include "tlsovl.h"
#include "gem.h"
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
static CHUNKS chk;		/* the answer's chunks, when it is chunked */

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
	unsigned long t0;
	if (!surl_parse(u, &s)) {
		snprintf(err, (size_t)n, "That isn't a station's address EPlay can open.");
		return 0;
	}
	ns_close();
	if (s.tls && !tls_available) {
		elog("the station %s:%u: https, and this build has no TLS", s.host, s.port);
		snprintf(err, (size_t)n, "The station needs a secure connection: play it through the Gateway.");
		return 0;
	}
	{
		/* (the path without its query: a station's tokens stay out of the log) */
		const char *q = strchr(s.path, '?');
		elog("the station: asking %s://%s:%u%.*s", s.tls ? "https" : "http", s.host, s.port,
		     (int)(q ? q - s.path : (long)strlen(s.path)), s.path);
	}
	t0 = pf_ms();
	c = conn_open("HTTP", s.host, s.port, 0, s.tls ? SEC_TLS : SEC_PLAIN, err, n);
	if (c)
		elog("the station: connected in %lu ms%s%s", pf_ms() - t0, s.tls ? ", " : "", conn_security(c));
	if (!c) {
		/* (conn's own words to the log; the user's are the station's) */
		elog("the station %s:%u: %s", s.host, s.port, err[0] ? err : "no connection");
		if (s.tls && !conn_stop && strstr(err, "TLS.OVL"))
			snprintf(err, (size_t)n, "A secure station needs TLS.OVL and CACERT.PEM beside EPlay.");
		else
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
			elog("the station: %s after %lu ms, %ld bytes of its answer (%s)",
			     r < 0 ? "no answer" : "closed", pf_ms() - t0, got, c->err[0] ? c->err : "closed");
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
		if (h > 0) {
			elog("the station: answered %d in %lu ms, %s%s%s", hd.status, pf_ms() - t0,
			     hd.type[0] ? hd.type : "no type", hd.metaint ? ", titles" : "", hd.chunked ? ", chunked" : "");
			break;
		}
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
	chunks_init(&chk);
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
		if (hd.chunked && (got = chunks_feed(&chk, (unsigned char *)head, got)) < 0) {
			snprintf(err, (size_t)n, "The station's answer isn't one EPlay reads.");
			return 0;
		}
		head[got < HEAD_MAX ? got : HEAD_MAX - 1] = 0;
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
	if (hd.chunked && (held_n = chunks_feed(&chk, (unsigned char *)head + held_at, held_n)) < 0) {
		snprintf(err, (size_t)n, "The station's answer isn't one EPlay reads.");
		ns_close();
		return 0;
	}
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

/* a Direct https station: TLS.OVL, CACERT.PEM and what TLS keeps between
   sessions (ROOTS.DAT, SEED.DAT, SIGS.DAT), all beside EPlay. The
   handshake's curves and RSA on the DSP when it is free (TLS.OVL locks it
   with the XBIOS for each job, the 68030 when the music holds it): a
   station connected before it plays has it. laut.fm's front hangs up on a
   client that hasn't asked within about 4 s, and P-256 on a 50 MHz 68030
   took 3.95 s of that (Erez's EPLAYNET.LOG, 8 Nov 2026); on the DSP 0.44 s */
static void tls_paths(void)
{
	static int done;
	const char *d = app_dir();
	if (done)
		return;
	done = 1;
	snprintf(tls_ovl_path, sizeof(tls_ovl_path), "%sTLS.OVL", d);
	snprintf(conn_cacert, sizeof(conn_cacert), "%sCACERT.PEM", d);
	snprintf(tls_roots_cache, sizeof(tls_roots_cache), "%sROOTS.DAT", d);
	snprintf(tls_seed_path, sizeof(tls_seed_path), "%sSEED.DAT", d);
	snprintf(tls_sigs_path, sizeof(tls_sigs_path), "%sSIGS.DAT", d);
	tls_file_stamp = pf_stamp;
	tls_read_at = pf_read_at;
	tls_file_info = pf_info;
	tls_ovl_dsp(0, 1);
	tls_ovl_dsp(1, 1);
	/* the connections' own steps (TLS's included), beside EPLAY.LOG */
	snprintf(conn_logfile, sizeof(conn_logfile), "%sEPLAYNET.LOG", d);
}

static int open_url(const char *url, char *err, int n)
{
	int hops, gw = settings_gateway();
	err[0] = 0;
	tls_paths();
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
		if (hd.chunked) {
			r = chunks_feed(&chk, buf, r);
			if (r < 0 || (chk.done && !r))
				return -1;	/* (its framing broken, or its last chunk) */
		}
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
