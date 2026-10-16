#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Build the example firmware with Clang and LLD, or with the GNU Arm
# toolchain: sh build.sh [clang|gcc]. `embsim build` does the same from the
# project's toolchain profile.
set -eu
cd "$(dirname "$0")"
case "${1:-clang}" in
clang)
    clang --target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -O2 -g -ffreestanding -fno-builtin \
        -nostdlib -fuse-ld=lld -Wl,--build-id=none -ffile-prefix-map="$(pwd)"=. \
        -T stm32f103c8.ld startup.c main.c -o button-led.elf ;;
gcc)
    arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -O2 -g -ffreestanding -nostdlib \
        -T stm32f103c8.ld startup.c main.c -o button-led.elf ;;
*)
    echo "usage: build.sh [clang|gcc]" >&2; exit 2 ;;
esac
echo "built button-led.elf"
