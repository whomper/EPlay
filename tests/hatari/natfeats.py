# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""natfeats.py - NATFEATS=1 in the eplay.inf a rig sets up: EPlay looks for
the emulator's console (NatFeats) only then (Erez, 15 Nov 2026: "a .INF
setting so that it can be added in our testing but have no bearing
outside"), and the rigs read what EPlay says there."""
import os


def natfeats(hd):
    """eplay.inf in hd made or added to with NATFEATS=1 (as it is when it
    has it already: inf_test compares it)"""
    path = os.path.join(hd, "eplay.inf")
    for f in os.listdir(hd):
        if f.lower() == "eplay.inf":
            path = os.path.join(hd, f)
    text = open(path, newline="", errors="replace").read() if os.path.exists(path) else ""
    if os.environ.get("EPLAY_RIG_NONATFEATS"):
        return              # (a check that EPlay says nothing then: no probe)
    if "NATFEATS=1" not in text:
        with open(path, "a", newline="") as fh:
            fh.write("NATFEATS=1\r\n\r\n")
