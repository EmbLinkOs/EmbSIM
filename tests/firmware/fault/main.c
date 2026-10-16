/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* fault -- the fault report's test: main calls read_sensor, which calls
 * peek on an address nothing answers (0x30000000 on an STM32F103). The
 * load is a precise bus error; with BusFault not enabled it escalates to a
 * HardFault, whose handler (startup.c's Default_Handler) loops. */
#include <stdint.h>

static volatile uint32_t sink;

__attribute__((noinline)) static uint32_t peek(uintptr_t addr) { return *(volatile uint32_t *)addr + 1u; }

__attribute__((noinline)) static uint32_t read_sensor(uint32_t n)
{
    uint32_t v = peek(0x30000000u + 4u * n);
    return v * 3u;
}

int main(void)
{
    sink = read_sensor(sink);
    for (;;)
        sink++;
    return 0;
}
