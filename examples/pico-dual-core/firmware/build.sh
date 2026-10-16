#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Build the Pico dual-core example with clang and lld (embsim build does the
# same from project.json).
set -eu
cd "$(dirname "$0")"
clang --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -mthumb -g -O2 -ffreestanding -fno-builtin \
    -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,-T,rp2040.ld boot2.c startup.c main.c -o dual-core.elf
