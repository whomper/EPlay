/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * fbstubs.c - what EPLAYFB.PRG's cmp3fb.c calls besides the decoders,
 * for fbmp3 built as that edition builds it (EPLAY_FB): pf_ms a counter
 * that steps at each call (QEMU times no V4e), elog's format to stderr.
 */
#include "rt.h"

unsigned long pf_ms(void)
{
	static unsigned long t;
	return t += 5;
}

/* (its format alone: the runtime has no printf) */
void elog(const char *fmt, ...)
{
	long n = 0;
	while (fmt[n])
		n++;
	sys_write(2, fmt, n);
	sys_write(2, "\n", 1);
}
