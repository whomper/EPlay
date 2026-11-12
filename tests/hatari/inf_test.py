#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""inf_test.py WORK [MACHINE]: EPLAY.INF is written only when a setting
changed (guidelines 6.1, Erez 12 Nov 2026). play_test's quit case three
times: with no eplay.inf (one written); with the one written, unchanged
(the file untouched: its date and bytes as they were); with its WINDOW
line taken out (EPlay keeps its window's place: written again)."""
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "common", "tools"))
sys.path.insert(0, HERE)
import machines  # noqa: E402
import play_test  # noqa: E402


def inf_of(work, spec):
    hd = os.path.join(work, spec.replace(":", "-") + "-quit", "hd")
    return machines.ci_path(hd, "eplay.inf")


def main():
    work = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "/tmp/eplay-inf")
    spec = sys.argv[2] if len(sys.argv) > 2 else machines.ERZ_MACHINE
    keep = os.path.join(work, "kept")
    os.makedirs(keep, exist_ok=True)
    fails = 0

    ok, what, found = play_test.run(os.path.join(work, "1"), "quit", spec)
    first = inf_of(os.path.join(work, "1"), spec)
    if not ok or not os.path.exists(first):
        print("FAIL  no eplay.inf written by a first start and quit (%s)" % found)
        return 1
    a = os.path.join(keep, "a.inf")
    shutil.copy(first, a)
    os.utime(a, (time.time() - 3600, time.time() - 3600))	# (an hour ago: a write would show)
    print("ok    a first start and quit wrote eplay.inf (%d bytes)" % os.path.getsize(a))

    os.environ["EPLAY_RIG_INF_FILE"] = a
    ok, what, found = play_test.run(os.path.join(work, "2"), "quit", spec)
    second = inf_of(os.path.join(work, "2"), spec)
    same = ok and os.path.exists(second) and int(os.path.getmtime(second)) == int(os.path.getmtime(a)) and \
        open(second, "rb").read() == open(a, "rb").read()
    print("%s  nothing changed: eplay.inf %s" % ("ok  " if same else "FAIL",
                                                  "untouched (its date and bytes)" if same else "written again"))
    fails += not same

    b = os.path.join(keep, "b.inf")
    with open(b, "wb") as fh:
        fh.write(b"".join(l for l in open(a, "rb").read().splitlines(True) if not l.startswith(b"WINDOW=")))
    os.utime(b, (time.time() - 3600, time.time() - 3600))
    os.environ["EPLAY_RIG_INF_FILE"] = b
    ok, what, found = play_test.run(os.path.join(work, "3"), "quit", spec)
    third = inf_of(os.path.join(work, "3"), spec)
    wrote = ok and os.path.exists(third) and int(os.path.getmtime(third)) != int(os.path.getmtime(b)) and \
        b"WINDOW=" in open(third, "rb").read()
    print("%s  the window's place not in it: eplay.inf %s" % ("ok  " if wrote else "FAIL",
                                                              "written, WINDOW= back" if wrote else "not written"))
    fails += not wrote
    print("all passed" if not fails else "%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
