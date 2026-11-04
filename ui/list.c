/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * list.c - the files in EPlay's list (list.h).
 */
#include <string.h>
#include <stdio.h>
#include "tos.h"
#include "list.h"
#include "tags.h"
#include "probe.h"

#define PER 64				/* records a block */
#define POOL 8192			/* bytes a block of names and tags */

static ENTRY *blk[LIST_MAX / PER];	/* the records' blocks, taken as needed */
static char *pool[64];			/* the names' and tags' blocks */
static int npool;
static long pool_left;			/* bytes left in the last of them */
static int n;
int list_cur = -1, list_sel = -1, list_top;

/* memory for the list: TT RAM where there is some; ST RAM only while its
   largest free block keeps LIST_RESERVE after it (a stock Falcon under
   NVDI with four accessories had 48 KB at the start, and TOS's AES needs
   room to drop a menu: the hub, 4 Nov 2026) */
int list_full;			/* a file not added for that: the window says so */

static void *take(long size)
{
	void *p = Mxalloc(size, 1);	/* TT RAM alone */
	long st;
	if ((long)p > 0)
		return p;
	st = (long)Mxalloc(-1L, 0);
	if (st - size < LIST_RESERVE) {
		list_full = 1;
		return 0;
	}
	p = Mxalloc(size, 0);
	return (long)p > 0 ? p : 0;
}

/* a copy of s in the pool: "" (no memory) if it can't be made */
static const char *keep(const char *s)
{
	long k = (long)strlen(s) + 1;
	char *p;
	if (k == 1)
		return "";
	if (k > pool_left) {
		if (npool >= (int)(sizeof(pool) / sizeof(pool[0])) || !(pool[npool] = take(POOL)))
			return "";
		npool++;
		pool_left = POOL;
	}
	p = pool[npool - 1] + (POOL - pool_left);
	memcpy(p, s, (size_t)k);
	pool_left -= k;
	return p;
}

int list_count(void)
{
	return n;
}

ENTRY *list_at(int i)
{
	return i >= 0 && i < n ? &blk[i / PER][i % PER] : 0;
}

static int same(const char *a, const char *b)
{
	/* (TOS's names: the case doesn't matter) */
	for (; *a && *b; a++, b++) {
		char x = *a, y = *b;
		if (x >= 'a' && x <= 'z')
			x -= 32;
		if (y >= 'a' && y <= 'z')
			y -= 32;
		if (x != y)
			return 0;
	}
	return *a == *b;
}

int list_find(const char *path)
{
	int i;
	for (i = 0; i < n; i++)
		if (same(list_at(i)->path, path))
			return i;
	return -1;
}

int list_add(const char *path)
{
	int i = list_find(path);
	ENTRY *e;
	const char *p;
	if (i >= 0)
		return i;
	if (n >= LIST_MAX)
		return -1;
	if (!blk[n / PER] && !(blk[n / PER] = take((long)PER * sizeof(ENTRY))))
		return -1;
	p = keep(path);
	if (!*p)
		return -1;
	e = &blk[n / PER][n % PER];
	memset(e, 0, sizeof(*e));
	e->path = p;
	e->title = e->artist = e->album = "";
	return n++;
}

void list_remove(int i)
{
	int k;
	if (i < 0 || i >= n)
		return;
	for (k = i; k + 1 < n; k++)
		*list_at(k) = *list_at(k + 1);
	n--;
	if (list_cur == i)
		list_cur = -1;
	else if (list_cur > i)
		list_cur--;
	if (list_sel >= n)
		list_sel = n - 1;
	if (list_top > 0 && list_top >= n)
		list_top = n - 1;
}

/* everything given back: an empty list takes no memory */
static void give_back(void)
{
	int k;
	for (k = 0; k < npool; k++)
		Mfree(pool[k]);
	npool = 0;
	pool_left = 0;
	for (k = 0; k < (int)(sizeof(blk) / sizeof(blk[0])); k++)
		if (blk[k]) {
			Mfree(blk[k]);
			blk[k] = 0;
		}
	n = 0;
}

void list_clear(void)
{
	give_back();
	list_full = 0;
	list_cur = list_sel = -1;
	list_top = 0;
}

void list_keep_only(int i)
{
	char path[200], title[64], artist[64], album[64];
	ENTRY *e = list_at(i), was;
	int j;
	if (!e) {
		list_clear();
		return;
	}
	was = *e;
	snprintf(path, sizeof(path), "%s", e->path);
	snprintf(title, sizeof(title), "%s", e->title);
	snprintf(artist, sizeof(artist), "%s", e->artist);
	snprintf(album, sizeof(album), "%s", e->album);
	list_clear();
	j = list_add(path);
	if (j < 0)
		return;
	e = list_at(j);
	e->title = keep(title);
	e->artist = keep(artist);
	e->album = keep(album);
	e->len_ms = was.len_ms;
	e->read = was.read;
	e->bad = was.bad;
	list_cur = list_sel = j;
}

const char *list_name(const ENTRY *e)
{
	const char *s;
	if (e->title[0])
		return e->title;
	s = strrchr(e->path, '\\');
	return s ? s + 1 : e->path;
}

long list_memory(void)
{
	long k = (long)npool * POOL;
	int b;
	for (b = 0; b < (int)(sizeof(blk) / sizeof(blk[0])); b++)
		if (blk[b])
			k += (long)PER * sizeof(ENTRY);
	return k;
}

static long fh_ = -1;
static long rd(void *u, long at, unsigned char *buf, long k)
{
	(void)u;
	if (Fseek(at, (short)fh_, 0) != at)
		return 0;
	return Fread((short)fh_, k, buf);
}

int list_step(int *row)
{
	static unsigned char buf[PROBE_BUF];
	static TAGS t;
	ENTRY *e = 0;
	long size;
	int i;
	for (i = 0; i < n && (e = list_at(i))->read; i++)
		;
	if (i >= n)
		return 0;
	e->read = 1;
	*row = i;
	fh_ = Fopen(e->path, 0);
	if (fh_ < 0) {
		e->bad = 1;
		return 1;
	}
	size = Fseek(0, (short)fh_, 2);
	tags_read(&t, rd, 0, size);
	/* (the list's columns are 20, 11 and 12 characters: 39 bytes kept at most) */
	t.title[39] = t.artist[39] = t.album[39] = 0;
	e->title = keep(t.title);
	e->artist = keep(t.artist);
	e->album = keep(t.album);
	e->len_ms = probe_len_ms(rd, 0, size, t.id3_len, buf);
	if (e->len_ms < 0) {
		e->bad = 1;
		e->len_ms = 0;
	}
	Fclose((short)fh_);
	fh_ = -1;
	return 1;
}
