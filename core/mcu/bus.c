/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* bus.c -- memory, bit-band aliases and memory-mapped devices.
 *
 * An access goes to the first of these that answers at its address:
 *   1. a memory region holding all of its bytes (RAM, or flash, which
 *      ignores the core's stores once the image is loaded);
 *   2. a bit-band alias: one word per bit of the target, a read giving
 *      the bit and a write setting or clearing it;
 *   3. a device, in the order the board and the core added them, so a
 *      device inside a wider one (SysTick inside the system control
 *      space, a UART inside the peripheral space that reads as zero) is
 *      added first.
 * Nothing answering is a bus error, which the core turns into its fault
 * (bus->fail_addr says where); so is a device refusing the access
 * (bus_fault: the SVD's register file, where no register is).
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/bus.c. Setup errors are
 * statuses (ES_ELIMIT for a full table, ES_ENOMEM) instead of exits; a
 * region's memory is the machine's. */
#include <string.h>

#include "mcu/mach.h"

es_status esk_bus_add_region(struct esk_mach *m, u32 base, u32 size, int kind)
{
    struct esk_bus *b = &m->bus;
    struct esk_region *r;
    if (!size)
        return ES_OK;
    if (b->nrg == ESK_BUS_REGIONS)
        return ES_ELIMIT;
    r = &b->rg[b->nrg];
    r->base = base;
    r->size = size;
    r->rom = 0;
    r->kind = kind;
    r->mirror = 0;
    r->mem = (u8 *)esk_mach_alloc(m, size);
    if (!r->mem)
        return ES_ENOMEM;
    b->nrg++;
    return ES_OK;
}

void esk_bus_code_changed(struct esk_bus *b)
{
    int i;
    for (i = 0; i < b->nrg; i++)
        b->rg[i].gen++;
}

void esk_bus_erase(struct esk_region *r)
{
    r->gen++;
    if (r->factory) {
        memcpy(r->mem, r->factory, r->size);
        return;
    }
    if (!r->erased) {
        memset(r->mem, 0, r->size);
        return;
    }
    for (u32 i = 0; i < r->size; i++)
        r->mem[i] = (u8)(r->erased >> (8 * (i & 3)));
}

es_status esk_bus_add_device(struct esk_bus *b, u32 base, u32 size,
                             const struct esk_dev_ops *ops, void *ctx)
{
    if (b->ndev == ESK_BUS_DEVICES)
        return ES_ELIMIT;
    b->dev[b->ndev].base = base;
    b->dev[b->ndev].size = size;
    b->dev[b->ndev].ops = ops;
    b->dev[b->ndev].ctx = ctx;
    b->ndev++;
    return ES_OK;
}

es_status esk_bus_add_mirror(struct esk_bus *b, u32 base, u32 target)
{
    struct esk_region *t = esk_bus_region(b, target, 1);
    if (!t)
        return ES_ENOTFOUND;
    if (b->nrg == ESK_BUS_REGIONS)
        return ES_ELIMIT;
    b->rg[b->nrg] = *t;
    b->rg[b->nrg].base = base;
    b->rg[b->nrg].mirror = 1;
    b->nrg++;
    return ES_OK;
}

es_status esk_bus_remap_mirror(struct esk_bus *b, u32 base, u32 target, u32 size)
{
    struct esk_region *t = size ? esk_bus_region(b, target, 1) : 0;
    int i;
    for (i = 0; i < b->nrg; i++) {
        struct esk_region *r = &b->rg[i];
        if (!r->mirror || r->base != base)
            continue;
        if (!t || t == r) {
            r->size = 0;
            r->mem = 0;
            r->gen++;
            return ES_OK;
        }
        r->mem = t->mem + (target - t->base);
        r->gen++;
        r->size = size < t->size - (target - t->base) ? size : t->size - (target - t->base);
        r->rom = t->rom;
        r->kind = t->kind;
        r->erased = t->erased;
        return ES_OK;
    }
    return ES_ENOTFOUND;
}

es_status esk_bus_add_alias(struct esk_bus *b, u32 base, u32 size, u32 target)
{
    if (!size)
        return ES_OK;
    if (b->nbb == ESK_BUS_ALIASES)
        return ES_ELIMIT;
    b->bb[b->nbb].base = base;
    b->bb[b->nbb].size = size;
    b->bb[b->nbb].target = target;
    b->nbb++;
    return ES_OK;
}

static int alias_of(struct esk_bus *b, u32 a, u32 *target, int *bit)
{
    for (int i = 0; i < b->nbb; i++)
        if (a - b->bb[i].base < b->bb[i].size) {
            *target = b->bb[i].target + ((a - b->bb[i].base) >> 5);
            *bit = (int)((a >> 2) & 7);
            return 1;
        }
    return 0;
}

static struct esk_device *device_of(struct esk_bus *b, u32 a)
{
    for (int i = 0; i < b->ndev; i++)
        if (a - b->dev[i].base < b->dev[i].size)
            return &b->dev[i];
    return 0;
}

static int rd(struct esk_bus *b, u32 a, int n, u32 *v)
{
    struct esk_region *r = esk_bus_region(b, a, (u32)n);
    if (r) {
        *v = esk_mem_rd_le(r->mem + (a - r->base), n);
        return 0;
    }
    u32 t, x;
    int bit;
    if (alias_of(b, a, &t, &bit)) {
        if (rd(b, t, 1, &x))
            return -1;
        *v = (x >> bit) & 1;
        return 0;
    }
    struct esk_device *d = device_of(b, a);
    if (d) {
        /* a device that keeps no time (the core's own registers) owes nothing */
        if (b->sync && (d->ops->tick || d->ops->next_event))
            b->sync(b->sync_ctx);
        b->touched = 1;
        x = d->ops->read ? d->ops->read(d->ctx, a - d->base, n) : 0;
        if (b->dev_fault) {
            b->dev_fault = 0;
            b->fail_addr = a;
            return -1;
        }
        *v = n == 4 ? x : x & ((1u << (8 * n)) - 1);
        return 0;
    }
    b->fail_addr = a;
    b->unmapped = 1;
    return -1;
}

static int wr(struct esk_bus *b, u32 a, int n, u32 v, int debug)
{
    if (!debug && b->snoop)
        b->snoop(b->snoop_ctx, a);
    struct esk_region *r = esk_bus_region(b, a, (u32)n);
    if (r) {
        if (!r->rom || debug)
            esk_mem_wr_le(r->mem + (a - r->base), n, v);
        if (r->rom && debug)
            esk_bus_code_changed(b);
        return 0;
    }
    u32 t, byte;
    int bit;
    if (alias_of(b, a, &t, &bit)) {
        if (rd(b, t, 1, &byte))
            return -1;
        return wr(b, t, 1, (v & 1) ? byte | 1u << bit : byte & ~(1u << bit),
                  debug);
    }
    struct esk_device *d = device_of(b, a);
    if (d) {
        if (b->sync && (d->ops->tick || d->ops->next_event)) {
            b->sync(b->sync_ctx);
            b->written = 1;
        }
        b->touched = 1;
        if (d->ops->write)
            d->ops->write(d->ctx, a - d->base, n, v);
        if (b->dev_fault) {
            b->dev_fault = 0;
            b->fail_addr = a;
            return -1;
        }
        return 0;
    }
    b->fail_addr = a;
    b->unmapped = 1;
    return -1;
}

/* Whether an access of the core's touches a watchpoint; the first one
 * touched is recorded. As QEMU's, the address reported is the higher of
 * the access's and the watchpoint's. */
int esk_bus_watch_check(struct esk_bus *b, u32 a, int n, int write)
{
    for (int i = 0; i < b->nwatch; i++) {
        struct esk_watch *w = &b->watch[i];
        int k = w->kind;
        if (k != ESK_WATCH_ACCESS && k != (write ? ESK_WATCH_WRITE : ESK_WATCH_READ))
            continue;
        /* the two ranges overlap */
        if (a - w->addr < w->len || w->addr - a < (u32)n) {
            if (!b->watch_hit) {
                b->watch_hit = 1;
                b->watch_kind = k;
                b->watch_addr = a > w->addr ? a : w->addr;
            }
            return 1;
        }
    }
    return 0;
}

int esk_bus_read(struct esk_bus *b, u32 a, int n, u32 *v)
{
    return rd(b, a, n, v);
}

int esk_bus_write(struct esk_bus *b, u32 a, int n, u32 v)
{
    return wr(b, a, n, v, 0);
}

/* b->debug tells a device the access is the debugger's: no side effect
 * of a read, no trace */
int esk_bus_debug_read(struct esk_bus *b, u32 a, int n, u32 *v)
{
    b->debug = 1;
    int r = rd(b, a, n, v);
    b->debug = 0;
    return r;
}

int esk_bus_debug_write(struct esk_bus *b, u32 a, int n, u32 v)
{
    b->debug = 1;
    int r = wr(b, a, n, v, 1);
    b->debug = 0;
    return r;
}

int esk_bus_watch_add(struct esk_bus *b, int kind, u32 addr, u32 len)
{
    if (b->nwatch == ESK_BUS_WATCHES || !len)
        return -1;
    b->watch[b->nwatch].kind = kind;
    b->watch[b->nwatch].addr = addr;
    b->watch[b->nwatch].len = len;
    b->nwatch++;
    return 0;
}

int esk_bus_watch_remove(struct esk_bus *b, int kind, u32 addr, u32 len)
{
    for (int i = 0; i < b->nwatch; i++) {
        struct esk_watch *w = &b->watch[i];
        if (w->kind == kind && w->addr == addr && w->len == len) {
            *w = b->watch[--b->nwatch];
            return 0;
        }
    }
    return -1;
}
