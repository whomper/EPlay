/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * elog.h - EPLAY.LOG: what EPlay measured and did (the clock, each file,
 * how it ended), through common's conn.c logger as the family's logs are:
 * added to across runs, kept in memory and written next to the program in
 * one go (guidelines 6.5), at the end and when something went wrong.
 * Always on, so a run on Erez's Falcon says what happened there.
 * Not from the tick (an interrupt): it calls vsnprintf, not TOS, but the
 * buffer isn't the tick's to touch.
 */
#ifndef ELOG_H
#define ELOG_H

void elog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void elog_init(void);	/* (the first elog does it: conn_logfile set) */
void elog_save(void);

#endif
