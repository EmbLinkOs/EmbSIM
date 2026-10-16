#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Build the HiFive1 example with clang and lld (embsim build does the same
# from project.json).
set -eu
cd "$(dirname "$0")"
clang --target=riscv32-unknown-elf -march=rv32imac_zicsr -mabi=ilp32 -g -O2 -ffreestanding -fno-builtin \
    -nostdlib -nostartfiles -fuse-ld=lld -Wl,--gc-sections -Wl,-T,fe310.ld start.S main.c -o blink.elf
