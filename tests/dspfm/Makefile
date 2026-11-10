# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# fm.a56 on Hatari's DSP core: what a YM2612-like sample costs (phase 9,
# step 3, a measurement for Erez): make
H := ../../common/tests/ec/build/hatari
B := build
TOP := ../..

run: $(B)/sim
	./$(B)/sim
$(B)/fm_prog.h: fm.a56 | $(B)
	cd $(B) && a56 ../fm.a56 > a56.lst && ! grep -q 'warning:\|error:' a56.lst && \
		python3 ../$(TOP)/common/dsp/mkh.py a56.out fm_prog.h dsp_fm_prog fm.a56
$(B)/sim: sim.c ../dspsim.h $(B)/fm_prog.h $(H)/dsp_core.c
	cc -O2 -w -I$(B) -I$(H) -I$(H)/stub -o $@ sim.c $(H)/dsp_core.c $(H)/dsp_cpu.c
$(H)/dsp_core.c:
	sh $(TOP)/common/tests/ec/fetch-hatari.sh
$(B):
	mkdir -p $@
.PHONY: run
