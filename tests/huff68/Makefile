# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# bench.c, huff68.c on a 68030, for bench.py (Hatari)
CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
A       := ../../common/atari
CFLAGS  := -m68030 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables \
           -Wall -nostdinc -isystem $(GCCINC) -I$(A)/include -I$(A) -I. -I../../src/mp3 -I../../build
LDFLAGS := -m68030 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,$(A)/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments
B       := build
all: $(B)/BENCH.PRG
$(B)/BENCH.PRG: $(B)/bench.elf ../../common/tools/elf2tos.py
	python3 ../../common/tools/elf2tos.py $< $@
$(B)/bench.elf: $(B)/crt0.o $(B)/bench.o $(B)/huff68.o $(B)/tos.o $(B)/libc.o $(B)/nf.o
	$(CC) $(LDFLAGS) -o $@ $^ -lgcc
$(B)/%.o: %.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/%.o: $(A)/%.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/crt0.o: $(A)/crt0.S | $(B)
	$(CC) -m68030 -DSTACK_SIZE=4096 -c -o $@ $<
$(B)/nf.o: $(A)/nf.S | $(B)
	$(CC) -m68030 -c -o $@ $<
$(B):
	mkdir -p $@
.PHONY: all
$(B)/huff68.o: huff68.c huff68.h ../../build/mp3tab.h | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
# the host's check against the model, and the granules for the bench
$(B)/check: check.c huff68.c ../../src/mp3/mp3dsp.c ../../src/mp3/mp3frame.c ../../build/mp3tab.h | $(B)
	cc -O2 -w -DMP3DSP_HUFF_HOOK -I. -I../../build -I../../src/mp3 -o $@ check.c huff68.c \
		../../src/mp3/mp3dsp.c ../../src/mp3/mp3frame.c
check: $(B)/check
	./$(B)/check $(wildcard ../../build/mp3files/*.mp3)
.PHONY: check
