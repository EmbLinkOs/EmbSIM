/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* dwt.c -- the Cortex-M's data watchpoint and trace unit, at 0xE0001000:
 * only its cycle counter, CYCCNT, which counts the cost model's estimate
 * (tools/bench/cost.h) while CTRL.CYCCNTENA is set. The rest reads as
 * zero.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/dwt.c. */
#include "mcu/cortexm/cortexm.h"

struct dwt {
    struct esk_mach *sim;
    u32 ctrl, base;                 /* CYCCNT is cycles - base when on */
};

static u32 dwt_read(void *ctx, u32 off, int n)
{
    struct dwt *d = ctx;
    (void)n;
    if (off == 0)
        return d->ctrl;
    if (off == 4)
        return (d->ctrl & 1) ? (u32)d->sim->cycles - d->base : d->base;
    return 0;
}

static void dwt_write(void *ctx, u32 off, int n, u32 v)
{
    struct dwt *d = ctx;
    u32 cycles = (u32)d->sim->cycles;
    (void)n;
    if (off == 0) {
        u32 now = (d->ctrl & 1) ? cycles - d->base : d->base;
        d->ctrl = v & 1;
        /* keep CYCCNT where it was across the switch */
        d->base = (d->ctrl & 1) ? cycles - now : now;
    } else if (off == 4) {
        d->base = (d->ctrl & 1) ? cycles - v : v;
    }
}

static void dwt_reset(void *ctx)
{
    struct dwt *d = ctx;
    d->ctrl = d->base = 0;
}

const struct esk_dev_ops esk_dwt_ops = { "dwt", dwt_read, dwt_write, dwt_reset, 0, 0 };

void *esk_dwt_create(struct esk_mach *s)
{
    struct dwt *d = (struct dwt *)esk_mach_alloc(s, sizeof *d);
    if (d)
        d->sim = s;
    return d;
}
