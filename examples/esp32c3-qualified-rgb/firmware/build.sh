#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
set -eu
cd "$(dirname "$0")"
if [ -n "${EMBSIM_ZIG:-}" ]; then
  set -- "$EMBSIM_ZIG" cc -target riscv32-freestanding -mcpu=generic_rv32+m+c
else
  set -- clang --target=riscv32-unknown-elf -march=rv32imc -fuse-ld=lld
fi
"$@" -mabi=ilp32 -O2 -g -ffreestanding -fno-builtin \
  -nostdlib -Wl,--build-id=none -Wl,-T,esp32c3.ld startup.S main.c -o pwm.elf
"${EMBSIM_PYTHON:-python3}" -m esptool --chip esp32c3 elf2image \
  --flash_mode dio --flash_freq 40m --flash_size 4MB -o pwm.bin pwm.elf
