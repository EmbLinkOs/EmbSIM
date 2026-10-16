/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* clint.c -- the FE310's CLINT (FE310-G002 manual, chapter 9), over the
 * register file: msip, mtimecmp and mtime of the one hart.
 *
 *   - mtime counts the real-time clock, 32.768 kHz (the HiFive1's crystal
 *     on the always-on domain), exactly: each core cycle adds 32768 to an
 *     accumulator that carries the remainder, so the count is the same at
 *     any core clock and over any run length;
 *   - MTIP is raised while mtime >= mtimecmp and MSIP while msip's bit 0
 *     is set; the time to mtimecmp is a device event, so a WFI sleeps
 *     until it;
 *   - the core's `time` CSR reads mtime.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/clint.c, whose mtime
 * counted the core's cycles; the RTC timebase is EmbSIM's. */
#include <string.h>

#include "mcu/riscv/riscv.h"

#define RTC_HZ 32768u

struct fe_clint {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *msip, *cmp_lo, *cmp_hi, *time_lo, *time_hi;
    u64 mtime;
    u64 acc; /* RTC_HZ per core cycle, a tick each core_hz */
    int running;
};

static u64 cmp(const struct fe_clint *k)
{
    return (u64)(u32)k->cmp_hi->value << 32 | (u32)k->cmp_lo->value;
}

static void mirror(struct fe_clint *k)
{
    esk_rf_hw(k->time_lo, (u32)k->mtime);
    esk_rf_hw(k->time_hi, (u32)(k->mtime >> 32));
}

static void lines(struct fe_clint *k)
{
    esk_riscv_line(k->s, MIP_MTIP, k->mtime >= cmp(k));
    esk_riscv_line(k->s, MIP_MSIP, (k->msip->value & 1) != 0);
}

static u64 cl_mtime(void *ctx) { return ((struct fe_clint *)ctx)->mtime; }

static void cl_tick(void *ctx, u32 cycles)
{
    struct fe_clint *k = (struct fe_clint *)ctx;
    u32 hz = esk_mach_core_hz(k->s);
    u64 before = k->mtime;
    if (!hz)
        return;
    k->acc += (u64)cycles * RTC_HZ;
    if (k->acc >= hz) {
        k->mtime += k->acc / hz;
        k->acc %= hz;
    }
    if (k->mtime != before) {
        mirror(k);
        if (before < cmp(k) && k->mtime >= cmp(k))
            lines(k);
    }
}

static int cl_next(void *ctx, u32 *cycles)
{
    struct fe_clint *k = (struct fe_clint *)ctx;
    u32 hz = esk_mach_core_hz(k->s);
    u64 c = cmp(k), need, left;
    if (k->mtime >= c || !hz)
        return 0;
    need = c - k->mtime;
    if (need > ~(u64)0 / hz) {
        *cycles = 0xffffffffu; /* mtimecmp is out of reach: as far as a wait goes */
        return 1;
    }
    /* the cycles until acc reaches need * hz */
    left = (need * hz - k->acc + RTC_HZ - 1) / RTC_HZ;
    *cycles = left > 0xffffffffu ? 0xffffffffu : (u32)left;
    return 1;
}

static u32 cl_read(void *ctx, u32 off, int n)
{
    return esk_rf_read(((struct fe_clint *)ctx)->p, off, n);
}

static void cl_write(void *ctx, u32 off, int n, u32 v)
{
    struct fe_clint *k = (struct fe_clint *)ctx;
    esk_rf_write(k->p, off, n, v);
    if (off == esk_rf_off(k->time_lo) || off == esk_rf_off(k->time_hi)) {
        k->mtime = (u64)(u32)k->time_hi->value << 32 | (u32)k->time_lo->value;
        k->acc = 0;
    }
    lines(k);
}

static void cl_reset(void *ctx)
{
    struct fe_clint *k = (struct fe_clint *)ctx;
    k->mtime = 0;
    k->acc = 0;
    k->running = 0;
    esk_rf_clock(k->p, &k->running, 1);
    esk_riscv_mtime_source(k->s, cl_mtime, k);
    lines(k);
}

const struct esk_dev_ops esk_fe310_clint_ops = {
    "fe310-clint", cl_read, cl_write, cl_reset, cl_tick, cl_next,
};

void *esk_fe310_clint_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct fe_clint k, *c;
    memset(&k, 0, sizeof k);
    k.s = s;
    k.p = p;
    k.msip = esk_rf_reg(p, "MSIP");
    k.cmp_lo = esk_rf_reg(p, "MTIMECMP_LO");
    k.cmp_hi = esk_rf_reg(p, "MTIMECMP_HI");
    k.time_lo = esk_rf_reg(p, "MTIME_LO");
    k.time_hi = esk_rf_reg(p, "MTIME_HI");
    if (!k.msip || !k.cmp_lo || !k.cmp_hi || !k.time_lo || !k.time_hi)
        return 0;
    c = (struct fe_clint *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = k;
    esk_riscv_mtime_source(s, cl_mtime, c);
    return c;
}
