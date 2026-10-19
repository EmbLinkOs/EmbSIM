/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* clk.c -- the ESP32-C3's clocks as its devices read them (ESP32-C3 TRM
 * v1.4, chapter 6): APB_CLK is 80 MHz while CPU_CLK comes from the PLL,
 * and CPU_CLK itself while it comes from XTAL_CLK or RC_FAST_CLK. */
#include "mcu/esp32c3/esp32c3.h"

u32 esk_esp32c3_apb_hz(struct esk_mach *m)
{
    u32 cpu = esk_mach_core_hz(m);
    return cpu > 80000000u ? 80000000u : cpu;
}
