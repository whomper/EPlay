/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_stsearch.c - src/stsearch.c: the addresses asked (directly, through
 * the Gateway), radiogate's lines and radio-browser's JSON read (escapes,
 * accents, nested values passed over, entries without an address left
 * out), then every answer cut short at every length and changed at
 * random, under AddressSanitizer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "stsearch.h"

static int bad;

static void same(const char *what, const char *got, const char *want)
{
	if (strcmp(got, want)) {
		printf("FAIL %s: \"%s\", wanted \"%s\"\n", what, got, want);
		bad = 1;
	}
}

static void same_n(const char *what, long got, long want)
{
	if (got != want) {
		printf("FAIL %s: %ld, wanted %ld\n", what, got, want);
		bad = 1;
	}
}

static void ok(const char *what)
{
	if (!bad)
		printf("ok   %s\n", what);
}

static const char json[] =
	"[{\"changeuuid\":\"x\",\"name\":\"Jazz \\\"FM\\\" K\\u00f6ln\",\"url\":\"http://a.example/x\","
	"\"url_resolved\":\"http://jazz.example/live\",\"tags\":\"jazz,smooth\",\"countrycode\":\"DE\","
	"\"codec\":\"MP3\",\"bitrate\":128,\"geo\":{\"lat\":1.5,\"long\":[1,2,{\"a\":null}]},\"lastcheckok\":1},\n"
	" {\"name\":\"No address\",\"url\":\"\",\"url_resolved\":\"\",\"codec\":\"AAC\",\"bitrate\":64},\n"
	" {\"name\":\"\\ud83c\\udfb7 Sax\",\"url\":\"https://sax.example/s\",\"url_resolved\":null,\"countrycode\":\"NL\","
	"\"codec\":\"MP3\",\"bitrate\":\"96\"}]";

int main(void)
{
	SSTATION st[SS_MAX];
	char u[600], want[64];
	SSQUERY q;
	int n, i, k;

	memset(&q, 0, sizeof(q));
	q.q = "jazz k\x94ln";	/* (Atari's ö) */
	q.tag = "smooth jazz";
	q.cc = "DE";
	q.lang = "german";
	q.sort = SS_CLICKS;
	ss_url_direct(u, sizeof(u), "https://de1.api.radio-browser.info", &q);
	same("direct address", u,
	     "https://de1.api.radio-browser.info/json/stations/search?hidebroken=true&codec=MP3&limit=50"
	     "&order=clickcount&reverse=true&name=jazz%20k%C3%B6ln&tag=smooth%20jazz&countrycode=DE&language=german");
	q.q = "";
	q.tag = "";
	q.sort = SS_NAME;
	ss_url_gateway(u, sizeof(u), "http://192.168.1.20:8580", &q);
	same("gateway address", u, "http://192.168.1.20:8580/search?n=50&sort=name&country=DE&language=german");
	same_n("too small", ss_url_gateway(u, 20, "http://192.168.1.20:8580", &q), -1);
	ok("the addresses");

	n = ss_parse_json(json, (long)strlen(json), st, SS_MAX);
	same_n("JSON stations", n, 2);
	{
		char k1[4];
		int m = uni_to_atari(0xf6, k1);
		k1[m] = 0;
		snprintf(want, sizeof(want), "Jazz \"FM\" K%sln", k1);
	}
	same("JSON name", st[0].name, want);
	same("JSON url_resolved", st[0].url, "http://jazz.example/live");
	same("JSON country", st[0].cc, "DE");
	same("JSON codec", st[0].codec, "MP3");
	same_n("JSON kbit/s", st[0].kbps, 128);
	same("JSON url (resolved null)", st[1].url, "https://sax.example/s");
	same_n("JSON kbit/s as a string", st[1].kbps, 96);
	same_n("JSON empty", ss_parse_json("[]", 2, st, SS_MAX), 0);
	same_n("JSON not an array", ss_parse_json("{\"a\":1}", 7, st, SS_MAX), 0);
	same_n("JSON max", ss_parse_json(json, (long)strlen(json), st, 1), 1);
	ok("radio-browser's JSON");

	{
		static const char lines[] = "Jazz FM\tDE\tMP3\t128\thttp://jazz.example/live\n"
					    "Bad\tFR\tAAC\t64\tftp://x/y\n"
					    "Ogg Jazz\tNL\tOGG\t0\thttps://ogg.example/s\r\n"
					    "Cut\tDE\tMP3\t128\thttp://cut.example/";
		n = ss_parse_lines(lines, (long)strlen(lines), st, SS_MAX);
		same_n("lines", n, 2);
		same("lines name", st[0].name, "Jazz FM");
		same_n("lines kbit/s", st[0].kbps, 128);
		same("lines url with CR", st[1].url, "https://ogg.example/s");
		same("lines codec", st[1].codec, "OGG");
	}
	ok("radiogate's lines");

	/* every length, and bytes changed at random */
	srand(1);
	for (k = 0; k <= (int)strlen(json); k++) {
		char *c = malloc((size_t)k + 1);
		memcpy(c, json, (size_t)k);
		ss_parse_json(c, k, st, SS_MAX);
		ss_parse_lines(c, k, st, SS_MAX);
		free(c);
	}
	for (i = 0; i < 20000; i++) {
		long len = (long)strlen(json);
		char *c = malloc((size_t)len);
		memcpy(c, json, (size_t)len);
		for (k = 0; k < 4; k++)
			c[rand() % len] = (char)(rand() & 255);
		ss_parse_json(c, len, st, SS_MAX);
		ss_parse_lines(c, len, st, SS_MAX);
		free(c);
	}
	{
		/* deep nesting: no stack run away */
		static char deep[20000];
		memset(deep, '[', sizeof(deep));
		deep[0] = '[';
		deep[1] = '{';
		memcpy(deep + 2, "\"a\":", 4);
		ss_parse_json(deep, sizeof(deep), st, SS_MAX);
	}
	ok("cut short and changed at random");
	return bad;
}
