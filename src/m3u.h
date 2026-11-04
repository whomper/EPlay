/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * m3u.h - playlists (.M3U), read and written (phase 4: playlists and
 * folders). A line is a file: a whole path (C:\MUSIC\A.MP3, \MUSIC\A.MP3
 * on the playlist's drive) or one relative to the playlist's folder
 * (A.MP3, LIVE\A.MP3, ..\B.MP3), with '\' or '/'. Lines starting '#' are
 * notes (#EXTM3U, #EXTINF:seconds,artist - title); a URL (http:// and the
 * like) is passed over; CR/LF, LF and a UTF-8 BOM are taken. Written:
 * #EXTM3U, then each file's #EXTINF and its path, relative to the
 * playlist's folder where it is in it, CR/LF ends.
 */
#ifndef EPLAY_M3U_H
#define EPLAY_M3U_H

#define M3U_PATH 256

/* each file of the playlist text t (n bytes) to add(path, ctx); dir is the
   playlist's folder ("C:\MUSIC\", ending in '\'). The files given */
int m3u_read(const char *t, long n, const char *dir, void (*add)(const char *path, void *ctx), void *ctx);

/* path as a playlist in dir would hold it: relative where it is in dir
   (its case aside), else whole */
void m3u_rel(const char *path, const char *dir, char *out, int omax);

/* a file's lines (its #EXTINF, then its path as m3u_rel has it) at out:
   their length, or -1 when they don't fit in omax. secs < 0: not known
   (-1); artist and title may be "" (then the file's name) */
long m3u_entry(char *out, long omax, const char *path, const char *dir, long secs, const char *artist,
	       const char *title);

#define M3U_HEAD "#EXTM3U\r\n"

#endif
