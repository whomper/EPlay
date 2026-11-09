/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * finddlg.h - File > Find station... (Erez, 9 Nov 2026): radio-browser.info's
 * stations by name, genre, country and language, sorted, asked directly or
 * through the Gateway as the Options menu has it (stsearch.h). A double
 * click or Play plays the station; Add puts it in the list and leaves the
 * dialog open.
 */
#ifndef EPLAY_FINDDLG_H
#define EPLAY_FINDDLG_H

/* the dialog: 1 with url (and name) to play, 0 closed. add(url, name) for
   each station added to the list meanwhile */
int find_dialog(char *url, int un, char *name, int nn, void (*add)(const char *url, const char *name));

#endif
