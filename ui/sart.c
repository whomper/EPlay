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
#include "settings.h"
#include "art.h"
#include "play.h"
#include "elog.h"
#include "busy.h"
#include "sart.h"

extern const char app_version[];

#define PIC_MAX (512L * 1024)		/* a bigger picture is left out */

static char want[ICY_URL], done_url[ICY_URL];
static void (*on_message)(const short *msg);

void sart_init(void (*msg)(const short *msg))
{
	on_message = msg;
}

void sart_reset(void)
{
	want[0] = done_url[0] = 0;
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

/* the Gateway's: GET /art?url=<the picture's address, %-encoded> */
static int gate_url(const char *pic, char *out, int omax)
{
	static const char hex[] = "0123456789ABCDEF";
	const char *g = settings_gate();
	long port = settings_gateport();
	int k;
	if (!g[0] || port <= 0)
		return 0;
	k = snprintf(out, (size_t)omax, "http://%s:%ld/art?url=", g, port);
	for (; *pic && k < omax - 4; pic++) {
		unsigned char ch = (unsigned char)*pic;
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
	return !*pic;
}

int sart_step(void)
{
	char u[ICY_URL], why[160];
	unsigned char *d = 0;
	long len = 0;
	int r = 0, hops, gw;
	unsigned long t0, key;
	void (*was_idle)(void);
	void (*was_msg)(const short *);
	/* (once the music plays: play_step starts it, and a fetch before
	   would hold it back) */
	if (!strcmp(want, done_url) || art_pending() || play_ms() < 1000)
		return 0;
	snprintf(done_url, sizeof(done_url), "%s", want);
	if (!want[0]) {
		art_clear();		/* (no picture now: the disc) */
		return 1;
	}
	key = hash(want);
	if (art_station_cached(want, key))
		return 0;		/* (art_step's from here) */
	gw = settings_gateway();
	if (gw) {
		if (!gate_url(want, u, sizeof(u))) {
			elog("the station's picture: no Gateway given");
			return 0;
		}
	} else
		snprintf(u, sizeof(u), "%s", want);
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
	{
		const char *q = strchr(want, '?');
		if (r != 1) {
			elog("the station's picture %.*s%s: %s, after %lu ms", (int)(q ? q - want : (long)strlen(want)),
			     want, gw ? " (through the Gateway)" : "", r == 2 ? "redirected too often" : why,
			     pf_ms() - t0);
			art_clear();
			return 1;
		}
		elog("the station's picture %.*s%s: %ld bytes in %lu ms", (int)(q ? q - want : (long)strlen(want)),
		     want, gw ? " (through the Gateway)" : "", len, pf_ms() - t0);
	}
	if (!art_station(want, key, d, len))
		elog("the station's picture: not unpacked (memory, or another picture being made)");
	free(d);
	return 0;
}
