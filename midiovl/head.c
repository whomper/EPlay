/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * head.c - MIDI.OVL's side of src/midiovl.h: the MIDIOVL table at the
 * start of its text (ovl.ld) and src/smf.c, which uses no library call.
 */
#include "midiovl.h"

#ifndef MIDIOVL_VERSION
#define MIDIOVL_VERSION "0.0"
#endif

__attribute__((section(".midiovl"), used))
const MIDIOVL midiovl = {
	MIDIOVL_MAGIC, MIDIOVL_MAJOR, sizeof(MIDIOVL), MIDIOVL_VERSION, "Standard MIDI Files, types 0, 1 and 2",
	smf_render, smf_state_size, smf_begin, smf_more, smf_length
};
