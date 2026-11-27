/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * m3u.c - playlists (m3u.h).
 */
#include <string.h>
#include <stdio.h>
#include "m3u.h"

static char up(char c)
{
	return c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
}

/* dir's start that a line beginning '\' keeps: its drive ("C:"), else "" */
static int drive_len(const char *dir)
{
	return dir[0] && dir[1] == ':' ? 2 : 0;
}

/* s (k bytes, '/' as '\') onto base: its "..\" taking base's last folder off,
   ".\" nothing. 0 when it doesn't fit */
static int join(char *out, int omax, const char *base, const char *s, int k)
{
	int n = (int)strlen(base), i = 0;
	if (n >= omax)
		return 0;
	memcpy(out, base, (size_t)n + 1);
	while (i < k) {
		int j = i;
		while (j < k && s[j] != '\\' && s[j] != '/')
			j++;
		if (j - i == 2 && s[i] == '.' && s[i + 1] == '.') {
			/* (the last folder off: out ends in '\') */
			if (n > 0 && out[n - 1] == '\\')
				n--;
			while (n > 0 && out[n - 1] != '\\' && out[n - 1] != ':')
				n--;
			if (n > 0 && out[n - 1] == ':')
				out[n++] = '\\';	/* (the root: "C:\" kept) */
			out[n] = 0;
		} else if (!(j - i == 1 && s[i] == '.') && j > i) {
			if (n + (j - i) + 1 >= omax)
				return 0;
			memcpy(out + n, s + i, (size_t)(j - i));
			n += j - i;
			if (j < k)
				out[n++] = '\\';
			out[n] = 0;
		}
		i = j + 1;
	}
	return n > 0;
}

char m3u_extinf[M3U_PATH];

/* "http://" or "https://", in any case, at t (k bytes) */
static int web(const char *t, int k)
{
	static const char *const s[2] = { "http://", "https://" };
	int j, c;
	for (j = 0; j < 2; j++) {
		int m = (int)strlen(s[j]);
		if (k <= m)
			continue;
		for (c = 0; c < m && (t[c] | (t[c] >= 'A' && t[c] <= 'Z' ? 0x20 : 0)) == s[j][c]; c++)
			;
		if (c == m)
			return 1;
	}
	return 0;
}

/* a Spotify item's kind at t (m bytes): "track", "album" or "playlist" */
static const char *sp_kind(const char *t, int m)
{
	static const char *const k[3] = { "track", "album", "playlist" };
	int j;
	for (j = 0; j < 3; j++)
		if ((int)strlen(k[j]) == m && !memcmp(t, k[j], (size_t)m))
			return k[j];
	return 0;
}

/* a Spotify id: 22 letters and digits (base 62), ending at t[m] */
static int sp_id(const char *t, int k, int *m)
{
	int c = 0;
	while (c < k && ((t[c] >= '0' && t[c] <= '9') || (t[c] >= 'a' && t[c] <= 'z') || (t[c] >= 'A' && t[c] <= 'Z')))
		c++;
	*m = c;
	return c == 22 && (c == k || t[c] == '?' || t[c] == '#' || t[c] == '/');
}

static int pre(const char *t, int k, const char *s)
{
	int m = (int)strlen(s), c;
	if (k < m)
		return 0;
	for (c = 0; c < m && (t[c] | (t[c] >= 'A' && t[c] <= 'Z' ? 0x20 : 0)) == s[c]; c++)
		;
	return c == m ? m : 0;
}

int m3u_link(const char *t, int k, char *out, int omax)
{
	int a, m, n;
	const char *kind = 0;
	if ((a = pre(t, k, "spotify:")) != 0) {
		/* spotify:KIND:ID, or the old spotify:user:NAME:playlist:ID */
		if ((n = pre(t + a, k - a, "user:")) != 0) {
			a += n;
			while (a < k && t[a] != ':')
				a++;
			a++;
		}
		for (n = 0; a + n < k && t[a + n] != ':'; n++)
			;
		kind = a + n < k ? sp_kind(t + a, n) : 0;
		a += n + 1;
	} else if ((a = pre(t, k, "https://open.spotify.com/")) != 0 || (a = pre(t, k, "http://open.spotify.com/")) != 0) {
		/* https://open.spotify.com/[intl-xx/]KIND/ID[?si=...] */
		if (pre(t + a, k - a, "intl-")) {
			while (a < k && t[a] != '/')
				a++;
			a++;
		}
		for (n = 0; a + n < k && t[a + n] != '/'; n++)
			;
		kind = a + n < k ? sp_kind(t + a, n) : 0;
		a += n + 1;
	} else if ((a = pre(t, k, "file://")) != 0) {
		/* file:///C:/MUSIC/A%20B.MP3 or file://C:/...: the path */
		char raw[M3U_PATH];
		int r = 0;
		if (a < k && t[a] == '/')
			a++;
		for (; a < k && r < M3U_PATH - 1; a++) {
			char c = t[a];
			if (c == '%' && a + 2 < k) {
				int h = 0, d;
				for (d = 1; d <= 2; d++) {
					char x = t[a + d];
					h = h * 16 + (x >= '0' && x <= '9' ? x - '0' : x >= 'a' && x <= 'f' ? x - 'a' + 10 :
						      x >= 'A' && x <= 'F' ? x - 'A' + 10 : 0);
				}
				c = (char)h;
				a += 2;
			}
			raw[r++] = c == '/' ? '\\' : c;
		}
		if (r < 3 || raw[1] != ':' || a < k)
			return 0;
		raw[r] = 0;
		return join(out, omax, "", raw, r) ? M3U_FILE : 0;
	} else if (web(t, k)) {
		if (k >= omax)
			return 0;
		memcpy(out, t, (size_t)k);
		out[k] = 0;
		return M3U_WEB;
	} else
		return 0;
	if (!kind || a > k || !sp_id(t + a, k - a, &m))
		return 0;
	if ((int)(8 + strlen(kind) + 1 + 22) >= omax)
		return 0;
	snprintf(out, (size_t)omax, "spotify:%s:%.22s", kind, t + a);
	return M3U_SPOTIFY;
}

int m3u_read(const char *t, long n, const char *dir, void (*add)(const char *path, void *ctx), void *ctx)
{
	long i = 0;
	int files = 0;
	m3u_extinf[0] = 0;
	if (n >= 3 && (unsigned char)t[0] == 0xef && (unsigned char)t[1] == 0xbb && (unsigned char)t[2] == 0xbf)
		i = 3;				/* (a UTF-8 BOM) */
	while (i < n) {
		long a = i, b;
		char path[M3U_PATH];
		int k, ok;
		while (i < n && t[i] != '\n' && t[i] != '\r')
			i++;
		b = i;
		while (i < n && (t[i] == '\n' || t[i] == '\r'))
			i++;
		while (a < b && (t[a] == ' ' || t[a] == '\t'))
			a++;
		while (b > a && (t[b - 1] == ' ' || t[b - 1] == '\t'))
			b--;
		if (a == b)
			continue;
		k = (int)(b - a);
		if (t[a] == '#') {
			/* #EXTINF:secs,name: the name, for the line after it */
			if (k > 8 && !memcmp(t + a, "#EXTINF:", 8)) {
				long c = a + 8;
				int m;
				while (c < b && t[c] != ',')
					c++;
				c++;
				while (c < b && t[c] == ' ')
					c++;
				m = c < b ? (int)(b - c) : 0;
				if (m >= M3U_PATH)
					m = M3U_PATH - 1;
				memcpy(m3u_extinf, t + c, (size_t)m);
				m3u_extinf[m] = 0;
			}
			continue;
		}
		if (m3u_link(t + a, k, path, (int)sizeof(path))) {
			/* a station or file on the web (phase 7), a Spotify item, a file:// path */
			add(path, ctx);
			files++;
			m3u_extinf[0] = 0;
			continue;
		}
		if (web(t + a, k) || pre(t + a, k, "spotify:") || pre(t + a, k, "file://")) {
			m3u_extinf[0] = 0;	/* (too long, or not an item EPlay knows) */
			continue;
		}
		{
			/* a URL: "x://" (but not a drive, "C:\") */
			int c = 0;
			while (c < k && t[a + c] != ':' && t[a + c] != '\\' && t[a + c] != '/')
				c++;
			if (c > 1 && c + 2 < k && t[a + c] == ':' && t[a + c + 1] == '/' && t[a + c + 2] == '/')
				continue;
		}
		if (k >= 2 && t[a + 1] == ':') {
			ok = join(path, (int)sizeof(path), "", t + a, k);	/* whole: C:\... */
		} else if (t[a] == '\\' || t[a] == '/') {
			char drv[3];
			int d = drive_len(dir);
			memcpy(drv, dir, (size_t)d);
			drv[d] = 0;
			ok = join(path, (int)sizeof(path), drv, t + a, k);	/* \...: the playlist's drive */
			if (ok && !d) {
				memmove(path + 1, path, strlen(path) + 1);	/* (no drive: keep its '\') */
				path[0] = '\\';
			} else if (ok) {
				memmove(path + d + 1, path + d, strlen(path + d) + 1);
				path[d] = '\\';
			}
		} else
			ok = join(path, (int)sizeof(path), dir, t + a, k);	/* relative: the playlist's folder */
		if (ok) {
			add(path, ctx);
			files++;
		}
		m3u_extinf[0] = 0;
	}
	return files;
}

void m3u_rel(const char *path, const char *dir, char *out, int omax)
{
	int d = (int)strlen(dir), i;
	for (i = 0; i < d && path[i] && up(path[i]) == up(dir[i]); i++)
		;
	if (d && i == d && path[i])
		snprintf(out, (size_t)omax, "%s", path + d);
	else
		snprintf(out, (size_t)omax, "%s", path);
}

long m3u_entry(char *out, long omax, const char *path, const char *dir, long secs, const char *artist,
	       const char *title)
{
	char rel[M3U_PATH], name[M3U_PATH];
	const char *f = strrchr(path, '\\');
	int n;
	m3u_rel(path, dir, rel, (int)sizeof(rel));
	if (title && *title && artist && *artist)
		snprintf(name, sizeof(name), "%s - %s", artist, title);
	else if (title && *title)
		snprintf(name, sizeof(name), "%s", title);
	else
		snprintf(name, sizeof(name), "%s", f ? f + 1 : path);
	n = snprintf(out, (size_t)omax, "#EXTINF:%ld,%s\r\n%s\r\n", secs < 0 ? -1L : secs, name, rel);
	return n < 0 || n >= omax ? -1 : n;
}
