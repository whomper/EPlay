/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * elog.h - EPLAY.LOG: what EPlay measured and did (the clock, each file,
 * how it ended), through common's conn.c logger as the family's logs are:
 * added to across runs, kept in memory and written next to the program
 * (guidelines 6.5) as it goes, so a crash or a freeze keeps it (Erez, 15
 * Nov 2026): at each file's or station's start, at an error or a warning
 * (elog_warn), at most every 10 s while it plays (elog_tick), at the end.
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
/* an error or a warning: logged and written at once */
void elog_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* (the main loop's, while it plays) lines waiting written, every 10 s at
   most */
void elog_tick(void);
/* Write a log: on, the file written as ever; off, nothing to the disk (the
   lines before EPLAY.INF was read, held in memory, dropped) */
void elog_switch(int on);
int elog_on(void);

#endif
