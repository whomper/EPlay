/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * xalloc.h - Mxalloc where GEMDOS has it, Malloc where it hasn't: TOS 1.x
 * (an ST's 1.04, an STE's 1.62) answers Mxalloc with EINVFN (-32), and
 * EPlay 1.38 and before took that for no memory at all on a real STE ("the
 * list full at 0 files"; EmuTOS has Mxalloc, so the rigs never saw it until
 * they ran on Erez's TOS 1.62, 15 Nov 2026). There all RAM is ST RAM: mode
 * 0 and 3 are Malloc's; mode 1 (TT RAM alone) has none.
 */
#ifndef EPLAY_XALLOC_H
#define EPLAY_XALLOC_H

#include "tos.h"

static inline void *x_alloc(long n, int mode)
{
	void *p = Mxalloc(n, mode);
	if ((long)p == -32)
		p = (mode & 3) == 1 ? 0 : Malloc(n);
	return p;
}

#endif
