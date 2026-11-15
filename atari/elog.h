/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * elog.h - EPLAY.LOG: what EPlay measured and did (the clock, each file,
 * how it ended), through common's conn.c logger as the family's logs are:
 * added to across runs, next to the program, each line written at once
 * (conn.c, AERA-Atari c9a6896: Erez, 15 Nov 2026, "apps should write log
 * at once as otherwise it may be ineffective to trace errors"), so a crash
 * or a freeze keeps it to its last line.
 * Written only with Options > Write a log ticked (LOG=1 in EPLAY.INF: the
 * guidelines' 6.5, Erez, 14 Nov 2026); each line still goes to the
 * emulator's console (the rigs'), never to the disk with it off.
 * Not from the tick (an interrupt): it calls vsnprintf, not TOS, but the
 * buffer isn't the tick's to touch.
 */
#ifndef ELOG_H
#define ELOG_H

void elog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void elog_init(void);	/* (the first elog does it: conn_logfile set) */
void elog_save(void);
/* Write a log: on, the file written as ever; off, nothing to the disk (the
   lines before EPLAY.INF was read, held in memory, dropped) */
void elog_switch(int on);
int elog_on(void);

#endif
