#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Build the Explorer 16/32 PIC24 example with XC16 (embsim build does the
# same from project.json and the XC16 profile).
set -eu
cd "$(dirname "$0")"
xc16-gcc -mcpu=24FJ1024GB610 -g -O1 -ffunction-sections -fdata-sections -Wall -c main.c -o main.o
xc16-gcc -mcpu=24FJ1024GB610 -Wl,--script=p24FJ1024GB610.gld -Wl,--gc-sections -Wl,--local-stack \
    main.o -o blink-serial.elf
rm -f main.o
