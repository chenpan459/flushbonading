# Copyright (c) 2024 Arm Limited. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

projname=Blinky
default_args=--packs --update-rte --jobs 8
cbuild=cbuild
model=FVP_Corstone_SSE-310_Ethos-U65
#model_args=-C mps3_board.uart0.out_file=- --simlimit 10
model_args=

all:
	$(MAKE) clean armclang
	$(MAKE) clean gcc
	$(MAKE) clean iar
	$(MAKE) clean clang

run-all:
	parallel $(model) $(model_args) ::: bins/fvp/*

armclang:
	$(cbuild) $(projname).csolution.yml $(default_args) --context .Debug --toolchain AC6
	mkdir -p bins
	mkdir -p bins/fvp
	cp build/MPS3_FVP/AC6/Debug/$(projname)/outdir/$(projname).axf bins/fvp/$(projname).axf
	mkdir -p bins/an555
	cp build/MPS3_AN555/AC6/Debug/$(projname)/outdir/$(projname).axf bins/an555/$(projname).axf
	fromelf --bin --output bins/an555/$(projname)_armclang.bin bins/an555/$(projname).axf

gcc:
	$(cbuild) $(projname).csolution.yml $(default_args) --context .Debug --toolchain GCC
	mkdir -p bins
	mkdir -p bins/fvp
	cp build/MPS3_FVP/GCC/Debug/$(projname)/outdir/$(projname).elf bins/fvp/$(projname).elf
	mkdir -p bins/an555
	cp build/MPS3_AN555/GCC/Debug/$(projname)/outdir/$(projname).elf bins/an555/$(projname).elf
	arm-none-eabi-objcopy -O binary bins/an555/$(projname).elf bins/an555/$(projname)_gcc.bin

iar:
	$(cbuild) $(projname).csolution.yml $(default_args) --context .Debug --toolchain IAR
	mkdir -p bins
	mkdir -p bins/fvp
	cp build/MPS3_FVP/IAR/Debug/$(projname)/outdir/$(projname).out bins/fvp/$(projname).out
	mkdir -p bins/an555
	cp build/MPS3_AN555/IAR/Debug/$(projname)/outdir/$(projname).out bins/an555/$(projname).out
	ielftool --bin bins/an555/Blinky.out bins/an555/Blinky_iar.bin

clang:
	$(cbuild) $(projname).csolution.yml $(default_args) --context .Debug --toolchain CLANG
	mkdir -p bins
	mkdir -p bins/fvp
	cp build/MPS3_FVP/CLANG/Debug/$(projname)/outdir/$(projname).elf bins/fvp/$(projname).clang.elf
	mkdir -p bins/an555
	cp build/MPS3_AN555/CLANG/Debug/$(projname)/outdir/$(projname).elf bins/an555/$(projname).clang.elf
	llvm-objcopy -O binary bins/an555/$(projname).clang.elf bins/an555/$(projname)_clang.bin

clean:
	rm -rf $(projname)/$(projname)*.[RD]*+*-*.*
	rm -rf *.cbuild-idx.yml
	rm -rf $(projname)/RTE

clean-build:
	rm -rf build bins

run-armclang:
	$(model) $(model_args) build/MPS3_FVP/AC6/Debug/$(projname)/outdir/$(projname).axf

run-gcc:
	$(model) $(model_args) build/MPS3_FVP/GCC/Debug/$(projname)/outdir/$(projname).elf

run-clang:
	$(model) $(model_args) build/MPS3_FVP/CLANG/Debug/$(projname)/outdir/$(projname).elf

run-iar:
	$(model) $(model_args) build/MPS3_FVP/IAR/Debug/$(projname)/outdir/$(projname).out
