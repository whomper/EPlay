/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * launch.c - for play_test.py: Hatari's --auto takes no command line, so
 * this starts C:\EPLAY.PRG with C:\TAIL.TXT's first line as its command
 * line, through the AES (shel_write), as a desktop starts a program with
 * a file dropped on it. Then it ends and the AES starts EPlay.
 */
#include <string.h>
#include "tos.h"
#include "gem.h"

int main(void)
{
	static char tail[128];
	long fh, n;
	short ok;
	appl_init();
	fh = Fopen("C:\\TAIL.TXT", 0);
	n = fh >= 0 ? Fread((short)fh, sizeof(tail) - 2, tail + 1) : 0;
	if (fh >= 0)
		Fclose((short)fh);
	if (n < 0)
		n = 0;
	tail[n + 1] = 0;
	n = (long)strcspn(tail + 1, "\r\n");
	tail[n + 1] = 0;
	tail[0] = (char)n;
	Dsetdrv(2);
	Dsetpath("\\");
	ok = shel_write(1, 1, 0, "C:\\EPLAY.PRG", tail);
	nf_debug(ok ? "LAUNCH shel_write ok\n" : "LAUNCH shel_write failed\n");
	appl_exit();
	return 0;
}
