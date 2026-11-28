/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_lib.c - the library's platform code (src/lib.c, src/libjf.c) and
 * its JSON reader (src/json.c): the requests Jellyfin is sent, its answers
 * read (made up here in Jellyfin's shape: nesting, escapes, accents, null,
 * kinds not shown), song lines, then every answer cut short at every
 * length and changed at random, under AddressSanitizer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "json.h"
#include "lib.h"

static int bad;

static void same(const char *what, const char *got, const char *want)
{
	if (strcmp(got, want)) {
		printf("FAIL %s:\n  got  \"%s\"\n  want \"%s\"\n", what, got, want);
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

#define UID "0a1b2c3d4e5f60718293a4b5c6d7e8f9"
#define TOK "f00dfeedf00dfeedf00dfeedf00dfeed"
#define ALB "11112222333344445555666677778888"

static const char signin_ans[] =
	"{\"User\":{\"Name\":\"listener\",\"ServerId\":\"abc\",\"Id\":\"" UID "\",\"HasPassword\":true,"
	"\"Configuration\":{\"OrderedViews\":[],\"GroupedFolders\":[\"x\",{\"y\":[1,2,{}]}]},"
	"\"Policy\":{\"IsAdministrator\":false}},"
	"\"SessionInfo\":{\"Id\":\"s1\",\"UserId\":\"" UID "\",\"PlayState\":{\"CanSeek\":false}},"
	"\"AccessToken\":\"" TOK "\",\"ServerId\":\"abc\"}";

static const char albums_ans[] =
	"{\"Items\":[\n"
	" {\"Name\":\"Signals\",\"ServerId\":\"abc\",\"Id\":\"" ALB "\",\"ProductionYear\":2025,"
	"  \"RunTimeTicks\":13000000000,\"ChildCount\":5,\"Type\":\"MusicAlbum\",\"IsFolder\":true,"
	"  \"AlbumArtist\":\"Test Card\",\"AlbumArtists\":[{\"Name\":\"Test Card\",\"Id\":\"aa\"}],"
	"  \"ImageTags\":{\"Primary\":\"1f2e\"},\"BackdropImageTags\":[],\"LocationType\":\"FileSystem\"},\n"
	" {\"Name\":\"Caf\\u00e9 \\\"Nights\\\"\",\"Id\":\"99998888777766665555444433332222\",\"Type\":\"MusicAlbum\","
	"  \"ProductionYear\":null,\"AlbumArtist\":\"B\\u00e4nd\",\"ImageTags\":{}},\n"
	" {\"Name\":\"A film\",\"Id\":\"12341234123412341234123412341234\",\"Type\":\"Movie\"},\n"
	" {\"Name\":\"\",\"Id\":\"43214321432143214321432143214321\",\"Type\":\"MusicAlbum\"}\n"
	"],\"TotalRecordCount\":23,\"StartIndex\":0}";

static const char songs_ans[] =
	"{\"Items\":[{\"Name\":\"Night Shift\",\"Id\":\"abcdefabcdefabcdefabcdefabcdef01\",\"Type\":\"Audio\","
	"\"RunTimeTicks\":2380000000,\"IndexNumber\":2,\"ProductionYear\":2025,\"Album\":\"Signals\","
	"\"AlbumId\":\"" ALB "\",\"AlbumPrimaryImageTag\":\"1f2e\",\"Artists\":[\"Test Card\",\"Guest\"],"
	"\"ArtistItems\":[{\"Name\":\"Test Card\",\"Id\":\"aa\"}],\"AlbumArtist\":\"\",\"ImageTags\":{},"
	"\"MediaType\":\"Audio\",\"NormalizationGain\":-7.25},"
	"{\"Name\":\"Long \\ud83c\\udfb5 Wave\",\"Id\":\"abcdefabcdefabcdefabcdefabcdef02\",\"Type\":\"Audio\","
	"\"RunTimeTicks\":4.2E9,\"Artists\":[]}],"
	"\"TotalRecordCount\":2}";

static void t_requests(void)
{
	LIB_ACCOUNT a;
	LIB_REQ r;
	LIB_QUERY q;
	const LIB_PLATFORM *jf = &lib_jellyfin;
	char url[400];
	memset(&a, 0, sizeof(a));
	strcpy(a.server, "http://192.168.1.x:8096");
	strcpy(a.user, "Ren\x82");		/* (é in Atari text) */
	strcpy(a.device, "e1a2");
	a.kbps = 192;
	same_n("signin", jf->signin(&a, "pa\"ss\\w\x81", "1.63", &r), 0);
	same_n("signin post", r.post, 1);
	same("signin path", r.path, "/Users/AuthenticateByName");
	same("signin body", r.body, "{\"Username\":\"Ren\xc3\xa9\",\"Pw\":\"pa\\\"ss\\\\w\xc3\xbc\"}");
	same("signin auth", r.auth, "MediaBrowser Client=\"EPlay\", Device=\"Atari\", DeviceId=\"e1a2\", Version=\"1.63\"");
	same_n("signed in", jf->signed_in(&a, signin_ans, (long)strlen(signin_ans)), 1);
	same("uid", a.uid, UID);
	same("token", a.token, TOK);
	ok("sign-in");

	memset(&q, 0, sizeof(q));
	q.what = LQ_ALBUMS;
	q.find = "caf\x82 & co";
	q.start = 50;
	q.count = 25;
	same_n("albums", jf->list(&a, &q, "1.63", &r), 0);
	same_n("albums get", r.post, 0);
	same("albums path", r.path,
	     "/Items?Recursive=true&IncludeItemTypes=MusicAlbum&SortBy=SortName&userId=" UID
	     "&Fields=ChildCount&EnableUserData=false&EnableImageTypes=Primary&ImageTypeLimit=1"
	     "&searchTerm=caf%C3%A9%20%26%20co&StartIndex=50&Limit=25");
	same("albums auth", r.auth,
	     "MediaBrowser Client=\"EPlay\", Device=\"Atari\", DeviceId=\"e1a2\", Version=\"1.63\", Token=\"" TOK "\"");
	q.what = LQ_ARTISTS;
	q.find = 0;
	q.start = 0;
	q.count = 0;
	jf->list(&a, &q, "1.63", &r);
	same("artists path", r.path,
	     "/Artists/AlbumArtists?SortBy=SortName&userId=" UID
	     "&Fields=ChildCount&EnableUserData=false&EnableImageTypes=Primary&ImageTypeLimit=1&StartIndex=0&Limit=50");
	q.what = LQ_IN;
	q.parent = ALB;
	q.parent_kind = LK_ALBUM;
	jf->list(&a, &q, "1.63", &r);
	same("album's songs path", r.path,
	     "/Items?Recursive=true&IncludeItemTypes=Audio&SortBy=ParentIndexNumber,IndexNumber,SortName&ParentId=" ALB
	     "&userId=" UID "&Fields=ChildCount&EnableUserData=false&EnableImageTypes=Primary&ImageTypeLimit=1"
	     "&StartIndex=0&Limit=50");
	q.parent_kind = LK_PLAYLIST;
	jf->list(&a, &q, "1.63", &r);
	same("playlist's songs path", r.path,
	     "/Playlists/" ALB "/Items?MediaTypes=Audio&userId=" UID
	     "&Fields=ChildCount&EnableUserData=false&EnableImageTypes=Primary&ImageTypeLimit=1&StartIndex=0&Limit=50");
	q.parent_kind = LK_ARTIST;
	jf->list(&a, &q, "1.63", &r);
	same("artist's albums path", r.path,
	     "/Items?Recursive=true&IncludeItemTypes=MusicAlbum&SortBy=ProductionYear,SortName&AlbumArtistIds=" ALB
	     "&userId=" UID "&Fields=ChildCount&EnableUserData=false&EnableImageTypes=Primary&ImageTypeLimit=1"
	     "&StartIndex=0&Limit=50");
	q.parent = "../../System";
	same_n("a parent that isn't an id", jf->list(&a, &q, "1.63", &r), -1);
	ok("lists asked");

	jf->stream(&a, "abcdefabcdefabcdefabcdefabcdef01", 0, url, sizeof(url));
	same("stream", url,
	     "http://192.168.1.x:8096/Audio/abcdefabcdefabcdefabcdefabcdef01/stream.mp3?AudioCodec=mp3"
	     "&MaxAudioChannels=2&AudioBitRate=192000&DeviceId=e1a2&ApiKey=" TOK);
	a.kbps = 320;
	jf->stream(&a, "abcdefabcdefabcdefabcdefabcdef01", 61500, url, sizeof(url));
	same("stream from 1:01.5", url,
	     "http://192.168.1.x:8096/Audio/abcdefabcdefabcdefabcdefabcdef01/stream.mp3?AudioCodec=mp3"
	     "&MaxAudioChannels=2&AudioBitRate=320000&StartTimeTicks=615000000&DeviceId=e1a2&ApiKey=" TOK);
	same_n("stream too long", jf->stream(&a, "abcdefabcdefabcdefabcdefabcdef01", 0, url, 60), -1);
	same_n("stream of no id", jf->stream(&a, "x/../y", 0, url, sizeof(url)), -1);
	jf->picture(&a, ALB, 96, url, sizeof(url));
	same("picture", url,
	     "http://192.168.1.x:8096/Items/" ALB "/Images/Primary?format=Jpg&quality=85&maxWidth=96&maxHeight=96");
	ok("stream and picture");
	a.token[0] = 0;
	same_n("no stream signed out", jf->stream(&a, ALB, 0, url, sizeof(url)), -1);
}

static void t_answers(void)
{
	LIB_ITEM it[8];
	long total;
	int n = lib_jellyfin.items(albums_ans, (long)strlen(albums_ans), it, 8, &total);
	same_n("albums given", n, 2);
	same_n("albums in all", total, 23);
	same("album 1", it[0].name, "Signals");
	same("album 1 artist", it[0].artist, "Test Card");
	same_n("album 1 kind", it[0].kind, LK_ALBUM);
	same_n("album 1 year", it[0].year, 2025);
	same_n("album 1 songs", it[0].count, 5);
	same_n("album 1 length", it[0].len_ms, 1300000);
	same("album 1 picture", it[0].pic, ALB);
	same("album 2", it[1].name, "Caf\x82 \"Nights\"");
	same("album 2 artist", it[1].artist, "B\x84nd");
	same_n("album 2 year", it[1].year, 0);
	same("album 2 no picture", it[1].pic, "");
	ok("albums read (a film and a nameless one left out)");

	n = lib_jellyfin.items(songs_ans, (long)strlen(songs_ans), it, 8, &total);
	same_n("songs given", n, 2);
	same("song 1", it[0].name, "Night Shift");
	same("song 1 artist (from Artists)", it[0].artist, "Test Card");
	same("song 1 album", it[0].album, "Signals");
	same_n("song 1 track", it[0].track, 2);
	same_n("song 1 length", it[0].len_ms, 238000);
	same("song 1 picture is its album's", it[0].pic, ALB);
	same_n("song 1 kind", it[0].kind, LK_SONG);
	same_n("song 2 length (4.2E9: the digits before the E)", it[1].len_ms, 0);
	ok("songs read");

	n = lib_jellyfin.items(songs_ans, (long)strlen(songs_ans), it, 1, &total);
	same_n("max 1", n, 1);
	same_n("max 1 total still read", total, 2);
	ok("a full list leaves the rest");
}

static void t_lines(void)
{
	char line[80], id[LIB_ID];
	same_n("line", lib_line(&lib_jellyfin, ALB, line, sizeof(line)) > 0, 1);
	same("line text", line, "jellyfin:" ALB);
	same_n("line read", lib_line_id(line, id) == &lib_jellyfin, 1);
	same("line id", id, ALB);
	same_n("a file", lib_line_id("C:\\MUSIC\\A.MP3", id) == 0, 1);
	same_n("http", lib_line_id("http://x/y", id) == 0, 1);
	same_n("an id with a slash", lib_line_id("jellyfin:ab/cd", id) == 0, 1);
	same_n("no id", lib_line_id("jellyfin:", id) == 0, 1);
	same_n("Settings' first", lib_platform(0) == &lib_jellyfin, 1);
	same_n("Settings' last", lib_platform(1) == 0, 1);
	ok("song lines");
}

static void t_json(void)
{
	JS j;
	char k[8];
	static const char deep[] = "{\"a\":[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]"
				   "]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]}";
	js_init(&j, deep, (long)strlen(deep));
	js_obj(&j);
	while (js_key(&j, k, sizeof(k)))
		js_skip(&j);
	same_n("deeper than 64: a fault", j.bad, 1);
	js_init(&j, "{\"a\":1 \"b\":2}", 13);
	js_obj(&j);
	while (js_key(&j, k, sizeof(k)))
		js_skip(&j);
	same_n("no comma: a fault", j.bad, 1);
	js_init(&j, "{ } ", 4);
	same_n("empty object", js_obj(&j) && !js_key(&j, k, sizeof(k)) && !j.bad, 1);
	js_init(&j, "99999999999999999999", 20);
	same_n("a big number held", js_num(&j, 0), 0x7fffffffL);
	ok("JSON's edges");
}

/* every answer cut short at every length, then bytes changed at random */
static void t_rough(void)
{
	const char *const ans[] = { signin_ans, albums_ans, songs_ans };
	unsigned i, seed = 1;
	LIB_ITEM it[8];
	LIB_ACCOUNT a;
	long total;
	for (i = 0; i < 3; i++) {
		long n = (long)strlen(ans[i]), l, r;
		for (l = 0; l <= n; l++) {
			char *c = malloc((size_t)(l ? l : 1));
			memcpy(c, ans[i], (size_t)l);
			memset(&a, 0, sizeof(a));
			lib_jellyfin.signed_in(&a, c, l);
			lib_jellyfin.items(c, l, it, 8, &total);
			free(c);
		}
		for (r = 0; r < 4000; r++) {
			char *c = malloc((size_t)n);
			int m;
			memcpy(c, ans[i], (size_t)n);
			for (m = 0; m < 4; m++) {
				seed = seed * 1103515245u + 12345u;
				c[(seed >> 8) % (unsigned)n] = (char)(seed >> 20);
			}
			memset(&a, 0, sizeof(a));
			lib_jellyfin.signed_in(&a, c, n);
			lib_jellyfin.items(c, n, it, 8, &total);
			for (m = 0; m < 8; m++)
				if (strlen(it[m].name) >= LIB_NAME || strlen(it[m].id) >= LIB_ID)
					bad = 1;
			free(c);
		}
	}
	ok("answers cut short and changed");
}

int main(void)
{
	t_requests();
	t_answers();
	t_lines();
	t_json();
	t_rough();
	if (bad)
		printf("FAILED\n");
	return bad;
}
