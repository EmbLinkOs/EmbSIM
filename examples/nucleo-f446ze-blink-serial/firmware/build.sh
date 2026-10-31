#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Build the NUCLEO-F446ZE example with clang and lld (embsim build does the
# same from project.json).
set -eu
cd "$(dirname "$0")"
clang --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -g -O2 \
    -ffreestanding -fno-builtin -nostdlib -fuse-ld=lld -Wl,--build-id=none -Wl,-T,stm32f446ze.ld \
    startup.c main.c -o blink-serial.elf
