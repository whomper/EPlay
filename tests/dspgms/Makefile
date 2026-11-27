# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# dsp/gms.a56, the internal synth's render on the DSP, on Hatari's DSP core:
# against src/gmsdsp.c's model bit for bit, and its time (sim.c)
H := ../../common/tests/ec/build/hatari
B := build
TOP := ../..

run: $(B)/sim
	./$(B)/sim $(TOP)/build/gm16.dat
$(B)/gms_prog.h: $(TOP)/dsp/gms.a56 | $(B)
	cd $(B) && a56 ../$(TOP)/dsp/gms.a56 > a56.lst && ! grep -q 'warning:\|error:' a56.lst && \
		python3 ../$(TOP)/common/dsp/mkh.py a56.out gms_prog.h dsp_gms_prog gms.a56
$(B)/sim: sim.c ../dspsim.h $(B)/gms_prog.h $(H)/dsp_core.c $(TOP)/src/gmsdsp.c $(TOP)/src/gmsdsp.h \
		$(TOP)/src/gmsynth.c $(TOP)/src/gmset.c
	cc -O2 -w -I$(B) -I$(H) -I$(H)/stub -I$(TOP)/src -o $@ sim.c $(H)/dsp_core.c $(H)/dsp_cpu.c \
		$(TOP)/src/gmsdsp.c $(TOP)/src/gmsynth.c $(TOP)/src/gmset.c -lm
$(H)/dsp_core.c:
	sh $(TOP)/common/tests/ec/fetch-hatari.sh
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: run clean
