/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Bare-register ROM-booted acceptance firmware, ESP32-C3 TRM v1.4 ch.5/7/32.
 * 1 kHz LEDC from 40 MHz XTAL / 156.25 / 256. A mode button cycles mixed
 * 25/50/75 percent, all off, all on, then mixed again. No optical averaging. */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define GPIO 0x60004000u
#define LEDC 0x60019000u
#define SYSTEM 0x600C0000u
#define TIMER ((40000u << 4) | 8u)

static void duty(unsigned channel, unsigned counts)
{
    REG(LEDC + 0x08u + 0x14u * channel) = counts << 4;
    REG(LEDC + 0x0Cu + 0x14u * channel) = 1u << 31 | 1u << 30 | 1u << 20 | 1u << 10;
    REG(LEDC + 0x14u * channel) = 1u << 2 | 1u << 4;
}

static void profile(unsigned mode)
{
    duty(0, mode == 1 ? 0 : mode == 2 ? 256 : 64);
    duty(1, mode == 1 ? 0 : mode == 2 ? 256 : 128);
    duty(2, mode == 1 ? 0 : mode == 2 ? 256 : 192);
}

int main(void)
{
    unsigned channel, mode = 0, was_pressed = 0;
    REG(SYSTEM + 0x10u) |= 1u << 11;
    REG(SYSTEM + 0x18u) &= ~(1u << 11);
    REG(LEDC + 0xD0u) = 3u | 1u << 31; /* XTAL clock */
    REG(LEDC + 0xA0u) = TIMER | 1u << 23 | 1u << 25;
    for (channel = 0; channel < 3; channel++) {
        unsigned pin = 4 + channel;
        REG(0x60009004u + 4u * pin) = 1u << 12 | 1u << 9;
        REG(GPIO + 0x554u + 4u * pin) = 45u + channel;
        REG(GPIO + 0x24u) = 1u << pin;
        REG(LEDC + 0x04u + 0x14u * channel) = 0;
    }
    REG(0x60009004u) = 1u << 12 | 1u << 9; /* IO0 input, external pull-up */
    profile(mode);
    REG(LEDC + 0xA0u) = TIMER;
    for (;;) {
        unsigned pressed = (REG(GPIO + 0x3Cu) & 1u) == 0;
        if (pressed && !was_pressed) {
            mode = (mode + 1u) % 3u;
            profile(mode);
        }
        was_pressed = pressed;
    }
}
