#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Nuked-OPN2 (LGPL-2.1+, Nuke.YKT), a cycle-exact YM2612 (YM3438), the
# reference EPlay's YM2612 is tested against (Erez allowed it, 10 Nov 2026):
# fetched here for the host tests, never committed nor built into EPlay
set -e
cd "$(dirname "$0")"
[ -f build/Nuked-OPN2/ym3438.c ] && exit 0
mkdir -p build
git clone -q https://github.com/nukeykt/Nuked-OPN2 build/Nuked-OPN2
git -C build/Nuked-OPN2 checkout -q 335747d78cb0abbc3b55b004e62dad9763140115
