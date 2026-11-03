/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_probe.c - src/probe.c on a file: its length must be MS, within
 * SLACK ms (an MP3 without a Xing frame is guessed from its bit rate).
 *
 *     test_probe FILE MS SLACK
 */
#include <stdio.h>
#include <stdlib.h>
#include "probe.h"

static long rd(void *u, long at, unsigned char *buf, long n)
{
	FILE *f = u;
	if (fseek(f, at, SEEK_SET))
		return 0;
	return (long)fread(buf, 1, (size_t)n, f);
}

int main(int argc, char **argv)
{
	static unsigned char buf[PROBE_BUF];
	TAGS t;
	FILE *f;
	long size, ms, want, slack;
	if (argc != 4)
		return 2;
	f = fopen(argv[1], "rb");
	if (!f)
		return 2;
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	tags_read(&t, rd, f, size);
	ms = probe_len_ms(rd, f, size, t.id3_len, buf);
	fclose(f);
	want = atol(argv[2]);
	slack = atol(argv[3]);
	printf("%-28s %6ld ms (wanted %ld): %s\n", argv[1], ms, want,
	       labs(ms - want) <= slack ? "ok" : "WRONG");
	return labs(ms - want) <= slack ? 0 : 1;
}
