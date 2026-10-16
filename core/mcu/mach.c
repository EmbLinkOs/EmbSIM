/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* mach.c -- the machine's housekeeping (SPEC-009 §2): its allocations, the
 * devices that keep time, the end of a run, and the host hooks.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/run.c (sim_end, sim_out,
 * sim_advance, sim_clock, sim_next_event, sim_event_coming, sim_rx_port,
 * sim_reset). The run loop itself is the MCU runtime's (mcu.c). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "mcu/mach.h"

es_status esk_mach_init(struct esk_mach *m, esk_alloc *a)
{
    memset(m, 0, sizeof *m);
    m->a = a;
    m->state = ESK_MACH_RUN;
    m->semihosting = 1;
    return ES_OK;
}

void esk_mach_free(struct esk_mach *m)
{
    size_t i;
    for (i = m->nallocs; i-- > 0;)
        esk_free(m->a, m->allocs[i].p, m->allocs[i].n);
    if (m->allocs)
        esk_free(m->a, m->allocs, m->cap_allocs * sizeof *m->allocs);
    m->allocs = NULL;
    m->nallocs = m->cap_allocs = 0;
}

void *esk_mach_alloc(struct esk_mach *m, size_t n)
{
    void *p;
    if (m->nallocs == m->cap_allocs) {
        size_t cap = m->cap_allocs ? m->cap_allocs * 2 : 64;
        esk_mach_alloc_rec *nr = (esk_mach_alloc_rec *)esk_realloc(
            m->a, m->allocs, m->cap_allocs * sizeof *m->allocs, cap * sizeof *m->allocs);
        if (!nr) {
            m->oom = 1;
            return NULL;
        }
        m->allocs = nr;
        m->cap_allocs = cap;
    }
    p = esk_calloc(m->a, 1, n ? n : 1);
    if (!p) {
        m->oom = 1;
        return NULL;
    }
    m->allocs[m->nallocs].p = p;
    m->allocs[m->nallocs].n = n ? n : 1;
    m->nallocs++;
    return p;
}

char *esk_mach_strdup(struct esk_mach *m, const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)esk_mach_alloc(m, n);
    if (p)
        memcpy(p, s, n);
    return p;
}

es_status esk_mach_add_dev(struct esk_mach *m, u32 base, u32 size, const struct esk_dev_ops *ops,
                           void *ctx)
{
    es_status st = esk_bus_add_device(&m->bus, base, size, ops, ctx);
    if (st != ES_OK)
        return st;
    if (ops->tick || ops->next_event) {
        if (m->ntick == ESK_MACH_TICKERS)
            return ES_ELIMIT;
        m->tick[m->ntick++] = &m->bus.dev[m->bus.ndev - 1];
        if (ops->tick) {
            m->tick_fn[m->ntick_fn] = ops->tick;
            m->tick_ctx[m->ntick_fn++] = ctx;
        }
    }
    return ES_OK;
}

void esk_mach_end(struct esk_mach *m, int state, const char *fmt, ...)
{
    va_list ap;
    m->state = state;
    m->yield = 1;
    va_start(ap, fmt);
    vsnprintf(m->end_why, sizeof m->end_why, fmt, ap);
    va_end(ap);
}

void esk_mach_out(struct esk_mach *m, int stream, int c)
{
    if (m->host.out)
        m->host.out(m->host.ctx, stream, c);
}

int esk_mach_readc(struct esk_mach *m)
{
    return m->host.readc ? m->host.readc(m->host.ctx) : -1;
}

void esk_mach_advance(struct esk_mach *m, u32 cycles)
{
    int i;
    for (i = 0; i < m->ntick_fn; i++)
        m->tick_fn[i](m->tick_ctx[i], cycles);
}

void esk_mach_clock(struct esk_mach *m, int *running, int on)
{
    on = on != 0;
    if (*running != on) {
        m->counting += on ? 1 : -1;
        m->bus.touched = 1;
    }
    *running = on;
}

int esk_mach_next_event(struct esk_mach *m, u32 *cycles)
{
    int any = 0, i;
    for (i = 0; i < m->ntick; i++) {
        u32 c = 0;
        struct esk_device *d = m->tick[i];
        if (d->ops->next_event && d->ops->next_event(d->ctx, &c) && (!any || c < *cycles)) {
            *cycles = c;
            any = 1;
        }
    }
    return any;
}

int esk_mach_event_coming(struct esk_mach *m)
{
    if (!m->ev_known || m->bus.touched || (m->ev_any && m->cycles >= m->ev_at)) {
        u32 left = 0;
        m->ev_any = esk_mach_next_event(m, &left);
        m->ev_at = m->cycles + (m->ev_any ? left : 0);
        m->ev_known = 1;
        m->bus.touched = 0;
    }
    return m->ev_any;
}

void esk_mach_rx_port(struct esk_mach *m, const char *unit, int (*room)(void *ctx),
                      void (*put)(void *ctx, int c), void *ctx)
{
    if (m->nrx == ESK_MACH_RX_PORTS)
        return;
    m->rx[m->nrx].room = room;
    m->rx[m->nrx].put = put;
    m->rx[m->nrx].ctx = ctx;
    m->rx[m->nrx].unit = unit;
    m->nrx++;
}

void esk_mach_reset(struct esk_mach *m)
{
    int i;
    for (i = 0; i < m->bus.ndev; i++)
        if (m->bus.dev[i].ops->reset)
            m->bus.dev[i].ops->reset(m->bus.dev[i].ctx);
    m->insns = m->cycles = 0;
    m->counting = 0;
    m->ev_known = 0;
    m->state = ESK_MACH_RUN;
    m->exit_status = 0;
    m->end_why[0] = 0;
    m->bus.watch_hit = 0;
    m->asleep = 0;
    m->reset_req = 0;
    m->yield = 0;
    if (m->cpu)
        m->cpu->ops->reset(m->cpu);
}

u32 esk_mach_core_hz(struct esk_mach *m)
{
    u32 hz = m->core_hz ? m->core_hz(m) : 0;
    return hz ? hz : (m->reset_hz ? m->reset_hz : 8000000u);
}

void esk_mach_idle(struct esk_mach *m, u64 cycles)
{
    while (cycles) {
        u32 c = cycles > 0x40000000u ? 0x40000000u : (u32)cycles;
        m->cycles += c;
        if (m->counting)
            esk_mach_advance(m, c);
        cycles -= c;
    }
}

static int at_breakpoint(const struct esk_mach *m, u32 pc)
{
    int i;
    for (i = 0; i < m->nbp; i++)
        if (m->bp[i] == pc)
            return 1;
    return 0;
}

u64 esk_mach_run(struct esk_mach *m, u64 budget, u64 max_insns)
{
    u64 start = m->cycles, end = start + budget, insns0 = m->insns;
    m->yield = 0;
    m->stop_bp = m->stop_watch = 0;
    while (m->state == ESK_MACH_RUN && m->cycles < end && !m->yield) {
        if (m->asleep) {
            u32 left = 0;
            u64 room = end - m->cycles;
            if (m->cpu->ops->wake_pending && m->cpu->ops->wake_pending(m->cpu)) {
                m->asleep = 0;
                continue;
            }
            if (!esk_mach_next_event(m, &left))
                break; /* until something outside wakes it */
            if (left == 0)
                left = 1;
            esk_mach_idle(m, left < room ? left : room);
            continue;
        }
        if (max_insns && m->insns - insns0 >= max_insns)
            break;
        if (m->nbp && !m->bp_skip && at_breakpoint(m, m->cpu->ops->pc(m->cpu))) {
            m->stop_bp = 1;
            break;
        }
        m->bp_skip = 0;
        m->cpu->ops->step(m->cpu);
        if (m->bus.watch_hit) {
            m->stop_watch = 1;
            break;
        }
    }
    return m->cycles - start;
}

int esk_mach_bp_add(struct esk_mach *m, u32 pc)
{
    if (at_breakpoint(m, pc))
        return 0;
    if (m->nbp == ESK_MACH_BREAKPOINTS)
        return -1;
    m->bp[m->nbp++] = pc;
    return 0;
}

int esk_mach_bp_remove(struct esk_mach *m, u32 pc)
{
    int i;
    for (i = 0; i < m->nbp; i++)
        if (m->bp[i] == pc) {
            m->bp[i] = m->bp[--m->nbp];
            return 0;
        }
    return -1;
}
