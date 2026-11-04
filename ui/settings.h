/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * settings.h - EPlay's Settings (Options > Settings..., ^,) and EPLAY.INF,
 * next to the program (a PRG-only app: the guidelines' 6.1). Output: how
 * EPlay plays (Erez, 2 Nov 2026): Auto, FDI or DSP (the 68030 dropped: Erez,
 * 3 Nov 2026). A change takes
 * effect at the next Play; what plays goes on as it is.
 */
#ifndef EPLAY_SETTINGS_H
#define EPLAY_SETTINGS_H

enum { OUT_AUTO, OUT_FDI, OUT_DSP };
/* Quality (Erez, 2 Nov 2026): High, the default, or Normal: up to about 15
   kHz, where the machine can't do more (the DSP on the Falcon's clock);
   with the FDI the music is always whole */
enum { Q_NORMAL, Q_HIGH };

void settings_load(void);	/* EPLAY.INF, at the start */
void settings_dialog(void);	/* the dialog; saved on OK when it changed */
int settings_output(void);	/* OUT_...: as set, or Auto where the machine hasn't it */
const char *settings_output_name(int out);	/* "Auto", "FDI", ... (the log) */
int settings_quality(void);	/* Q_NORMAL or Q_HIGH */
const char *settings_quality_name(int q);	/* "Normal", "High" (the log) */

/* the volume (0 silent to VOL_FULL, steps of 1.5 dB), VOLUME= in the INF,
   saved by settings_flush at the end when it changed */
#define VOL_FULL 16
int settings_volume(void);
void settings_set_volume(int v);
void settings_flush(void);

/* the Add files dialog's folder and its last folders (ADDDIR=, FOLDER1=
   to FOLDER10= in the INF), saved by settings_flush once changed */
#include "addfiles.h"
char *settings_add_dir(void);
char (*settings_recent(void))[AF_PATH];
void settings_recent_changed(void);

#endif
