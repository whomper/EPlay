/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * tags.h - what a file says it is (the bar's title, artist and album, and
 * where its album art is): ID3v2 (2.2, 2.3, 2.4) at an MP3's start or in a
 * WAV's or AIFF's "id3 " chunk, ID3v1 at an MP3's end, a WAV's LIST INFO
 * (INAM, IART, IPRD), an AIFF's NAME and AUTH. The text in the Atari
 * character set (common's charset.c: Hebrew too). The file is read through
 * a callback, a piece at a time: an album art of a megabyte is never read
 * here, only found.
 */
#ifndef EPLAY_TAGS_H
#define EPLAY_TAGS_H

#include "rgain.h"

#define TAG_TEXT 64

/* n bytes of the file from at into buf: how many (0 or less: none) */
typedef long (*TAG_READ)(void *u, long at, unsigned char *buf, long n);

typedef struct {
	char title[TAG_TEXT], artist[TAG_TEXT], album[TAG_TEXT];	/* "" where none */
	long pic_at, pic_end;	/* the album art's bytes in the file, [at, end); end 0: none */
	int pic_unsync;		/* its bytes unsynchronised (each $FF $00 is $FF) */
	int pic_type;		/* ID3's picture type: 3 the front cover */
	long id3_len;		/* the bytes an ID3v2 tag at the start takes (0: none) */
	RGAIN rg;		/* ReplayGain: ID3v2's TXXX REPLAYGAIN_TRACK_GAIN, _PEAK,
				   REPLAYGAIN_ALBUM_GAIN, _PEAK (RG_NONE, 0: none) */
} TAGS;

void tags_clear(TAGS *t);

/* an ID3v2 tag at base: 1 if there is one (its fields fill the empty
   ones; the front cover is taken over any other picture) */
int tags_id3v2(TAGS *t, TAG_READ rd, void *u, long base);
/* an ID3v1 tag in the last 128 bytes: 1 if there is one (it fills only
   what is still empty) */
int tags_id3v1(TAGS *t, TAG_READ rd, void *u, long size);
/* a WAV's (RIFF) or an AIFF's (FORM) chunks: LIST INFO or NAME and AUTH,
   and an ID3v2 tag in its "id3 " or "ID3 " chunk. 1 if it is one */
int tags_chunks(TAGS *t, TAG_READ rd, void *u, long size);

/* all of them, as a file has them: the chunks of a WAV or AIFF, else ID3v2
   at the start then ID3v1 at the end */
void tags_read(TAGS *t, TAG_READ rd, void *u, long size);

#endif
