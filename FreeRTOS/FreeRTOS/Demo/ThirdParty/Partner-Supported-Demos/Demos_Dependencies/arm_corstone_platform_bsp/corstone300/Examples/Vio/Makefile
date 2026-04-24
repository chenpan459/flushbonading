# Copyright (c) 2024 Arm Limited. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

projname=Vio
default_args=--packs --update-rte --jobs 8
cbuild=cbuild
model=FVP_Corstone_SSE-300_Ethos-U55
#model_args=-C mps3_board.uart0.out_file=- --simlimit 10
model_args=--simlimit 10 -C mps3_board.v_path=/path/to/vio/py/dir

all:
	$(MAKE) clean armclang
	$(MAKE) clean gcc
	$(MAKE) clean iar
	$(MAKE) clean clang

run-all:
	parallel '$(model) $(model_args)' ::: bins/*

armclang:
	$(cbuild) $(projname).csolution.yml $(default_args) --context .Debug --toolchain AC6
	mkdir -p bins
	cp build/MPS3/AC6/Debug/$(projname)/outdir/$(projname).axf bins/$(projname).axf
gcc:
	$(cbuild) $(projname).csolution.yml $(default_args) --context .Debug --toolchain GCC
	mkdir -p bins
	cp build/MPS3/GCC/Debug/$(projname)/outdir/$(projname).elf bins/$(projname).elf
iar:
	$(cbuild) $(projname).csolution.yml $(default_args) --context .Debug --toolchain IAR
	mkdir -p bins
	cp build/MPS3/IAR/Debug/$(projname)/outdir/$(projname).out bins/$(projname).out
clang:
	$(cbuild) $(projname).csolution.yml $(default_args) --context .Debug --toolchain CLANG
	mkdir -p bins
	cp build/MPS3/CLANG/Debug/$(projname)/outdir/$(projname).elf bins/$(projname).clang.elf

clean:
	rm -rf $(projname)/$(projname)*.[RD]*+*-*.*
	rm -rf *.cbuild-idx.yml
	rm -rf $(projname)/RTE

clean-build:
	rm -rf build bins

run-armclang:
	$(model) $(model_args) bins/$(projname).axf

run-gcc:
	$(model) $(model_args) build/MPS3/GCC/Debug/$(projname)/outdir/$(projname).elf

run-clang:
	$(model) $(model_args) build/MPS3/CLANG/Debug/$(projname)/outdir/$(projname).elf

run-iar:
	$(model) $(model_args) build/MPS3/IAR/Debug/$(projname)/outdir/$(projname).out
