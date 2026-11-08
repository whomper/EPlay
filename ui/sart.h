/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * sart.h - a station's picture (sart.c): the address it names with its
 * titles (ICY StreamUrl), fetched directly or through the Gateway, shown
 * in the art square.
 */
#ifndef EPLAY_SART_H
#define EPLAY_SART_H

void sart_init(void (*msg)(const short *msg));	/* the window's messages during a fetch */
void sart_reset(void);		/* a station opened or closed */
void sart_want(const char *url);	/* the picture's address now ("" none) */
/* a station opened: its address and name, for its logo through the
   Gateway (when one is given) if it names no picture of its own */
void sart_station(const char *url, const char *name);
/* from the main loop: the picture wanted fetched and handed to art.c (its
   art_step's unpack it); 1 when the art square should be drawn again */
int sart_step(void);

#endif
