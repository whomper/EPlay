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
		if (web(t + a, k)) {
			/* a station (phase 7): its address as it is */
			if (k < M3U_PATH) {
				memcpy(path, t + a, (size_t)k);
				path[k] = 0;
				add(path, ctx);
				files++;
			}
			m3u_extinf[0] = 0;
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
