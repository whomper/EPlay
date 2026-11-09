/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * term.h - EPlay ending however it ends (quit, a crash, killed under
 * MiNT): the GEMDOS terminate vector (etv_term, Setexc 0x102, so that
 * MiNT keeps it as this program's) takes the tick, the host port's
 * interrupt and the sound away before the program's memory goes. A tester
 * on a CT60 under MiNT, 9 Nov 2026: EPlay killed took the system down.
 */
#ifndef EPLAY_TERM_H
#define EPLAY_TERM_H

void term_install(void);	/* once, at the start (after crash_install) */

#endif
