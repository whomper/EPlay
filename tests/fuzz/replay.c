/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * replay.c - runs a fuzz target over files (tests/fuzz/found, the seeds)
 * with the build machine's compiler, so what once broke a parser is tried
 * at every make test (the guidelines, 7.1).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n);

int main(int argc, char **argv)
{
	int i, done = 0;
	for (i = 1; i < argc; i++) {
		FILE *f = fopen(argv[i], "rb");
		static uint8_t b[65536];
		size_t n;
		if (!f)
			continue;
		n = fread(b, 1, sizeof(b), f);
		fclose(f);
		LLVMFuzzerTestOneInput(b, n);
		done++;
	}
	printf("fuzz replay: %d inputs, no fault\n", done);
	return 0;
}
