/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * feedxb.h - feed.h's calls with no interrupt, in EPLAY.PRG (feedfb.c
 * built again as feedxb.o): where the sound is the XBIOS's alone (snd_xb:
 * a FireBee's, a Milan's), feed.c gives each call to these, and the tick
 * runs from GEM's turns (fx_poll), so that nothing touches the MFP.
 */
#ifndef EPLAY_FEEDXB_H
#define EPLAY_FEEDXB_H

void fx_t0(void);
long fx_us(void);
void fx_term(void);
void fx_on(void (*tick)(void), int hz);
void fx_off(void);
void fx_poll(void);

#endif
