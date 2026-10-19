/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* gpio.c -- the ESP32-C3's GPIO block (ESP32-C3 TRM v1.4, chapters 5 and
 * 7), so far its strapping latch: at a chip reset GPIO_STRAP's STRAPPING
 * takes the levels of GPIO9 (bit 3), GPIO8 (bit 2), GPIO3 (bit 1) and GPIO2
 * (bit 0), and keeps them through every other reset. Bits 3 and 2 are the
 * TRM's (section 7.2: forcing download overwrites STRAPPING[3:2]); bits 1
 * and 0 follow table 7.2-1's column order. A pin nothing drives reads its
 * reset pull: GPIO9's weak pull-up gives 1, GPIO8, GPIO3 and GPIO2 have
 * none and read 0.
 * Not yet: the GPIO matrix, the pins' outputs, inputs and interrupts (the
 * RTOS boards plan's step 4); their registers only keep what is written. */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

struct c3_gpio {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *strap;
    u32 latched;
    int have;
};

static int pin_level(struct c3_gpio *k, int gpio, int dflt)
{
    int level = dflt;
    if (k->s->host.pin_in && k->s->host.pin_in(k->s->host.ctx, gpio / 16, gpio % 16, &level))
        return level != 0;
    return dflt;
}

static u32 gp_read(void *ctx, u32 off, int n)
{
    struct c3_gpio *k = (struct c3_gpio *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void gp_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_gpio *k = (struct c3_gpio *)ctx;
    if (off == esk_rf_off(k->strap))
        return; /* read-only */
    esk_rf_write(k->p, off, n, v);
}

static void gp_reset(void *ctx)
{
    struct c3_gpio *k = (struct c3_gpio *)ctx;
    if (!k->have || k->s->reset_cause == ESK_RESET_POWER || k->s->reset_cause == ESK_RESET_PIN) {
        k->latched = (u32)pin_level(k, 9, 1) << 3 | (u32)pin_level(k, 8, 0) << 2 | (u32)pin_level(k, 3, 0) << 1 |
                     (u32)pin_level(k, 2, 0);
        k->have = 1;
    }
    esk_rf_hw(k->strap, k->latched);
}

const struct esk_dev_ops esk_esp32c3_gpio_ops = {
    "esp32c3-gpio", gp_read, gp_write, gp_reset, 0, 0,
};

void *esk_esp32c3_gpio_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_gpio *k = (struct c3_gpio *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->strap = esk_rf_reg(p, "STRAP");
    return k->strap ? k : 0;
}
