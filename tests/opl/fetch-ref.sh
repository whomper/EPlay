#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Nuked-OPL3 (LGPL-2.1+, Nuke.YKT), a cycle-exact YMF262 that runs OPL2 files
# as an OPL2 does: the reference EPlay's OPL2 is tested against, as
# Nuked-OPN2 is for its YM2612: fetched here for the host tests, never
# committed nor built into EPlay
set -e
cd "$(dirname "$0")"
[ -f build/Nuked-OPL3/opl3.c ] && exit 0
mkdir -p build
git clone -q https://github.com/nukeykt/Nuked-OPL3 build/Nuked-OPL3
git -C build/Nuked-OPL3 checkout -q 765ec962e473aeb767e4cba74ffdc8f588ffbfe8
