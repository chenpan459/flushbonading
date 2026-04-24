# Copyright (c) 2024 Arm Limited. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

projname=Vio
default_args=--packs --update-rte --jobs 8 --rebuild --context .Debug
cbuild=cbuild
model=FVP_Corstone_SSE-320
model_args=-C mps4_board.uart0.out_file=- -C mps4_board.v_path=.
#model_args=-C mps4_board.v_path=. --simlimit 10

all:
	$(MAKE) clean armclang
	$(MAKE) clean gcc
	$(MAKE) clean iar
	$(MAKE) clean clang

run-all:
	parallel $(model) $(model_args) ::: bins/*

armclang:
	$(cbuild) $(projname).csolution.yml $(default_args) --toolchain AC6
	mkdir -p bins
	mkdir -p bins/fvp
	cp build/SSE-320-FVP/AC6/Debug/$(projname)/outdir/$(projname).axf bins/$(projname).axf

gcc:
	$(cbuild) $(projname).csolution.yml $(default_args) --toolchain GCC
	mkdir -p bins
	mkdir -p bins/fvp
	cp build/SSE-320-FVP/GCC/Debug/$(projname)/outdir/$(projname).elf bins/$(projname).elf

iar:
	$(cbuild) $(projname).csolution.yml $(default_args) --toolchain IAR
	mkdir -p bins
	mkdir -p bins/fvp
	cp build/SSE-320-FVP/IAR/Debug/$(projname)/outdir/$(projname).out bins/$(projname).out

clang:
	$(cbuild) $(projname).csolution.yml $(default_args) --toolchain CLANG
	mkdir -p bins
	mkdir -p bins/fvp
	cp build/SSE-320-FVP/CLANG/Debug/$(projname)/outdir/$(projname).elf bins/$(projname).clang.elf

clean:
	rm -rf $(projname)/$(projname)*.[RD]*+*-*.*
	rm -rf *.cbuild-idx.yml

clean-build:
	rm -rf build bins

run-armclang:
	$(model) $(model_args) build/SSE-320-FVP/AC6/Debug/$(projname)/outdir/$(projname).axf

run-gcc:
	$(model) $(model_args) build/SSE-320-FVP/GCC/Debug/$(projname)/outdir/$(projname).elf

run-clang:
	$(model) $(model_args) build/SSE-320-FVP/CLANG/Debug/$(projname)/outdir/$(projname).elf

run-iar:
	$(model) $(model_args) build/SSE-320-FVP/IAR/Debug/$(projname)/outdir/$(projname).out
