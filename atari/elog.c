/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * elog.c - EPLAY.LOG (elog.h) through common's conn.c logger, as the
 * family's logs (the hub, 9 Nov 2026; Erez: "just like the other
 * applications"): one file beside the program, added to across runs, each
 * line with its time; the network's lines (conn, TLS) in the same file.
 * A line at each start (conn_log_start), the file capped at 256 KB and
 * rolled over to EPLAY.OLD (common's). The lines held in memory (16 KB,
 * conn_log_hold) and written in one go
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
#include "plat.h"

extern const char app_version[];

static int ready;
static int off;		/* Options > Write a log not ticked (guidelines 6.5, Erez 14 Nov 2026) */

void elog_init(void)
{
	if (ready || off)
		return;
	ready = 1;
	snprintf(conn_logfile, sizeof(conn_logfile), "%seplay.log", app_dir());
	conn_log_hold_bytes = 16384;
	conn_log_hold(1);		/* (for the whole run: written at elog_save) */
	crash_report = conn_log_append;	/* (a crash's report in EPLAY.LOG) */
	conn_log_size = pf_handle_size;	/* (the cap's size from the open file: no look) */
	{
		char v[24];
		snprintf(v, sizeof(v), "EPlay %s", app_version);
		conn_log_start(v);	/* (one line a run: the version, the date and time) */
	}
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

void elog_switch(int on)
{
	if (on) {
		off = 0;
		elog_init();
		return;
	}
	/* (nothing more to the disk: the file's name gone, the lines held
	   given back with the buffer, a crash's report CRASH.LOG's alone) */
	off = 1;
	ready = 0;
	conn_logfile[0] = 0;
	conn_log("EPlay", " -- ", "", 0);
	conn_log_hold(0);
	if (crash_report == conn_log_append)
		crash_report = 0;
}

int elog_on(void)
{
	return !off;
}
