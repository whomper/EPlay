/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * addfiles.h - File > Open... (^O): the Add files dialog (Erez, 4 Nov
 * 2026): several files chosen at once for the list, which TOS's file
 * selector can't give. A dialog of the AES's own objects (3D where the AES
 * has it, in a window on MiNT and MagiC), Selectric's width and ways: the
 * drive in one pop-up at the folder line's start, the last folders, the
 * type, the sort, a name mask, each file's size and date, a selection (a
 * click, Shift-click, a drag), the selected files' count and bytes.
 */
#ifndef EPLAY_ADDFILES_H
#define EPLAY_ADDFILES_H

#define AF_RECENT 10		/* the Folders pop-up's: the last folders added from */

/* the dialog, at folder dir (ending in '\'; "" for EPlay's own) and left
   in it; recent[] the last folders, the newest first, kept up to date
   (each AF_PATH long, "" where none). Each file chosen goes to add(), in
   the list's order; idle() is called every 20 ms or so while it is open
   (the music fed). The number added, 0 when cancelled */
#define AF_PATH 128
int addfiles_run(char *dir, char recent[AF_RECENT][AF_PATH], void (*add)(const char *path), void (*idle)(void));

#endif
