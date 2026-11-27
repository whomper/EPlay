/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_m3u.c - src/m3u.c: playlists read (whole, drive-relative and
 * relative paths, '/' and '\', ..\, notes, URLs, CR/LF, LF, a BOM) and
 * written (#EXTINF, paths relative to the playlist's folder).
 */
#include <stdio.h>
#include <string.h>
#include "m3u.h"

static int bad;
static void check(int ok, const char *what)
{
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	bad += !ok;
}

static char got[2000], names[2000];
static void add(const char *path, void *ctx)
{
	(void)ctx;
	if (got[0])
		strcat(got, "|");
	strcat(got, path);
	strcat(names, "[");
	strcat(names, m3u_extinf);
	strcat(names, "]");
}

static const char *read(const char *t, const char *dir)
{
	got[0] = names[0] = 0;
	m3u_read(t, (long)strlen(t), dir, add, 0);
	return got;
}

int main(void)
{
	char out[400], rel[300];
	long n;
	check(!strcmp(read("#EXTM3U\r\n#EXTINF:123,Erez - Night\r\n01_NIGHT.MP3\r\nLIVE\\02.WAV\r\n", "C:\\MUSIC\\"),
		      "C:\\MUSIC\\01_NIGHT.MP3|C:\\MUSIC\\LIVE\\02.WAV"),
	      "relative to the playlist's folder; notes passed over (CR/LF)");
	check(!strcmp(read("D:\\OTHER\\A.AIF\n\\TOP\\B.MP3\n", "C:\\MUSIC\\"), "D:\\OTHER\\A.AIF|C:\\TOP\\B.MP3"),
	      "whole paths, and \\ on the playlist's drive (LF)");
	check(!strcmp(read("../SHARED/C.MP3\n./D.MP3\nLIVE/E.MP3\n", "C:\\MUSIC\\LISTS\\"),
		      "C:\\MUSIC\\SHARED\\C.MP3|C:\\MUSIC\\LISTS\\D.MP3|C:\\MUSIC\\LISTS\\LIVE\\E.MP3"),
	      "'/' as '\\', ..\\ and .\\");
	check(!strcmp(read("..\\..\\..\\F.MP3\n", "C:\\A\\"), "C:\\F.MP3"), "..\\ past the root stays at the root");
	check(!strcmp(read("\xef\xbb\xbf  G.MP3  \r\n\r\n\r\nftp://radio.example/s.mp3\r\n# a note\r\nH.WAV", "C:\\M\\"),
		      "C:\\M\\G.MP3|C:\\M\\H.WAV"),
	      "a BOM, spaces, empty lines, a URL not a station's passed over, the last line without an end");
	/* stations (Erez, 9 Nov 2026: Open... of an .M3U of stations added none) */
	check(!strcmp(read("#EXTM3U\r\n#EXTINF:-1,Radio One\r\nhttp://radio.example:8000/s.mp3\r\n"
			   "HTTPS://Secure.example/live\r\nA.MP3\r\n#EXTINF:12,Tone\r\nB.MP3\r\n", "C:\\M\\"),
		      "http://radio.example:8000/s.mp3|HTTPS://Secure.example/live|C:\\M\\A.MP3|C:\\M\\B.MP3"),
	      "a station's address taken as it is");
	check(!strcmp(names, "[Radio One][][][Tone]"), "its #EXTINF name with it, none where there is none");
	m3u_rel("C:\\MUSIC\\LIVE\\A.MP3", "c:\\music\\", rel, sizeof(rel));
	check(!strcmp(rel, "LIVE\\A.MP3"), "written relative where in the folder (the case aside)");
	m3u_rel("D:\\X\\B.MP3", "C:\\MUSIC\\", rel, sizeof(rel));
	check(!strcmp(rel, "D:\\X\\B.MP3"), "else whole");
	n = m3u_entry(out, sizeof(out), "C:\\MUSIC\\01.MP3", "C:\\MUSIC\\", 245, "Artist", "Night");
	check(n > 0 && !strcmp(out, "#EXTINF:245,Artist - Night\r\n01.MP3\r\n"), "an entry: #EXTINF, artist - title");
	m3u_entry(out, sizeof(out), "C:\\MUSIC\\02.WAV", "C:\\MUSIC\\", -1, "", "");
	check(!strcmp(out, "#EXTINF:-1,02.WAV\r\n02.WAV\r\n"), "no tags, no length: the file's name, -1");
	check(m3u_entry(out, 10, "C:\\MUSIC\\01.MP3", "C:\\MUSIC\\", 1, "", "") < 0, "too long for out: -1");
	{
		/* written, then read back */
		char pl[600] = M3U_HEAD;
		m3u_entry(out, sizeof(out), "C:\\MUSIC\\01.MP3", "C:\\MUSIC\\", 1, "", "T");
		strcat(pl, out);
		m3u_entry(out, sizeof(out), "D:\\X\\02.WAV", "C:\\MUSIC\\", 2, "", "");
		strcat(pl, out);
		check(!strcmp(read(pl, "C:\\MUSIC\\"), "C:\\MUSIC\\01.MP3|D:\\X\\02.WAV"), "written and read back");
	}
	/* links: Spotify (its forms to one), file:///, mixed with paths and stations */
	check(!strcmp(read("#EXTM3U\r\n#EXTINF:-1,Night Mix\r\nspotify:playlist:37i9dQZF1DXcBWIGoYBM5M\r\n"
			   "https://open.spotify.com/intl-de/album/4aawyAB9vmqN3uQ7FjRGTy?si=abc123\r\n"
			   "http://open.spotify.com/track/0VjIjW4GlUZAMYd2vXMi3b\r\n"
			   "spotify:user:someone:playlist:37i9dQZF1DXcBWIGoYBM5M\r\n"
			   "SPOTIFY:track:0VjIjW4GlUZAMYd2vXMi3b\r\n"
			   "file:///C:/MUSIC/A%20B.MP3\r\nfile://D:/X/Y.WAV\r\nA.MP3\r\nhttps://radio.example/live\r\n", "C:\\M\\"),
		      "spotify:playlist:37i9dQZF1DXcBWIGoYBM5M|spotify:album:4aawyAB9vmqN3uQ7FjRGTy|"
		      "spotify:track:0VjIjW4GlUZAMYd2vXMi3b|spotify:playlist:37i9dQZF1DXcBWIGoYBM5M|"
		      "spotify:track:0VjIjW4GlUZAMYd2vXMi3b|C:\\MUSIC\\A B.MP3|D:\\X\\Y.WAV|C:\\M\\A.MP3|https://radio.example/live"),
	      "links: Spotify's forms, file:///, paths and a station in one list");
	check(!strcmp(names, "[Night Mix][][][][][][][][]"), "links: #EXTINF names the Spotify line");
	check(!strcmp(read("spotify:artist:0OdUWJ0sBjDrqHygGUXeCF\nspotify:track:short\n"
			   "https://open.spotify.com/show/0VjIjW4GlUZAMYd2vXMi3b\nfile:///MUSIC/A.MP3\nftp://x/y\nB.MP3\n", "C:\\"),
		      "C:\\B.MP3"),
	      "links: an artist, a bad id, another Spotify page (a web page, not sound), file:/// with no drive passed over");
	{
		char o[40];
		check(!m3u_link("spotify:playlist:37i9dQZF1DXcBWIGoYBM5M", 39, o, 30), "links: too long for out: 0");
	}
	return bad != 0;
}
