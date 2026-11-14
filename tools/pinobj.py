#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""An object's code at a place of its own: each of its .text.NAME sections
(-ffunction-sections) renamed .text.1st.TAG.NAME, which the link
(--sort-section=name) puts straight after the ticks' (.text.0hot.*,
src/tickcode.h) and before all other code. Its own ticks' sections are
left as they are. The start's paths (the list's tags and lengths, inflate,
the DSP's loaders) so keep their place however other code grows (the hub,
14 Nov 2026: 1.36's starts a background turn longer for code that moved).

    pinobj.py OBJCOPY OBJDUMP FILE.o TAG
"""
import subprocess
import sys

objcopy, objdump, obj, tag = sys.argv[1:5]
out = subprocess.run([objdump, "-h", obj], capture_output=True, text=True, check=True).stdout
names = [l.split()[1] for l in out.split("\n") if len(l.split()) > 1 and l.split()[0].isdigit()]
args = []
for n in names:
    if n.startswith(".text.") and not n.startswith((".text.0hot.", ".text.1st.")):
        args += ["--rename-section", "%s=.text.1st.%s.%s" % (n, tag, n[len(".text."):])]
    elif n == ".text":
        args += ["--rename-section", ".text=.text.1st.%s" % tag]
if args:
    subprocess.run([objcopy] + args + [obj], check=True)
