/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * stsearch.h - Find station (Erez, 9 Nov 2026): radio-browser.info's
 * stations by name, genre, country and language, sorted by votes, by
 * listeners or by name. Asked directly (its JSON, MP3 stations only: what
 * a Direct connection plays) or through the Gateway (radiogate's /search:
 * a line of text a station, any codec), as the Options menu has it. The
 * names in the Atari character set.
 */
#ifndef EPLAY_STSEARCH_H
#define EPLAY_STSEARCH_H

#define SS_MAX 50		/* stations a search shows at most */
#define SS_URL 256

typedef struct {
	char name[48];		/* Atari text, cut */
	char cc[3];		/* the country's code: "DE" ("" none) */
	char codec[8];		/* "MP3", "AAC" */
	int kbps;		/* 0: not said */
	char url[SS_URL];
} SSTATION;

enum { SS_VOTES, SS_CLICKS, SS_NAME };

typedef struct {
	const char *q;		/* words of the name ("" any) */
	const char *tag;	/* a genre, radio-browser's tag ("" any) */
	const char *cc;		/* a country's code ("" any) */
	const char *lang;	/* a language, radio-browser's name of it ("" any) */
	int sort;		/* SS_ */
} SSQUERY;

/* the address to ask: radio-browser's own at api (its "https://de1.api..."
   or http), or the Gateway's /search at gate ("http://GATE:PORT"). The
   length, or -1 when it doesn't fit in n */
int ss_url_direct(char *out, int n, const char *api, const SSQUERY *q);
int ss_url_gateway(char *out, int n, const char *gate, const SSQUERY *q);

/* the answer read into st (max of them): how many. radiogate's lines, or
   radio-browser's JSON array of objects; anything else, or what is cut
   short, gives the whole stations before it */
int ss_parse_lines(const char *text, long len, SSTATION *st, int max);
int ss_parse_json(const char *text, long len, SSTATION *st, int max);

#endif
