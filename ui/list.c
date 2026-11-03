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

static ENTRY *ent;
static int n;
int list_cur = -1, list_sel = -1, list_top;

int list_init(void)
{
	long size = (long)LIST_MAX * sizeof(ENTRY);
	ent = Mxalloc(size, 3);		/* (TT RAM where there is some: not the DMA's) */
	if ((long)ent <= 0)
		ent = Malloc(size);
	if ((long)ent <= 0) {
		ent = 0;
		return 0;
	}
	n = 0;
	return 1;
}

int list_count(void)
{
	return n;
}

ENTRY *list_at(int i)
{
	return ent && i >= 0 && i < n ? &ent[i] : 0;
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
		if (same(ent[i].path, path))
			return i;
	return -1;
}

int list_add(const char *path)
{
	int i = list_find(path);
	ENTRY *e;
	if (i >= 0)
		return i;
	if (!ent || n >= LIST_MAX || strlen(path) >= sizeof(e->path))
		return -1;
	e = &ent[n];
	memset(e, 0, sizeof(*e));
	strcpy(e->path, path);
	return n++;
}

void list_remove(int i)
{
	if (i < 0 || i >= n)
		return;
	memmove(&ent[i], &ent[i + 1], (size_t)(n - i - 1) * sizeof(ENTRY));
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

void list_clear(void)
{
	n = 0;
	list_cur = list_sel = -1;
	list_top = 0;
}

const char *list_name(const ENTRY *e)
{
	const char *s;
	if (e->title[0])
		return e->title;
	s = strrchr(e->path, '\\');
	return s ? s + 1 : e->path;
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
	TAGS t;
	ENTRY *e;
	long size;
	int i;
	for (i = 0; i < n && ent[i].read; i++)
		;
	if (i >= n)
		return 0;
	e = &ent[i];
	e->read = 1;
	*row = i;
	fh_ = Fopen(e->path, 0);
	if (fh_ < 0) {
		e->bad = 1;
		return 1;
	}
	size = Fseek(0, (short)fh_, 2);
	tags_read(&t, rd, 0, size);
	snprintf(e->title, sizeof(e->title), "%s", t.title);
	snprintf(e->artist, sizeof(e->artist), "%s", t.artist);
	snprintf(e->album, sizeof(e->album), "%s", t.album);
	e->len_ms = probe_len_ms(rd, 0, size, t.id3_len, buf);
	if (e->len_ms < 0) {
		e->bad = 1;
		e->len_ms = 0;
	}
	Fclose((short)fh_);
	fh_ = -1;
	return 1;
}
