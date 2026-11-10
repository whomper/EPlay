# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# dsp/sn.a56, the SN76489 on the DSP (phase 9), on Hatari's DSP core: its
# tone's pitch and level, the noise, silence (sim.c)
H := ../../common/tests/ec/build/hatari
B := build
TOP := ../..

run: $(B)/sim
	./$(B)/sim
$(B)/sn_prog.h: $(TOP)/dsp/sn.a56 | $(B)
	cd $(B) && a56 ../$(TOP)/dsp/sn.a56 > a56.lst && ! grep -q 'warning:\|error:' a56.lst && \
		python3 ../$(TOP)/common/dsp/mkh.py a56.out sn_prog.h dsp_sn_prog sn.a56
$(B)/sim: sim.c ../dspsim.h $(B)/sn_prog.h $(H)/dsp_core.c
	cc -O2 -w -I$(B) -I$(H) -I$(H)/stub -o $@ sim.c $(H)/dsp_core.c $(H)/dsp_cpu.c -lm
$(H)/dsp_core.c:
	sh $(TOP)/common/tests/ec/fetch-hatari.sh
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: run clean
