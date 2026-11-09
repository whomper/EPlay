/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * about.c - EPlay's About box: the family's (common/ui/aboutbox.c), with
 * its status lines (aboutline.c; the guidelines, 1.3): what plays, and the
 * memory.
 */
#include <stdio.h>
#include "ui.h"
#include "aboutbox.h"
#include "aboutline.h"
#include "icon.h"
#include "icon16.h"
#include "iconify.h"
#include "icon256.h"
#include "play.h"

extern const char app_version[];

void dlg_about(void)
{
	static char what[80], mem[80];
	ABOUT a = { "EPlay", app_version, "An audio player for the Falcon, TT and STE",
		    { what, mem, 0 }, { "MP3 decoded on the Falcon's DSP.", 0, 0 },
		    icon_bits, icon16_px, icon16_rgb, icon256_px, icon256_rgb };
	if (play_state() == PS_PLAYING)
		snprintf(what, sizeof(what), "Playing  \xF9  %s", play_out);
	else
		snprintf(what, sizeof(what), "Not playing");
	about_mem_line(mem, sizeof(mem));
	about_show(&a);
}

/* the iconified window: EPlay's icon, and under it what it does (WM_REDRAW's
   area, 0 for all of it) */
void about_icon_redraw(short h, const GRECT *area, const char *status)
{
	icon_redraw(h, area, icon_bits, icon16_px, status);
}
