/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* efuse.c -- the ESP32-C3's eFuse values the other devices read (ESP32-C3
 * TRM v1.4, chapter 4). So far the read registers' reset values from the
 * register description; the eFuse controller comes with the boot. */
#include "mcu/esp32c3/esp32c3.h"

/* EFUSE_RD_REPEAT_DATA1_REG's WDT_DELAY_SEL: stage 0 of the RWDT is its
 * hold shifted left by this plus one */
int esk_esp32c3_efuse_wdt_delay_sel(struct esk_mach *m)
{
    u32 v = 0;
    if (esk_bus_debug_read(&m->bus, 0x60008834u, 4, &v) != 0)
        return 0;
    return (int)((v >> 16) & 3u);
}
