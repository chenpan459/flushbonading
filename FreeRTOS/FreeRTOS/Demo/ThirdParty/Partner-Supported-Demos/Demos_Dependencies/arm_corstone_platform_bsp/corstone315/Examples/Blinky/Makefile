# Copyright (c) 2024 Arm Limited. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

projname=Blinky
default_args=--packs --update-rte --jobs 8 --rebuild --context .Debug
cbuild=cbuild
model=FVP_Corstone_SSE-315
#model_args=-C mps4_board.uart0.out_file=- --simlimit 10
model_args=

all:
	$(MAKE) clean armclang
	$(MAKE) clean gcc
	$(MAKE) clean iar
	$(MAKE) clean clang

run-all:
	parallel $(model) $(model_args) ::: bins/fvp/*

armclang:
	$(cbuild) $(projname).csolution.yml $(default_args) --toolchain AC6
	mkdir -p bins
	mkdir -p bins/fvp
	cp build/SSE-315-FVP/AC6/Debug/$(projname)/outdir/$(projname).axf bins/$(projname).axf
	fromelf --bin --output bins/$(projname)_armclang.bin bins/$(projname).axf

gcc:
	$(cbuild) $(projname).csolution.yml $(default_args) --toolchain GCC
	mkdir -p bins
	mkdir -p bins/fvp
	cp build/SSE-315-FVP/GCC/Debug/$(projname)/outdir/$(projname).elf bins/$(projname).elf
	arm-none-eabi-objcopy -O binary bins/$(projname).elf bins/$(projname)_gcc.bin

iar:
	$(cbuild) $(projname).csolution.yml $(default_args) --toolchain IAR
	mkdir -p bins
	mkdir -p bins/fvp
	cp build/SSE-315-FVP/IAR/Debug/$(projname)/outdir/$(projname).out bins/$(projname).out
	ielftool --bin bins/Blinky.out bins/Blinky_iar.bin

clang:
	$(cbuild) $(projname).csolution.yml $(default_args) --toolchain CLANG
	mkdir -p bins
	mkdir -p bins/fvp
	cp build/SSE-315-FVP/CLANG/Debug/$(projname)/outdir/$(projname).elf bins/$(projname).clang.elf
	llvm-objcopy -O binary bins/$(projname).clang.elf bins/$(projname)_clang.bin

clean:
	rm -rf $(projname)/$(projname)*.[RD]*+*-*.*
	rm -rf *.cbuild-idx.yml
	rm -rf $(projname)/RTE

clean-build:
	rm -rf build bins

run-armclang:
	$(model) $(model_args) build/SSE-315-FVP/AC6/Debug/$(projname)/outdir/$(projname).axf

run-gcc:
	$(model) $(model_args) build/SSE-315-FVP/GCC/Debug/$(projname)/outdir/$(projname).elf

run-clang:
	$(model) $(model_args) build/SSE-315-FVP/CLANG/Debug/$(projname)/outdir/$(projname).elf

run-iar:
	$(model) $(model_args) build/SSE-315-FVP/IAR/Debug/$(projname)/outdir/$(projname).out
