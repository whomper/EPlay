/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sart.c - a station's picture in the bar's art square (sart.h). Some
 * stations name one with each title (ICY's StreamUrl: a song's cover at
 * Radio Paradise, the station's logo at SomaFM). Erez, 8 Nov 2026: "if it
 * is direct connection the Atari does all, else the gateway":
 *   - Direct: EPlay fetches the picture itself (http or https, through
 *     TLS.OVL; up to three redirects; 512 KB at most) and the 68030
 *     unpacks it (art.c: the DSP plays the station meanwhile);
 *   - Through the Gateway: GET /art?url=... of its radio service, which
 *     fetches it and gives back a 64 x 64 JPEG, quick to unpack.
 * Each picture is kept in art.c's cache under its address, so a cover
 * comes again from the disk. A fetch connects and waits the way a station
 * does (conn.c), its waits feeding the music (play_feed) and redrawing
 * the window; it runs from the main loop, never inside play_step.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "plat.h"
#include "conn.h"
#include "stream.h"
#include "prefs.h"
#include "art.h"
#include "play.h"
#include "elog.h"
#include "busy.h"
#include "sart.h"

extern const char app_version[];

#define PIC_MAX (512L * 1024)		/* a bigger picture is left out */

static char want[ICY_URL], done_url[ICY_URL + 8];
static int logo_st;
static void (*on_message)(const short *msg);

void sart_init(void (*msg)(const short *msg))
{
	on_message = msg;
}

void sart_reset(void)
{
	want[0] = done_url[0] = 0;
	logo_st = 0;
}

void sart_want(const char *url)
{
	snprintf(want, sizeof(want), "%s", url);
}

static unsigned long hash(const char *s)
{
	unsigned long h = 2166136261UL;
	while (*s)
		h = (h ^ (unsigned char)*s++) * 16777619UL;
	return h;
}

/* (the waits of a fetch: the music fed, the window redrawn; 30 s at most
   for the whole of it) */
static int feeding;
static unsigned long t_fetch;
static void idle(void)
{
	if (pf_ms() - t_fetch > 30000UL)
		conn_stop = 1;
	if (!feeding) {
		feeding = 1;
		play_feed();
		feeding = 0;
	}
	busy_idle();
}

/* one GET: 1 with the bytes in *data (*len), 2 with a redirect's address
   in u, 0 with why */
static int get(char *u, unsigned char **data, long *len, char *why, int wn)
{
	SURL s;
	SHEAD hd;
	CHUNKS ck;
	CONN *c;
	static char head[4096];
	char req[SURL_PATH + SURL_HOST + 160];
	long got = 0, h = 0, n = 0, cap = 0;
	unsigned char *b = 0;
	if (!surl_parse(u, &s)) {
		snprintf(why, (size_t)wn, "not an address EPlay opens");
		return 0;
	}
	c = conn_open("PIC", s.host, s.port, 0, s.tls ? SEC_TLS : SEC_PLAIN, why, wn);
	if (!c)
		return 0;
	c->timeout_ms = 10000;
	snprintf(req, sizeof(req),
		 "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: EPlay/%s (Atari)\r\nAccept: image/jpeg, image/png\r\n"
		 "Connection: close\r\n\r\n", s.path, s.host, app_version);
	if (!conn_write(c, req, (long)strlen(req))) {
		snprintf(why, (size_t)wn, "the request not sent");
		conn_close(c);
		return 0;
	}
	while (!h) {
		long r = conn_read(c, head + got, (long)sizeof(head) - got);
		if (r <= 0 || (got += r) >= (long)sizeof(head)) {
			snprintf(why, (size_t)wn, "no answer");
			conn_close(c);
			return 0;
		}
		h = shead_parse(head, got, &hd);
		if (h < 0) {
			snprintf(why, (size_t)wn, "an answer EPlay doesn't read");
			conn_close(c);
			return 0;
		}
	}
	if (hd.status >= 300 && hd.status < 400 && hd.location[0]) {
		conn_close(c);
		if (surl_is(hd.location))
			snprintf(u, ICY_URL, "%s", hd.location);
		else if (hd.location[0] == '/' &&
			 snprintf(u, ICY_URL, "%s://%s:%u%s", s.tls ? "https" : "http", s.host, s.port, hd.location) <
				 ICY_URL)
			;
		else {
			snprintf(why, (size_t)wn, "sent to an address EPlay doesn't open");
			return 0;
		}
		return 2;
	}
	if (hd.status != 200) {
		snprintf(why, (size_t)wn, "answered %d", hd.status);
		conn_close(c);
		return 0;
	}
	if (hd.type[0] && strncmp(hd.type, "image/", 6)) {
		snprintf(why, (size_t)wn, "not a picture (%s)", hd.type);
		conn_close(c);
		return 0;
	}
	chunks_init(&ck);
	cap = 32768;
	b = malloc((size_t)cap);
	if (!b) {
		snprintf(why, (size_t)wn, "no memory");
		conn_close(c);
		return 0;
	}
	n = got - h;
	memcpy(b, head + h, (size_t)n);
	if (hd.chunked && (n = chunks_feed(&ck, b, n)) < 0)
		goto bad;
	for (;;) {
		long r;
		if (n + 4096 > cap) {
			unsigned char *nb;
			if (cap >= PIC_MAX) {
				snprintf(why, (size_t)wn, "over %ld KB, left out", PIC_MAX / 1024);
				free(b);
				conn_close(c);
				return 0;
			}
			nb = realloc(b, (size_t)(cap * 2));
			if (!nb) {
				snprintf(why, (size_t)wn, "no memory");
				free(b);
				conn_close(c);
				return 0;
			}
			b = nb;
			cap *= 2;
		}
		r = conn_read(c, (char *)b + n, 4096);
		if (r == 0)
			break;			/* (the end) */
		if (r < 0) {
			snprintf(why, (size_t)wn, "%s", conn_stop ? "stopped" : "the server stopped sending");
			free(b);
			conn_close(c);
			return 0;
		}
		if (hd.chunked && (r = chunks_feed(&ck, b + n, r)) < 0)
			goto bad;
		n += r;
		if (hd.chunked && ck.done)
			break;
	}
	conn_close(c);
	*data = b;
	*len = n;
	return 1;
bad:
	snprintf(why, (size_t)wn, "an answer EPlay doesn't read");
	free(b);
	conn_close(c);
	return 0;
}

/* the address q %-encoded onto out at k (omax in all): its new length, or
   -1 when it doesn't fit */
static int enc(char *out, int k, int omax, const char *q)
{
	static const char hex[] = "0123456789ABCDEF";
	for (; *q; q++) {
		unsigned char ch = (unsigned char)*q;
		if (k >= omax - 4)
			return -1;
		if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' ||
		    ch == '.' || ch == '_' || ch == '~')
			out[k++] = (char)ch;
		else {
			out[k++] = '%';
			out[k++] = hex[ch >> 4];
			out[k++] = hex[ch & 15];
		}
	}
	out[k] = 0;
	return k;
}

/* the Gateway's address and port (Settings), whatever the connection:
   0 when none is given */
static int gate_base(char *out, int omax, const char *what)
{
	const char *g = settings_gate();
	long port = settings_gateport();
	if (!g[0] || port <= 0)
		return 0;
	return snprintf(out, (size_t)omax, "http://%s:%ld/%s", g, port, what);
}

/* one picture fetched (redirects followed) and handed to art.c under its
   key; 1 shown (or being unpacked), 0 not (why logged unless quiet) */
static int fetch_show(const char *what, const char *u0, const char *name, unsigned long key, int quiet)
{
	char u[ICY_URL + 512], why[160];
	unsigned char *d = 0;
	long len = 0;
	int r = 0, hops;
	unsigned long t0;
	void (*was_idle)(void);
	void (*was_msg)(const short *);
	snprintf(u, sizeof(u), "%s", u0);
	t0 = t_fetch = pf_ms();
	was_idle = pf_idle;
	was_msg = busy_message;
	pf_idle = idle;
	busy_message = on_message;
	conn_stop = 0;
	why[0] = 0;
	for (hops = 0; hops < 4; hops++) {
		r = get(u, &d, &len, why, sizeof(why));
		if (r != 2)
			break;
	}
	pf_idle = was_idle;
	busy_message = was_msg;
	conn_stop = 0;
	if (r != 1) {
		if (!quiet || strncmp(why, "answered 404", 12))
			elog("%s: %s, after %lu ms", what, r == 2 ? "redirected too often" : why, pf_ms() - t0);
		return 0;
	}
	elog("%s: %ld bytes in %lu ms", what, len, pf_ms() - t0);
	r = art_station(name, key, d, len);
	if (!r)
		elog("%s: not unpacked (memory, or another picture being made)", what);
	free(d);
	return r;
}

/* the station's logo, when it names no picture of its own and a Gateway is
   given in Settings, whatever the connection (Erez, 8 Nov 2026: "if there
   is a gateway around, let it bring icons, silently"): GET /logo of its
   radio service, 8 s into the station; a station it knows none for keeps
   the disc, and nothing is said */
enum { L_NONE, L_ASK, L_HAVE, L_NO };
static char logo_url[ICY_URL], logo_name[96], logo_key_s[ICY_URL + 8];

void sart_station(const char *url, const char *name)
{
	snprintf(logo_url, sizeof(logo_url), "%s", url);
	snprintf(logo_name, sizeof(logo_name), "%s", name);
	snprintf(logo_key_s, sizeof(logo_key_s), "logo:%s", url);
	logo_st = url[0] ? L_ASK : L_NONE;
}

int sart_step(void)
{
	char u[ICY_URL + 512], what[200];
	unsigned long key;
	const char *target;
	/* (once the music plays: play_step starts it, and a fetch before
	   would hold it back) */
	if (art_pending() || play_ms() < 1000)
		return 0;
	if (logo_st == L_ASK && !want[0] && play_ms() >= 8000) {
		/* the logo: from the cache, else asked of the Gateway */
		int k;
		key = hash(logo_key_s);
		if (art_station_cached(logo_key_s, key)) {
			logo_st = L_HAVE;
			snprintf(done_url, sizeof(done_url), "%s", logo_key_s);
			return 0;
		}
		k = gate_base(u, sizeof(u), "logo?station=");
		if (k <= 0 || (k = enc(u, k, (int)sizeof(u), logo_url)) < 0 ||
		    snprintf(u + k, sizeof(u) - (size_t)k, "&name=") >= (int)(sizeof(u) - (size_t)k) ||
		    enc(u, k + 6, (int)sizeof(u), logo_name) < 0) {
			logo_st = L_NO;		/* (no Gateway given: the disc) */
			return 0;
		}
		snprintf(what, sizeof(what), "the station's logo (through the Gateway)");
		logo_st = fetch_show(what, u, logo_key_s, key, 1) ? L_HAVE : L_NO;
		if (logo_st == L_HAVE)
			snprintf(done_url, sizeof(done_url), "%s", logo_key_s);
		return 0;
	}
	/* what to show: the station's own picture, else its logo, else the disc */
	target = want[0] ? want : logo_st == L_HAVE ? logo_key_s : "";
	if (!strcmp(target, done_url))
		return 0;
	snprintf(done_url, sizeof(done_url), "%s", target);
	if (!target[0]) {
		art_clear();
		return 1;
	}
	key = hash(target);
	if (art_station_cached(target, key))
		return 0;		/* (art_step's from here) */
	if (target == logo_key_s) {
		logo_st = L_ASK;	/* (gone from the cache: asked again) */
		done_url[0] = 0;
		return 0;
	}
	{
		const char *q = strchr(want, '?');
		int gw = settings_gateway();
		snprintf(what, sizeof(what), "the station's picture %.*s%s", (int)(q ? q - want : (long)strlen(want)) > 100 ? 100 :
			 (int)(q ? q - want : (long)strlen(want)), want, gw ? " (through the Gateway)" : "");
		if (gw) {
			int k = gate_base(u, sizeof(u), "art?url=");
			if (k <= 0 || enc(u, k, (int)sizeof(u), want) < 0) {
				elog("%s: no Gateway given", what);
				return 0;
			}
		} else
			snprintf(u, sizeof(u), "%s", want);
		if (!fetch_show(what, u, want, key, 0)) {
			if (logo_st == L_HAVE) {
				done_url[0] = 0;	/* (the logo again) */
				snprintf(want, sizeof(want), "%s", "");
				return 0;
			}
			art_clear();
			return 1;
		}
	}
	return 0;
}
