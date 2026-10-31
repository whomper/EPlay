# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# What the heaviest parts of an MP3 decode cost the Falcon's DSP, counted
# on Hatari's DSP56001 core: make (fetches the core with common's
# tests/ec/fetch-hatari.sh the first time; needs a56)
H := ../../common/tests/ec/build/hatari
B := build

run: $(B)/sim
	./$(B)/sim
$(B)/bench_prog.h: bench.a56 | $(B)
	cd $(B) && a56 ../bench.a56 >/dev/null && python3 ../../../common/dsp/mkh.py a56.out bench_prog.h dsp_bench_prog bench.a56
$(B)/sim: sim.c $(B)/bench_prog.h $(H)/dsp_core.c
	cc -O2 -w -I$(B) -I$(H) -I$(H)/stub -o $@ sim.c $(H)/dsp_core.c $(H)/dsp_cpu.c
$(H)/dsp_core.c:
	sh ../../common/tests/ec/fetch-hatari.sh
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: run clean
