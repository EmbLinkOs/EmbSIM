/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* dwt.c -- the Cortex-M's data watchpoint and trace unit, at 0xE0001000:
 *
 *   - CYCCNT, which counts the cost model's estimate (cost.h) while
 *     CTRL.CYCCNTENA is set (ARMv7-M);
 *   - CTRL's NUMCOMP: four comparators on the M3 and M4, two on the M0+;
 *   - each comparator's COMP, MASK and FUNCTION: FUNCTION 5, 6 or 7
 *     watches reads, writes or both of the address COMP with MASK's low
 *     bits ignored, MATCHED set on a match (cleared by FUNCTION's read)
 *     and the core halted after the instruction when it is being debugged
 *     (debug.c);
 *   - the CoreSight identification registers.
 * Omissions: the other counters (CPICNT, EXCCNT, SLEEPCNT, LSUCNT,
 * FOLDCNT), PC sampling, data value matching and linked comparators,
 * FUNCTION's trace and CYCCNT event kinds, the ITM packets it would emit.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/dwt.c (CYCCNT); the
 * comparators written for EmbSIM from ARM DDI 0403E C1.8. */
#include "mcu/cortexm/cortexm.h"

struct dwt {
    struct esk_mach *sim;
    struct esk_cm_state *core;      /* the core it watches (esk_dwt_core) */
    u32 ctrl, base;                 /* CYCCNT is cycles - base when on */
};

static int ncomp(const struct dwt *d)
{
    return d->core && d->core->model->arch == ARCH_V6M ? 2 : 4;
}

static void comps_on(struct dwt *d)
{
    int i, any = 0;
    if (!d->core)
        return;
    for (i = 0; i < ncomp(d); i++) {
        u32 f = d->core->dbg.dwt_func[i] & 0xfu;
        if (f >= 5 && f <= 7)
            any = 1;
    }
    d->core->dbg.dwt_on = any;
}

static u32 dwt_read(void *ctx, u32 off, int n)
{
    struct dwt *d = ctx;
    (void)n;
    if (off == 0)
        return d->ctrl | (u32)ncomp(d) << 28;
    if (off == 4)
        return (d->ctrl & 1) ? (u32)d->sim->cycles - d->base : d->base;
    if (d->core && off >= 0x20 && off < 0x20u + 16u * (u32)ncomp(d)) {
        int i = (int)((off - 0x20) / 16);
        struct esk_cm_debug *g = &d->core->dbg;
        switch (off & 15u) {
        case 0: return g->dwt_comp[i];
        case 4: return g->dwt_mask[i];
        case 8: {
            u32 v = g->dwt_func[i];
            if (!d->sim->bus.debug)
                g->dwt_func[i] &= ~(1u << 24); /* MATCHED: cleared by the read */
            return v;
        }
        }
        return 0;
    }
    switch (off) { /* a DWT (ARM DDI 0403E C1.8.12) */
    case 0xfd0: return 0x04;
    case 0xfe0: return d->core && d->core->model->arch == ARCH_V6M ? 0x0a : 0x02;
    case 0xfe4: return 0xb0;
    case 0xfe8: return 0x0b;
    case 0xff0: return 0x0d;
    case 0xff4: return 0xe0;
    case 0xff8: return 0x05;
    case 0xffc: return 0xb1;
    }
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
    } else if (d->core && off >= 0x20 && off < 0x20u + 16u * (u32)ncomp(d)) {
        int i = (int)((off - 0x20) / 16);
        struct esk_cm_debug *g = &d->core->dbg;
        switch (off & 15u) {
        case 0: g->dwt_comp[i] = v; break;
        case 4: g->dwt_mask[i] = v & 0x1fu; break;
        case 8: g->dwt_func[i] = (g->dwt_func[i] & (1u << 24)) | (v & 0xfu); break;
        }
        comps_on(d);
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

void esk_dwt_core(void *dwt, struct esk_cm_state *cs)
{
    if (dwt)
        ((struct dwt *)dwt)->core = cs;
}
