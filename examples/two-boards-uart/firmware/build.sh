#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Build the Uno side of the two-board example with clang and lld (embsim build does the same from
# project.json).
set -eu
cd "$(dirname "$0")"
clang --target=avr -mmcu=atmega328p -g -O2 -ffreestanding -fno-builtin -nostdlib -fuse-ld=lld \
    -Wl,--gc-sections -Wl,-T,atmega328p.ld startup.S main.c -o ping.elf
