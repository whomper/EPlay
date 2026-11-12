# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The rig's launcher (launch.c): make, then python3 play_test.py WORK.
# PCMTEST.PRG (pcmtest.c): src/pcm.c's 68030 loops against its C, on
# Hatari's 68030 (python3 pcm_test.py WORK)
CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
A       := ../../common/atari
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A)
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
B       := build

all: $(B)/LAUNCH.PRG $(B)/PCMTEST.PRG
.PHONY: mp3cpu
mp3cpu: $(B)/MP3CPU.PRG
$(B)/%.PRG: $(B)/%.elf ../../common/tools/elf2tos.py
	python3 ../../common/tools/elf2tos.py $< $@
$(B)/LAUNCH.elf: $(B)/crt0.o $(B)/launch.o $(B)/gem.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
# (as EPlay builds it: the 68030, -O2)
P030    := -m68030 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -Wall -Wextra -Werror \
           -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A) -I../../src
$(B)/PCMTEST.elf: $(B)/crt0.o $(B)/pcmtest.o $(B)/pcm.o $(B)/pcmtest_c.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
$(B)/pcmtest.o: pcmtest.c ../../src/pcm.h | $(B)
	$(CC) $(P030) -c -o $@ $<
$(B)/pcmtest_c.o: pcmtest_c.c ../../src/pcm.c ../../src/pcm.h | $(B)
	$(CC) $(P030) -c -o $@ $<
$(B)/pcm.o: ../../src/pcm.c ../../src/pcm.h | $(B)
	$(CC) $(P030) -c -o $@ $<
# MP3CPU.PRG (mp3cpu.c): the C model of the DSP's MP3 decoder timed on the
# CPU it is built for, the 68060 (python3 mp3cpu.py WORK FILE.MP3)
P060    := -m68060 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -Wall \
           -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A) -I../../src/mp3 -I../../build
$(B)/MP3CPU.elf: $(B)/crt0.o $(B)/mp3cpu.o $(B)/mul060.o $(B)/mp3frame.o $(B)/mp3dsp.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC) -m68060 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
	    -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments -o $@ $^ -lgcc
$(B)/mp3cpu.o: mp3cpu.c | $(B)
	$(CC) $(P060) -c -o $@ $<
$(B)/mp3frame.o: ../../src/mp3/mp3frame.c | $(B)
	$(CC) $(P060) -c -o $@ $<
$(B)/mp3dsp.o: ../../src/mp3/mp3dsp.c ../../build/mp3tab.h | $(B)
	$(CC) $(P060) -c -o $@ $<
$(B)/%.o: %.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/%.o: $(A)/%.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/crt0.o: $(A)/crt0.S | $(B)
	$(CC) -m68000 -DSTACK_SIZE=4096 -c -o $@ $<
$(B)/nf.o: $(A)/nf.S | $(B)
	$(CC) -m68000 -c -o $@ $<
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: all clean
$(B)/mul060.o: mul060.c | $(B)
	$(CC) $(P060) -c -o $@ $<
# MP3MINI.PRG (mp3mini.c): minimp3 built for the 68060's FPU, timed the same
# way (python3 mp3cpu.py WORK FILE.MP3 6 MP3MINI.PRG)
.PHONY: mp3mini
mp3mini: $(B)/MP3MINI.PRG
P060F   := $(subst -I../../build,-I../mp3/third_party,$(P060)) -ffast-math
$(B)/MP3MINI.elf: $(B)/crt0_64k.o $(B)/mp3mini.o $(B)/mul060.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC) -m68060 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
	    -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments -o $@ $^ -lgcc
$(B)/mp3mini.o: mp3mini.c ../mp3/third_party/minimp3.h | $(B)
	$(CC) $(P060F) -c -o $@ $<
# (minimp3 keeps some 16 KB of scratch on the stack: crt0's 4 KB overran)
$(B)/crt0_64k.o: $(A)/crt0.S | $(B)
	$(CC) -m68000 -DSTACK_SIZE=65536 -c -o $@ $<
