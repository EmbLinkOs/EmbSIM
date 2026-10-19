/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* intmtx.c -- the ESP32-C3's interrupt matrix and the ESP-RV core's
 * interrupt controller (ESP32-C3 TRM v1.4, sections 1.5 and 8), one
 * device at INTERRUPT_CORE0:
 *
 *   - each of the 62 peripheral sources is a level from its device;
 *     INTR_STATUS_REG_0/1 read them; its *_MAP register routes it to CPU
 *     interrupt 1-31 (0 routes it nowhere: id 0 is the exceptions');
 *     several sources on one interrupt are ORed;
 *   - a CPU interrupt of level type (CPU_INT_TYPE bit 0) is pending while
 *     its line is high; one of edge type latches a rising edge of its line
 *     while it is enabled, and keeps it until cleared;
 *   - CPU_INT_EIP_STATUS shows the pending interrupts that are enabled and
 *     of a nonzero priority at least CPU_INT_THRESH;
 *   - the core claims the highest priority among them (the lowest id
 *     among equals) when it takes it; toggling CPU_INT_CLEAR's bit (set,
 *     then cleared) clears a claimed edge interrupt's latch, or an
 *     unclaimed one's when it is disabled; a level interrupt is cleared at
 *     its source;
 *   - in steady state an interrupt is taken 4 cycles after it shows in
 *     EIP_STATUS (TRM 1.5.3.1), so up to five instructions may run first.
 * Not modelled: the transient ordering for 4 cycles after a register
 * write (software follows the TRM's MIE-off, write, FENCE sequence);
 * CLOCK_GATE (the registers always answer). */
#include <stdio.h>
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"
#include "mcu/riscv/riscv.h"

#define LATENCY 4 /* cycles from EIP_STATUS to the trap, in steady state */

struct intmtx {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    struct esk_rf_reg *map[ESP32C3_NSOURCES], *status[2];
    struct esk_rf_reg *enable, *type, *clear, *eip, *prio[32], *thresh;
    u8 level[ESP32C3_NSOURCES];
    u32 lines;   /* the CPU interrupts' lines: their sources ORed */
    u32 latched; /* edge interrupts' captured rising edges */
    u32 claimed; /* taken by the core and not yet cleared */
    u32 shown;   /* EIP_STATUS as last computed */
    u64 since[32]; /* the cycle each interrupt showed in EIP_STATUS */
};

static struct intmtx *intmtx_of(struct esk_mach *m)
{
    int i;
    for (i = 0; m->rf; i++) {
        struct esk_rf_periph *p = esk_rf_periph_at(m->rf, i);
        if (!p)
            break;
        if (p->ops == &esk_esp32c3_intmtx_ops)
            return (struct intmtx *)p->ctx;
    }
    return NULL;
}

static u32 reg(const struct esk_rf_reg *r) { return (u32)r->value; }

/* the interrupts' lines from the sources and the map */
static u32 lines_of(const struct intmtx *k)
{
    u32 l = 0;
    int s;
    for (s = 0; s < ESP32C3_NSOURCES; s++) {
        u32 id = reg(k->map[s]) & 31u;
        if (k->level[s] && id)
            l |= 1u << id;
    }
    return l;
}

/* the pending state, EIP_STATUS and the core's news */
static void update(struct intmtx *k)
{
    u32 en = reg(k->enable), edge = reg(k->type), th = reg(k->thresh) & 15u, nl, pend, eip = 0;
    int i;
    nl = lines_of(k);
    k->latched |= nl & ~k->lines & edge & en; /* rising edges, while enabled */
    k->lines = nl;
    pend = (nl & ~edge) | (k->latched & edge);
    for (i = 1; i < 32; i++) {
        u32 pr = reg(k->prio[i]) & 15u;
        if ((pend & en) >> i & 1u && pr && pr >= th)
            eip |= 1u << i;
    }
    for (i = 1; i < 32; i++)
        if ((eip & ~k->shown) >> i & 1u)
            k->since[i] = k->s->cycles;
    k->shown = eip;
    esk_rf_hw(k->eip, eip);
    esk_rf_hw(k->status[0], 0);
    esk_rf_hw(k->status[1], 0);
    for (i = 0; i < ESP32C3_NSOURCES; i++)
        if (k->level[i])
            esk_rf_hw(k->status[i / 32], k->status[i / 32]->value | (u64)1 << (i % 32));
    esk_riscv_ext_changed(k->s);
}

/* the core's question: the interrupt to take now (take), or whether one
 * is pending at all */
static int claim(void *ctx, int take)
{
    struct intmtx *k = (struct intmtx *)ctx;
    u32 best = 0, bp = 0;
    int i;
    if (!k->shown)
        return 0;
    if (!take)
        return 1;
    for (i = 1; i < 32; i++) {
        u32 pr = reg(k->prio[i]) & 15u;
        if (!(k->shown >> i & 1u) || k->s->cycles < k->since[i] + LATENCY)
            continue;
        if (pr > bp) {
            best = (u32)i;
            bp = pr;
        }
    }
    if (best)
        k->claimed |= 1u << best;
    return (int)best;
}

void esk_esp32c3_source(struct esk_mach *m, int src, int level)
{
    struct intmtx *k = intmtx_of(m);
    if (!k || src < 0 || src >= ESP32C3_NSOURCES || k->level[src] == (level != 0))
        return;
    k->level[src] = (u8)(level != 0);
    update(k);
}

static u32 im_read(void *ctx, u32 off, int n)
{
    struct intmtx *k = (struct intmtx *)ctx;
    return esk_rf_read(k->p, off, n);
}

static void im_write(void *ctx, u32 off, int n, u32 v)
{
    struct intmtx *k = (struct intmtx *)ctx;
    if (off == esk_rf_off(k->clear)) {
        /* a bit set and then cleared: the claimed (or disabled) edge's
         * latch clears */
        u32 was = reg(k->clear), fell = was & ~v;
        u32 ok = fell & (k->claimed | ~reg(k->enable));
        k->latched &= ~ok;
        k->claimed &= ~fell;
    }
    esk_rf_write(k->p, off, n, v);
    update(k);
}

static void im_reset(void *ctx)
{
    struct intmtx *k = (struct intmtx *)ctx;
    k->lines = k->latched = k->claimed = k->shown = 0;
    update(k);
}

const struct esk_dev_ops esk_esp32c3_intmtx_ops = {
    "esp32c3-intmtx", im_read, im_write, im_reset, 0, 0,
};

void *esk_esp32c3_intmtx_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct intmtx *c, k;
    char name[48];
    int i, n = 0;
    memset(&k, 0, sizeof k);
    k.s = s;
    k.p = p;
    /* the map registers, one per source in order, are the first 62 */
    for (i = 0; i < p->nreg && n < ESP32C3_NSOURCES; i++)
        if (p->reg[i]->addr - p->base == 4u * (u32)n)
            k.map[n++] = p->reg[i];
    k.status[0] = esk_rf_reg(p, "INTR_STATUS_REG_0");
    k.status[1] = esk_rf_reg(p, "INTR_STATUS_REG_1");
    k.enable = esk_rf_reg(p, "CPU_INT_ENABLE");
    k.type = esk_rf_reg(p, "CPU_INT_TYPE");
    k.clear = esk_rf_reg(p, "CPU_INT_CLEAR");
    k.eip = esk_rf_reg(p, "CPU_INT_EIP_STATUS");
    k.thresh = esk_rf_reg(p, "CPU_INT_THRESH");
    for (i = 0; i < 32; i++) {
        (void)snprintf(name, sizeof name, "CPU_INT_PRI_%d", i);
        k.prio[i] = esk_rf_reg(p, name);
        if (!k.prio[i])
            return 0;
    }
    if (n != ESP32C3_NSOURCES || !k.status[0] || !k.status[1] || !k.enable || !k.type || !k.clear ||
        !k.eip || !k.thresh)
        return 0;
    c = (struct intmtx *)esk_mach_alloc(s, sizeof *c);
    if (!c)
        return 0;
    *c = k;
    esk_riscv_ext_source(s, claim, c);
    return c;
}
