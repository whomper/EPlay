/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * stream.h - web radio and audio streams (phase 7): what comes from the
 * network, parsed with no allocation and every length checked (network
 * data is untrusted: the guidelines, 7.1).
 *   - a stream's URL (http:// or https://, a host, a port, a path);
 *   - the server's answer head (Icecast and Shoutcast: "ICY 200 OK" or
 *     "HTTP/1.x 200", icy-metaint, icy-name, icy-br, Content-Type, and
 *     Location for a redirect);
 *   - the ICY metadata taken out of the audio in place (a length byte
 *     every metaint bytes, then that many times 16 bytes of
 *     "StreamTitle='...';"), the title kept;
 *   - station lists (.PLS: File1=URL, Title1=name).
 */
#ifndef EPLAY_STREAM_H
#define EPLAY_STREAM_H

#define SURL_HOST 128
#define SURL_PATH 512

typedef struct {
	int tls;			/* https:// */
	char host[SURL_HOST];
	unsigned short port;		/* 80 or 443 unless given */
	char path[SURL_PATH];		/* "/" at least, the query kept */
} SURL;

/* 1 and o filled for a URL EPlay can open, else 0 (another scheme, a host
   or path too long, a bad port, spaces or control bytes) */
int surl_parse(const char *u, SURL *o);

/* 1 if s looks like a stream's URL (http:// or https://, case ignored) */
int surl_is(const char *s);

#define SHEAD_NAME 80
#define SHEAD_TYPE 48

typedef struct {
	int status;			/* 200, 301, 302, 303, 307, 308, 404...; 0 if none */
	long metaint;			/* icy-metaint: 0 without metadata */
	long kbps;			/* icy-br: 0 if not given */
	char name[SHEAD_NAME];		/* icy-name: the station's */
	char type[SHEAD_TYPE];		/* Content-Type, lowercase, its parameters cut */
	char location[SURL_PATH];	/* Location, for a redirect */
	int chunked;			/* Transfer-Encoding: chunked (chunks_feed takes it out) */
} SHEAD;

/* the head in t (n bytes): its lines, CR/LF or LF, up to the blank line or
   n. The bytes the head took (the blank line's end) with h filled, 0 when
   the blank line hasn't come yet within n, -1 when it isn't an answer EPlay
   reads (no status line, a line over 1 KB, a metaint out of 1..65535) */
long shead_parse(const char *t, long n, SHEAD *h);

/* the kinds of audio a Content-Type names */
#define STYPE_UNKNOWN 0
#define STYPE_MP3     1			/* audio/mpeg, audio/mp3, audio/x-mpeg */
#define STYPE_AAC     2			/* audio/aac, audio/aacp, audio/x-aac */
#define STYPE_OGG     3			/* application/ogg, audio/ogg */
#define STYPE_PLAYLIST 4		/* audio/x-scpls, audio/x-mpegurl, application/vnd.apple.mpegurl */
int stype(const char *type);

#define ICY_TITLE 128
#define ICY_URL 256

typedef struct {
	long metaint;			/* 0: no metadata, all audio */
	long left;			/* audio bytes before the next length byte */
	int mlen, mgot;			/* the metadata block's bytes, and those come */
	char meta[16 * 255 + 1];
	char title[ICY_TITLE];		/* StreamTitle, "" before the first */
	int new_title;			/* set when the title changed; the caller clears it */
	char url[ICY_URL];		/* StreamUrl, when an http(s) address: a picture's on some stations */
	int new_url;			/* set when it changed; the caller clears it */
} ICY;

void icy_init(ICY *s, long metaint);

/* the n bytes at b as they came: the metadata taken out, the audio moved
   down in place. The audio bytes left at b */
long icy_feed(ICY *s, unsigned char *b, long n);

/* each station of the .PLS text t (n bytes) to add(url, title, ctx), in the
   order of their File lines; title "" without a TitleN. The stations given */
int pls_read(const char *t, long n, void (*add)(const char *url, const char *title, void *ctx), void *ctx);

/* Transfer-Encoding: chunked (some servers and CDNs answer so): the chunks'
   framing taken out of the n bytes at b, the data moved down in place.
   The data bytes left at b, or -1 for framing that isn't chunked's; done
   is set after the last chunk */
typedef struct {
	int state, digits, ext, done;
	unsigned long left;
} CHUNKS;

void chunks_init(CHUNKS *c);
long chunks_feed(CHUNKS *c, unsigned char *b, long n);

#endif
