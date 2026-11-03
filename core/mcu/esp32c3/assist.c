/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* assist.c -- the ESP32-C3's Debug Assistant (ASSIST_DEBUG, TRM v1.4
 * chapter 17), so far its PC logging (17.4.2): while CORE_0_RCD_EN has
 * RCD_PDEBUGEN (the CPU sends its PC) and RCD_RECORDEN (the register
 * samples it), CORE_0_RCD_PDEBUGPC reads the CPU's PC and
 * CORE_0_RCD_PDEBUGSP its SP; otherwise they keep what they last sampled.
 * A CPU reset clears RCD_EN but not them, so after one they hold the PC and
 * SP at the reset, which the boot ROM prints as "Saved PC". A chip reset
 * (power-on, the CHIP_EN pin) clears them.
 * Not yet: the region and stack monitors, the bus access logging and their
 * interrupts; their registers only keep what is written. */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

struct c3_assist {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *rcd_en, *pc, *sp;
    u32 saved_pc, saved_sp;
};

static struct c3_assist *assist_of(struct esk_mach *m)
{
    return (struct c3_assist *)esk_rf_ctx_of(m->rf, &esk_esp32c3_assist_ops);
}

static int recording(const struct c3_assist *k)
{
    return (k->rcd_en->value & 3u) == 3u;
}

static void sample(struct c3_assist *k)
{
    u8 b[4] = { 0, 0, 0, 0 };
    k->saved_pc = k->s->cpu->ops->pc(k->s->cpu);
    if (k->s->cpu->ops->reg_read && k->s->cpu->ops->reg_read(k->s->cpu, 2, b) >= 4)
        k->saved_sp = (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24;
}

void esk_esp32c3_assist_cpu_reset(struct esk_mach *m)
{
    struct c3_assist *k = assist_of(m);
    if (!k)
        return;
    if (recording(k))
        sample(k);
    esk_rf_hw(k->rcd_en, 0);
}

static u32 as_read(void *ctx, u32 off, int n)
{
    struct c3_assist *k = (struct c3_assist *)ctx;
    if (recording(k))
        sample(k);
    esk_rf_hw(k->pc, k->saved_pc);
    esk_rf_hw(k->sp, k->saved_sp);
    return esk_rf_read(k->p, off, n);
}

static void as_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_assist *k = (struct c3_assist *)ctx;
    if (recording(k))
        sample(k); /* what it held when the recording stops */
    esk_rf_write(k->p, off, n, v);
}

static void as_reset(void *ctx)
{
    struct c3_assist *k = (struct c3_assist *)ctx;
    if (k->s->reset_cause == ESK_RESET_POWER || k->s->reset_cause == ESK_RESET_PIN)
        k->saved_pc = k->saved_sp = 0;
}

const struct esk_dev_ops esk_esp32c3_assist_ops = {
    "esp32c3-assist", as_read, as_write, as_reset, 0, 0,
};

void *esk_esp32c3_assist_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_assist *k = (struct c3_assist *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    k->rcd_en = esk_rf_reg(p, "CORE_0_RCD_EN");
    k->pc = esk_rf_reg(p, "CORE_0_RCD_PDEBUGPC");
    k->sp = esk_rf_reg(p, "CORE_0_RCD_PDEBUGSP");
    return k->rcd_en && k->pc && k->sp ? k : 0;
}
