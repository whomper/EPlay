/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_smf.c FILE: src/smf.c's stream for a MIDI file, one record a line
 * ("ms hex bytes"), then "len N" and the info; or "error: ..." (exit 1).
 * tests/smf/run.py makes the files and checks the lines against its own
 * reading of them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smf.h"

int main(int argc, char **argv)
{
	FILE *f;
	long n, need, got, i;
	unsigned char *b, *out;
	char err[80];
	SMF_INFO info, info2;
	if (argc < 2)
		return 2;
	f = fopen(argv[1], "rb");
	if (!f)
		return 2;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc(n ? n : 1);
	if (fread(b, 1, n, f) != (size_t)n)
		return 2;
	fclose(f);
	need = smf_render(b, n, 0, 0, &info, err, sizeof(err));
	if (need < 0) {
		printf("error: %s\n", err);
		free(b);
		return 1;
	}
	out = malloc(need + 1);
	got = smf_render(b, n, out, need, &info2, err, sizeof(err));
	if (got != need || info2.len_ms != info.len_ms || info2.events != info.events) {
		printf("error: counted %ld, made %ld\n", need, got);
		return 1;
	}
	/* (one byte short: refused, not overrun) */
	if (need) {
		/* (in a buffer of its own: what it wrote before it stopped isn't the stream) */
		unsigned char *s = malloc(need);
		long r = smf_render(b, n, s, need - 1, &info2, err, sizeof(err));
		free(s);
		if (r != -2) {
			printf("error: a stream one byte short not refused\n");
			return 1;
		}
	}
	{
		/* the same made a few messages at a time (smf_more): the same
		   bytes, and what each piece says it made whole records */
		unsigned char *s = malloc(need + 1);
		long made = 0, last = 0;
		unsigned long ms;
		int r, pieces = 0;
		SMF_ST *st = malloc(smf_state_size());
		if (smf_begin(st, b, n, s, need, &info2, err, sizeof(err)) < 0) {
			printf("error: smf_begin refused it\n");
			free(s);
			return 1;
		}
		while ((r = smf_more(st, 7, &made, &ms)) == 1) {
			long a = 0;
			pieces++;
			while (a < made)
				a += 5 + s[a + 4];
			if (a != made || made < last || memcmp(s, out, made)) {
				printf("error: piece %d made %ld bytes, not whole records of the stream\n", pieces, made);
				free(s);
				return 1;
			}
			last = made;
		}
		if (r != 0 || made != got || memcmp(s, out, got) || info2.len_ms != info.len_ms) {
			printf("error: in pieces %ld bytes (r %d), whole %ld\n", made, r, got);
			return 1;
		}
		free(s);
		free(st);
	}
	{
		/* the quick length: the stream's, to its ms */
		SMF_ST *st = malloc(smf_state_size());
		SMF_INFO i3;
		long l = smf_length(st, b, n, &i3, err, sizeof(err));
		free(st);
		if (l != -2 && (l != info.len_ms || strcmp(i3.title, info.title) || strcmp(i3.copyright, info.copyright))) {
			printf("error: smf_length %ld \"%s\", the stream's %ld \"%s\"\n", l, i3.title, info.len_ms, info.title);
			return 1;
		}
	}
	/* a record of channel messages: each on a line of its own (the
	   reference reads them one by one) */
	for (i = 0; i < got;) {
		unsigned long ms = (unsigned long)out[i] << 24 | (unsigned long)out[i + 1] << 16 | out[i + 2] << 8 | out[i + 3];
		int k = out[i + 4], j = 0, m;
		const unsigned char *r = out + i + 5;
		if (r[0] >= 0x80 && r[0] < 0xf0) {
			while (j < k) {
				int len = (r[j] & 0xe0) == 0xc0 ? 2 : 3;
				if (r[j] < 0x80 || r[j] >= 0xf0 || j + len > k) {
					printf("error: a record of channel messages broken at %d\n", j);
					return 1;
				}
				printf("%lu", ms);
				for (m = 0; m < len; m++)
					printf(" %02x", r[j + m]);
				printf("\n");
				j += len;
			}
		} else {
			printf("%lu", ms);
			for (j = 0; j < k; j++)
				printf(" %02x", r[j]);
			printf("\n");
		}
		i += 5 + k;
	}
	printf("len %ld\nevents %ld\ntype %d tracks %d channels %04x\ntitle %s\ncopyright %s\n", info.len_ms, info.events,
	       info.type, info.tracks, info.channels, info.title, info.copyright);
	free(b);
	free(out);
	return 0;
}
