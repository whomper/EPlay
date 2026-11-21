#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""mkvgms.py - the VGMs fbvgm plays (tests/fb's Makefile): the rig's own
(tests/hatari/play_test.py's make_vgm), three seconds each, unpacked.

    mkvgms.py DIR NAME...    (ay sn opn md opl opl3)
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "hatari"))
import play_test  # noqa: E402

KINDS = {"ay": {}, "sn": {"sn": True}, "opn": {"opn": True}, "md": {"md": True},
         "opl": {"opl": True}, "opl3": {"opl3": True}}
for name in sys.argv[2:]:
    play_test.make_vgm(os.path.join(sys.argv[1], name + ".vgm"), 3, False, **KINDS[name])
