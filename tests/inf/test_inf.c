/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * test_inf.c - src/inf.c: a value read; set in a new file, set again in
 * place, added after other settings with an empty line between; notes and
 * unknown lines kept; a buffer too small refused.
 */
#include <stdio.h>
#include <string.h>
#include "inf.h"

static int bad;

static void same(const char *what, const char *got, long n, const char *want)
{
	if (n != (long)strlen(want) || memcmp(got, want, (size_t)n)) {
		printf("FAIL %s:\n--- got (%ld) ---\n%.*s\n--- wanted ---\n%s\n", what, n, (int)(n > 0 ? n : 0), got,
		       want);
		bad = 1;
	} else
		printf("ok   %s\n", what);
}

#define NOTE "; Output: AUTO, FDI, DSP or 68030\r\n"

int main(void)
{
	char out[512], out2[512], v[16];
	long n, n2;
	const char *other = "; EPlay's settings\r\n\r\n; a setting of a later version\r\nLATER=1\r\n\r\n";
	const char *mixed = "LATER=1\nOUTPUT=FDI\nZZZ=2";

	n = inf_set("", 0, "OUTPUT", "AUTO", NOTE, out, sizeof(out));
	same("a new file", out, n, NOTE "OUTPUT=AUTO\r\n\r\n");
	n2 = inf_set(out, n, "OUTPUT", "DSP", NOTE, out2, sizeof(out2));
	same("set again, in place", out2, n2, NOTE "OUTPUT=DSP\r\n\r\n");
	if (!inf_get(out2, n2, "OUTPUT", v, sizeof(v)) || strcmp(v, "DSP"))
		printf("FAIL read back: %s\n", v), bad = 1;
	else
		printf("ok   read back\n");

	n = inf_set(other, (long)strlen(other), "OUTPUT", "FDI", NOTE, out, sizeof(out));
	same("after other settings", out, n,
	     "; EPlay's settings\r\n\r\n; a setting of a later version\r\nLATER=1\r\n\r\n" NOTE "OUTPUT=FDI\r\n\r\n");
	n = inf_set("LATER=1", 7, "OUTPUT", "FDI", 0, out, sizeof(out));
	same("after a last line with no end", out, n, "LATER=1\r\n\r\nOUTPUT=FDI\r\n\r\n");

	n = inf_set(mixed, (long)strlen(mixed), "OUTPUT", "68030", NOTE, out, sizeof(out));
	same("LF ends and a line after, kept", out, n, "LATER=1\nOUTPUT=68030\nZZZ=2");
	if (inf_get(mixed, (long)strlen(mixed), "OUT", v, sizeof(v)) || inf_get("OUTPUTX=1", 9, "OUTPUT", v, sizeof(v)))
		printf("FAIL a name that only starts the same\n"), bad = 1;
	else
		printf("ok   a name that only starts the same\n");
	if (!inf_get("OUTPUT=AUTO_LONGER", 18, "OUTPUT", v, 5) || strcmp(v, "AUTO"))
		printf("FAIL a value cut to its room: %s\n", v), bad = 1;
	else
		printf("ok   a value cut to its room\n");
	if (inf_set(other, (long)strlen(other), "OUTPUT", "FDI", NOTE, out, 40) != -1)
		printf("FAIL too small a buffer\n"), bad = 1;
	else
		printf("ok   too small a buffer refused\n");
	return bad;
}
