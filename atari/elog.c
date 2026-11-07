/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * elog.c - EPLAY.LOG (elog.h): lines in a buffer of 8 KB (the oldest
 * dropped past it, the first 2 KB kept: the start says the machine),
 * the file written whole each time elog_save is asked. Each line also
 * goes to the emulator's console (nf_debug), for the rigs.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "elog.h"

#define SIZE 8192
#define KEEP 2048

static char buf[SIZE];
static long used;

void elog(const char *fmt, ...)
{
	char line[600];
	va_list ap;
	long n;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line) - 2, fmt, ap);
	va_end(ap);
	n = (long)strlen(line);
	line[n++] = '\r';
	line[n++] = '\n';
	line[n] = 0;
	nf_debug("LOG ");
	nf_debug(line);
	if (used + n > SIZE) {		/* the middle dropped */
		long drop = used + n - SIZE;
		if (drop < 1024)
			drop = 1024;
		if (drop > used - KEEP)
			drop = used - KEEP;
		memmove(buf + KEEP, buf + KEEP + drop, (size_t)(used - KEEP - drop));
		used -= drop;
	}
	memcpy(buf + used, line, (size_t)n);
	used += n;
}

void elog_save(void)
{
	char path[256];
	long fh;
	snprintf(path, sizeof(path), "%seplay.log", app_dir());
	fh = Fcreate(path, 0);
	if (fh < 0)
		return;
	Fwrite((short)fh, used, buf);
	Fclose((short)fh);
}
