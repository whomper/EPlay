/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * settings.h - EPlay's Settings (Options > Settings..., ^,) and EPLAY.INF,
 * next to the program (a PRG-only app: the guidelines' 6.1). Output: how
 * EPlay plays (Erez, 2 Nov 2026): Auto, FDI, DSP or 68030. A change takes
 * effect at the next Play; what plays goes on as it is.
 */
#ifndef EPLAY_SETTINGS_H
#define EPLAY_SETTINGS_H

enum { OUT_AUTO, OUT_FDI, OUT_DSP, OUT_68030 };

void settings_load(void);	/* EPLAY.INF, at the start */
void settings_dialog(void);	/* the dialog; saved on OK when it changed */
int settings_output(void);	/* OUT_...: as set, or Auto where the machine hasn't it */
const char *settings_output_name(int out);	/* "Auto", "FDI", ... (the log) */

#endif
