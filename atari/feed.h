/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * feed.h - a tick 1000 or 250 times a second from the MFP's Timer A, for
 * the sound: the tick refills the sound ring from memory, so that the
 * music doesn't wait for GEM (a menu, the file selector, a dialog, another
 * program). The tick runs in an interrupt: no TOS, GEM or VDI call in it,
 * only memory, the DMA's registers and the DSP's host port.
 */
#ifndef FEED_H
#define FEED_H

/* Timer A taken (its vector and registers kept), hz 1000 or 250: each
   tick costs the stock Falcon's 68030 tens of microseconds, so the
   slower where it is enough */
void feed_on(void (*tick)(void), int hz);
void feed_off(void);			/* given back as they were */

#endif
