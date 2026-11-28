/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * libjf.c - Jellyfin as a library platform (lib.h). Every call carries the
 * Authorization header Jellyfin's own clients send ("MediaBrowser
 * Client=..., Token=..."); the stream's URL carries the key as ApiKey, as
 * the player fetches it with no header of ours. Lists ask for the few
 * fields shown and for no user data, so the answers stay small.
 */
#include <string.h>
#include "json.h"
#include "lib.h"
#include "libtext.h"

static int auth(const LIB_ACCOUNT *a, const char *version, LIB_REQ *r)
{
	int n = sizeof(r->auth), k;
	k = lt_s(r->auth, 0, n, "MediaBrowser Client=\"EPlay\", Device=\"Atari\", DeviceId=\"");
	k = lt_s(r->auth, k, n, a->device);
	k = lt_s(r->auth, k, n, "\", Version=\"");
	k = lt_s(r->auth, k, n, version);
	k = lt_s(r->auth, k, n, "\"");
	if (a->token[0]) {
		k = lt_s(r->auth, k, n, ", Token=\"");
		k = lt_s(r->auth, k, n, a->token);
		k = lt_s(r->auth, k, n, "\"");
	}
	return k < 0 ? -1 : 0;
}

static int signin(const LIB_ACCOUNT *a, const char *password, const char *version, LIB_REQ *r)
{
	LIB_ACCOUNT out = *a;
	int n = sizeof(r->body), k;
	memset(r, 0, sizeof(*r));
	out.token[0] = 0;
	r->post = 1;
	lt_s(r->path, 0, sizeof(r->path), "/Users/AuthenticateByName");
	k = lt_s(r->body, 0, n, "{\"Username\":");
	k = js_put(r->body, k, n, a->user);
	k = lt_s(r->body, k, n, ",\"Pw\":");
	k = js_put(r->body, k, n, password);
	k = lt_s(r->body, k, n, "}");
	return k < 0 || auth(&out, version, r) < 0 ? -1 : 0;
}

/* an id as Jellyfin gives it: hex and '-', LIB_ID at most */
static int good_id(const char *s)
{
	int k = 0;
	for (; *s; s++, k++)
		if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F') || *s == '-'))
			return 0;
	return k > 0 && k < LIB_ID;
}

static int signed_in(LIB_ACCOUNT *a, const char *text, long len)
{
	JS j;
	char key[24], uid[LIB_ID], token[sizeof(a->token)];
	uid[0] = token[0] = 0;
	js_init(&j, text, len);
	if (!js_obj(&j))
		return 0;
	while (js_key(&j, key, sizeof(key)))
		if (!strcmp(key, "AccessToken"))
			js_str(&j, token, sizeof(token));
		else if (!strcmp(key, "User") && js_peek(&j) == '{') {
			js_obj(&j);
			while (js_key(&j, key, sizeof(key)))
				if (!strcmp(key, "Id"))
					js_str(&j, uid, sizeof(uid));
				else
					js_skip(&j);
		} else
			js_skip(&j);
	if (j.bad || !good_id(uid) || !good_id(token) || strlen(token) >= sizeof(a->token) - 1)
		return 0;
	strcpy(a->uid, uid);
	strcpy(a->token, token);
	return 1;
}

static int list(const LIB_ACCOUNT *a, const LIB_QUERY *q, const char *version, LIB_REQ *r)
{
	char *p = r->path;
	int n = sizeof(r->path), k;
	memset(r, 0, sizeof(*r));
	if (!good_id(a->uid) || (q->what == LQ_IN && (!q->parent || !good_id(q->parent))))
		return -1;
	if (q->what == LQ_ARTISTS)
		k = lt_s(p, 0, n, "/Artists/AlbumArtists?SortBy=SortName");
	else if (q->what == LQ_IN && q->parent_kind == LK_PLAYLIST) {
		k = lt_s(p, 0, n, "/Playlists/");
		k = lt_s(p, k, n, q->parent);
		k = lt_s(p, k, n, "/Items?MediaTypes=Audio");	/* (in the playlist's order) */
	} else {
		k = lt_s(p, 0, n, "/Items?Recursive=true");
		if (q->what == LQ_ALBUMS)
			k = lt_s(p, k, n, "&IncludeItemTypes=MusicAlbum&SortBy=SortName");
		else if (q->what == LQ_PLAYLISTS)
			k = lt_s(p, k, n, "&IncludeItemTypes=Playlist&MediaTypes=Audio&SortBy=SortName");
		else if (q->what == LQ_SONGS)
			k = lt_s(p, k, n, "&IncludeItemTypes=Audio&SortBy=SortName");
		else if (q->what == LQ_IN && q->parent_kind == LK_ARTIST) {
			k = lt_s(p, k, n, "&IncludeItemTypes=MusicAlbum&SortBy=ProductionYear,SortName&AlbumArtistIds=");
			k = lt_s(p, k, n, q->parent);
		} else if (q->what == LQ_IN && q->parent_kind == LK_ALBUM) {
			k = lt_s(p, k, n, "&IncludeItemTypes=Audio&SortBy=ParentIndexNumber,IndexNumber,SortName&ParentId=");
			k = lt_s(p, k, n, q->parent);
		} else
			return -1;
	}
	k = lt_s(p, k, n, "&userId=");
	k = lt_s(p, k, n, a->uid);
	k = lt_s(p, k, n, "&Fields=ChildCount&EnableUserData=false&EnableImageTypes=Primary&ImageTypeLimit=1");
	if (q->find && q->find[0]) {
		k = lt_s(p, k, n, "&searchTerm=");
		k = lib_enc(p, k, n, q->find);
	}
	k = lt_s(p, k, n, "&StartIndex=");
	k = lt_n(p, k, n, q->start);
	k = lt_s(p, k, n, "&Limit=");
	k = lt_n(p, k, n, q->count > 0 ? q->count : 50);
	return k < 0 || auth(a, version, r) < 0 ? -1 : 0;
}

/* ImageTags: {"Primary":"..."} has a Primary */
static int has_primary(JS *j)
{
	char key[16];
	int yes = 0;
	if (js_peek(j) != '{') {
		js_skip(j);
		return 0;
	}
	js_obj(j);
	while (js_key(j, key, sizeof(key))) {
		if (!strcmp(key, "Primary") && js_peek(j) == '"')
			yes = 1;
		js_skip(j);
	}
	return yes;
}

/* the first string of an array (Artists: ["..."]) */
static void first_text(JS *j, char *out, int n)
{
	out[0] = 0;
	if (js_peek(j) != '[') {
		js_skip(j);
		return;
	}
	js_arr(j);
	while (js_item(j))
		if (!out[0] && js_peek(j) == '"')
			js_text(j, out, n);
		else
			js_skip(j);
}

/* one item (its '{' next) into it; 1 if it's one to show */
static int item(JS *j, LIB_ITEM *it)
{
	char key[24], type[16], album_id[LIB_ID], album_tag[4], artist2[LIB_SUB];
	int own_pic = 0;
	memset(it, 0, sizeof(*it));
	type[0] = album_id[0] = album_tag[0] = artist2[0] = 0;
	if (!js_obj(j))
		return 0;
	while (js_key(j, key, sizeof(key))) {
		if (!strcmp(key, "Id"))
			js_str(j, it->id, sizeof(it->id));
		else if (!strcmp(key, "Name"))
			js_text(j, it->name, sizeof(it->name));
		else if (!strcmp(key, "Type"))
			js_str(j, type, sizeof(type));
		else if (!strcmp(key, "AlbumArtist"))
			js_text(j, it->artist, sizeof(it->artist));
		else if (!strcmp(key, "Artists"))
			first_text(j, artist2, sizeof(artist2));
		else if (!strcmp(key, "Album"))
			js_text(j, it->album, sizeof(it->album));
		else if (!strcmp(key, "AlbumId"))
			js_str(j, album_id, sizeof(album_id));
		else if (!strcmp(key, "AlbumPrimaryImageTag"))
			js_str(j, album_tag, sizeof(album_tag));
		else if (!strcmp(key, "RunTimeTicks"))
			it->len_ms = js_num(j, 4);	/* (100 ns: 10^4 to the ms) */
		else if (!strcmp(key, "ChildCount"))
			it->count = (int)js_num(j, 0);
		else if (!strcmp(key, "ProductionYear"))
			it->year = (int)js_num(j, 0);
		else if (!strcmp(key, "IndexNumber"))
			it->track = (int)js_num(j, 0);
		else if (!strcmp(key, "ImageTags"))
			own_pic = has_primary(j);
		else
			js_skip(j);
	}
	if (j->bad || !good_id(it->id) || !it->name[0])
		return 0;
	it->kind = !strcmp(type, "MusicArtist") ? LK_ARTIST : !strcmp(type, "MusicAlbum") ? LK_ALBUM :
		   !strcmp(type, "Playlist") ? LK_PLAYLIST : !strcmp(type, "Audio") ? LK_SONG : -1;
	if (it->kind < 0)
		return 0;
	if (!it->artist[0])
		strcpy(it->artist, artist2);
	if (own_pic)
		strcpy(it->pic, it->id);
	else if (album_tag[0] && good_id(album_id))
		strcpy(it->pic, album_id);
	if (it->year < 0 || it->year > 9999)
		it->year = 0;
	if (it->count < 0)
		it->count = 0;
	return 1;
}

static int items(const char *text, long len, LIB_ITEM *it, int max, long *total)
{
	JS j;
	char key[24];
	int n = 0;
	*total = -1;
	js_init(&j, text, len);
	if (!js_obj(&j))
		return 0;
	while (js_key(&j, key, sizeof(key)))
		if (!strcmp(key, "Items") && js_peek(&j) == '[') {
			js_arr(&j);
			while (js_item(&j))
				if (n < max && item(&j, &it[n]))
					n++;
				else if (j.bad)
					break;
				else if (n >= max)
					js_skip(&j);
		} else if (!strcmp(key, "TotalRecordCount"))
			*total = js_num(&j, 0);
		else
			js_skip(&j);
	return n;
}

static int stream(const LIB_ACCOUNT *a, const char *id, long at_ms, char *out, int n)
{
	int k;
	if (!good_id(id) || !a->token[0])
		return -1;
	k = lt_s(out, 0, n, a->server);
	k = lt_s(out, k, n, "/Audio/");
	k = lt_s(out, k, n, id);
	k = lt_s(out, k, n, "/stream.mp3?AudioCodec=mp3&MaxAudioChannels=2&AudioBitRate=");
	k = lt_n(out, k, n, (long)(a->kbps == 128 || a->kbps == 320 ? a->kbps : 192) * 1000);
	if (at_ms > 0) {
		k = lt_s(out, k, n, "&StartTimeTicks=");
		k = lt_n(out, k, n, at_ms);
		k = lt_s(out, k, n, "0000");
	}
	k = lt_s(out, k, n, "&DeviceId=");
	k = lt_s(out, k, n, a->device);
	k = lt_s(out, k, n, "&ApiKey=");
	k = lt_s(out, k, n, a->token);
	return k;
}

static int picture(const LIB_ACCOUNT *a, const char *id, int px, char *out, int n)
{
	int k;
	if (!good_id(id))
		return -1;
	k = lt_s(out, 0, n, a->server);
	k = lt_s(out, k, n, "/Items/");
	k = lt_s(out, k, n, id);
	k = lt_s(out, k, n, "/Images/Primary?format=Jpg&quality=85&maxWidth=");
	k = lt_n(out, k, n, px);
	k = lt_s(out, k, n, "&maxHeight=");
	k = lt_n(out, k, n, px);
	return k;
}

const LIB_PLATFORM lib_jellyfin = { "Jellyfin", "jellyfin", signin, signed_in, list, items, stream, picture };
