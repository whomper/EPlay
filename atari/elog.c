/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * elog.c - EPLAY.LOG (elog.h) through common's conn.c logger, as the
 * family's logs (the hub, 9 Nov 2026; Erez: "just like the other
 * applications"): one file beside the program, added to across runs, each
 * line with its time; the network's lines (conn, TLS) in the same file.
 * The lines held in memory (16 KB, conn_log_hold) and written in one go
 * (the guidelines, 6.5) at elog_save, when the buffer is full and at an
 * error; a crash's report goes in it too. Each line also goes to the
 * emulator's console (nf_debug), for the rigs.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "tos.h"
#include "gem.h"
#include "conn.h"
#include "crash.h"
#include "elog.h"

static int ready;

void elog_init(void)
{
	if (ready)
		return;
	ready = 1;
	snprintf(conn_logfile, sizeof(conn_logfile), "%seplay.log", app_dir());
	conn_log_hold_bytes = 16384;
	conn_log_hold(1);		/* (for the whole run: written at elog_save) */
	crash_report = conn_log_append;	/* (a crash's report in EPLAY.LOG) */
}

void elog(const char *fmt, ...)
{
	char line[600];
	va_list ap;
	long n;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line) - 2, fmt, ap);
	va_end(ap);
	n = (long)strlen(line);
	elog_init();
	conn_log("EPlay", " -- ", line, n);
	line[n++] = '\r';
	line[n++] = '\n';
	line[n] = 0;
	nf_debug("LOG ");
	nf_debug(line);
}

void elog_save(void)
{
	conn_log_flush();
}
