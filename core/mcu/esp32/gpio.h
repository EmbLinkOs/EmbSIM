/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_GPIO_H
#define ESK_ESP32_GPIO_H
#include "embsim/types.h"
/* Opt-in original-ESP32 software-GPIO route fixture, not full GPIO/IO_MUX.
 * Config: route[2], input-enable[2], pull-up[2], pull-down[2], PAD_DRIVER[2], OUT[2], ENABLE[2] (14 words).
 * Routes are explicit normal/noninverted software GPIO selection, not guessed
 * chip reset/pinmux or package/board exposure. Initial latches/directions are explicit;
 * unsupported PIN fields are disabled fixture choices, not chip reset facts.
 * OUT_SEL reads 256; only this unchanged fixed selection can be written. GPIO_PIN fields other than
 * PAD_DRIVER are fixed disabled in this profile; enabling them fails.
 * OUT/ENABLE are shared hardware latches; input-only 34..39 never drive.
 * Inputs are supplied resolved levels, not fabricated floating levels.
 * Output = drive0/drive1/HiZ plus pull controls, not voltage/current/IV timing.
 * DIG_ISO: global pad hold/unhold, auto-hold enable/latch/clear only. Isolation,
 * sleep/power/Wi-Fi/memory changes fail; fixed fields retain published defaults.
 * Auto-hold event is an external latch input, not SoC sleep execution.
 * DIG_PAD_HOLD implements the SDK-mapped digital pins; RTC pad-specific hold
 * and the PICO-only GPIO20 hold bit remain unsupported. Local/global hold use
 * one effective latch per routed pad; FORCE_UNHOLD overrides local/auto hold.
 * Simultaneous FORCE_HOLD/FORCE_UNHOLD remains unsupported.
 * No IO_MUX/peripheral matrix, RTC per-pad hold, IRQ/wakeup/filter/straps, sleep
 * configuration, package mapping, loading/glitches/strength/timing qualification.
 * All calls serialized; failed operations preserve state/output. */
typedef struct esk_esp32_gpio {
    uint32_t enabled,route[2],ie[2],pu[2],pd[2],out[2],oe[2],od[2],known[2],level[2];
    uint32_t hold_out[2],hold_oe[2],hold_od[2],hold_ie[2],hold_pu[2],hold_pd[2];
    uint32_t iso,auto_hold,dig_hold,held[2];
} esk_esp32_gpio;
/* drive -1=HiZ, 0=low, 1=high. Pulls describe controls to an external solver. */
typedef struct esk_esp32_gpio_pad { int drive;unsigned input_enabled,pull_up,pull_down; } esk_esp32_gpio_pad;
void esk_esp32_gpio_init(esk_esp32_gpio *g);
es_status esk_esp32_gpio_configure(esk_esp32_gpio *g,const uint32_t *config,size_t count);
es_status esk_esp32_gpio_read(const esk_esp32_gpio *g,uint32_t address,unsigned bytes,uint32_t *value);
es_status esk_esp32_gpio_write(esk_esp32_gpio *g,uint32_t address,unsigned bytes,uint32_t value);
es_status esk_esp32_gpio_input(esk_esp32_gpio *g,unsigned pin,int known,int high);
es_status esk_esp32_gpio_output(const esk_esp32_gpio *g,unsigned pin,esk_esp32_gpio_pad *pad);
es_status esk_esp32_gpio_auto_hold_event(esk_esp32_gpio *g);
#endif
