#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Build the Explorer 16/32 PIC32MX example with clang and lld (embsim build
# does the same from project.json).
set -eu
cd "$(dirname "$0")"
clang --target=mipsel-linux-gnu -march=mips32r2 -msoft-float -mno-abicalls -fno-pic -G0 -g -O2 \
    -ffreestanding -fno-builtin -nostdlib -nostartfiles -static -fuse-ld=lld -Wl,--gc-sections -Wl,-T,pic32mx795f512l.ld \
    crt0.S main.c -o blink-serial.elf
