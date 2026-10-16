/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* rf.c -- the register file (rf.h says what it is and how models sit on it).
 *
 * One device on the bus covers the peripherals' span, from the lowest
 * register to the highest; an access goes to the peripheral of the register
 * it touches, that is, to its model's hooks or the plain register file's. A
 * byte or halfword access reads or writes those bytes alone.
 *
 * The description (schemas/registers.schema.json):
 *   { "device": "STM32F103xx",
 *     "interrupts": { "TIM2": 28, ... },
 *     "peripherals": [
 *       { "name": "RCC", "base": "0x40021000", "interrupts": ["RCC"],
 *         "registers": [ { "name": "CR", "offset": "0x00", "reset": "0x83",
 *                          "fields": [ ["HSION", 0, 1], ["HSIRDY", 1, 1, "r"] ] } ] },
 *       { "name": "GPIOB", "derived_from": "GPIOA", "base": "0x40010C00" } ] }
 * A field is [name, lsb, width] with an optional access in the reference
 * manuals' notation: rw (the default), r, w, rc_w0, rc_w1, rc_r, rs, t, w1.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/svd-map.c (the access
 * rules, the clock gates and the timekeeping of the models), reading the
 * JSON description instead of an SVD file. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "mcu/rf.h"
#include "util/num.h"
#include "util/str.h"

#define RF_MAX_PERIPHS 256
#define RF_MAX_REGS 64
#define RF_MAX_FIELDS 32

static const struct esk_dev_ops rf_plain_ops;

void esk_rf_diag(struct esk_mach *m, int severity, const char *code, const char *fmt, ...)
{
    char msg[256];
    va_list ap;
    if (!m->host.diag)
        return;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    m->host.diag(m->host.ctx, severity, code, msg);
}

static u64 field_mask(const struct esk_rf_field *f)
{
    u64 m = f->width >= 64 ? ~UINT64_C(0) : (UINT64_C(1) << f->width) - 1u;
    return m << f->lsb;
}

static u64 lanes(int at, int n)
{
    u64 m = n >= 8 ? ~UINT64_C(0) : (UINT64_C(1) << (8 * n)) - 1u;
    return m << (8 * at);
}

/* ---- building it ---------------------------------------------------------- */

static int access_code(const char *a, int *acc, int *mwv, int *ract)
{
    *acc = ESK_ACC_RW;
    *mwv = ESK_MWV_MODIFY;
    *ract = ESK_RA_NONE;
    if (!a || !strcmp(a, "rw"))
        return 0;
    if (!strcmp(a, "r"))
        *acc = ESK_ACC_RO;
    else if (!strcmp(a, "w"))
        *acc = ESK_ACC_WO;
    else if (!strcmp(a, "w1"))
        *acc = ESK_ACC_W1;
    else if (!strcmp(a, "rw1"))
        *acc = ESK_ACC_RW1;
    else if (!strcmp(a, "rc_w0"))
        *mwv = ESK_MWV_0CLR;
    else if (!strcmp(a, "rc_w1"))
        *mwv = ESK_MWV_1CLR;
    else if (!strcmp(a, "rs"))
        *mwv = ESK_MWV_1SET;
    else if (!strcmp(a, "t"))
        *mwv = ESK_MWV_1TOG;
    else if (!strcmp(a, "rc_r"))
        *ract = ESK_RA_CLEAR;
    else
        return -1;
    return 0;
}

static int num(const esk_json *j, u64 *out)
{
    return j && esk_json_to_u64(j, out) == 0 ? 0 : -1;
}

static const esk_json *find_periph(const esk_json *list, const char *name)
{
    size_t i;
    for (i = 0; i < esk_json_len(list); i++) {
        const esk_json *p = esk_json_at(list, i);
        if (esk_str_eq(esk_json_get_str(p, "name", ""), name))
            return p;
    }
    return NULL;
}

static es_status read_regs(struct esk_mach *m, struct esk_rf_periph *p, const esk_json *regs,
                           u32 base, char *why, size_t cap)
{
    size_t i, k, nr = esk_json_len(regs);
    if (nr == 0 || nr > RF_MAX_REGS) {
        snprintf(why, cap, "%s: %s registers", p->name, nr ? "too many" : "no");
        return ES_ESCHEMA;
    }
    p->reg = (struct esk_rf_reg **)esk_mach_alloc(m, nr * sizeof *p->reg);
    if (!p->reg)
        return ES_ENOMEM;
    for (i = 0; i < nr; i++) {
        const esk_json *rj = esk_json_at(regs, i), *fl = esk_json_get(rj, "fields");
        struct esk_rf_reg *r = (struct esk_rf_reg *)esk_mach_alloc(m, sizeof *r);
        u64 off = 0, reset = 0, size = 32;
        size_t nf = fl ? esk_json_len(fl) : 0;
        if (!r)
            return ES_ENOMEM;
        r->name = esk_mach_strdup(m, esk_json_get_str(rj, "name", ""));
        if (!r->name || !r->name[0] || num(esk_json_get(rj, "offset"), &off) || off > 0xffffu) {
            snprintf(why, cap, "%s: register %zu has no name or offset", p->name, i);
            return ES_ESCHEMA;
        }
        if (esk_json_get(rj, "reset") && num(esk_json_get(rj, "reset"), &reset)) {
            snprintf(why, cap, "%s.%s: bad reset value", p->name, r->name);
            return ES_ESCHEMA;
        }
        if (esk_json_get(rj, "size") && (num(esk_json_get(rj, "size"), &size) || (size != 16 && size != 32))) {
            snprintf(why, cap, "%s.%s: size is 16 or 32", p->name, r->name);
            return ES_ESCHEMA;
        }
        if (nf > RF_MAX_FIELDS) {
            snprintf(why, cap, "%s.%s: too many fields", p->name, r->name);
            return ES_ESCHEMA;
        }
        r->addr = base + (u32)off;
        r->bytes = (int)(size / 8u);
        r->reset = r->value = reset;
        r->p = p;
        r->nf = nf ? (int)nf : 1;
        r->f = (struct esk_rf_field *)esk_mach_alloc(m, (size_t)r->nf * sizeof *r->f);
        if (!r->f)
            return ES_ENOMEM;
        if (!nf) {
            r->f[0].width = 8 * r->bytes;
        }
        for (k = 0; k < nf; k++) {
            const esk_json *fj = esk_json_at(fl, k);
            struct esk_rf_field *f = &r->f[k];
            u64 lsb = 0, width = 0;
            const char *acc = esk_json_len(fj) > 3 ? esk_json_str(esk_json_at(fj, 3)) : NULL;
            f->name = esk_mach_strdup(m, esk_json_str(esk_json_at(fj, 0)) ? esk_json_str(esk_json_at(fj, 0)) : "");
            if (!f->name || num(esk_json_at(fj, 1), &lsb) || num(esk_json_at(fj, 2), &width) ||
                width == 0 || lsb + width > size || access_code(acc, &f->acc, &f->mwv, &f->ract)) {
                snprintf(why, cap, "%s.%s: field %zu is not [name, lsb, width, access?]", p->name,
                         r->name, k);
                return ES_ESCHEMA;
            }
            f->lsb = (int)lsb;
            f->width = (int)width;
        }
        p->reg[i] = r;
        if (r->addr + (u32)r->bytes > p->end)
            p->end = r->addr + (u32)r->bytes;
    }
    p->nreg = (int)nr;
    return ES_OK;
}

static es_status read_irqs(struct esk_mach *m, struct esk_rf_periph *p, const esk_json *names,
                           const esk_json *table, char *why, size_t cap)
{
    size_t i, n = names ? esk_json_len(names) : 0;
    const char **nm;
    int *nums;
    if (!n)
        return ES_OK;
    nm = (const char **)esk_mach_alloc(m, n * sizeof *nm);
    nums = (int *)esk_mach_alloc(m, n * sizeof *nums);
    if (!nm || !nums)
        return ES_ENOMEM;
    for (i = 0; i < n; i++) {
        const char *s = esk_json_str(esk_json_at(names, i));
        const esk_json *v = s ? esk_json_get(table, s) : NULL;
        u64 x = 0;
        if (!v || num(v, &x) || x > 239) {
            snprintf(why, cap, "%s: interrupt %s is not in the device's table", p->name, s ? s : "?");
            return ES_ESCHEMA;
        }
        nm[i] = esk_mach_strdup(m, s);
        if (!nm[i])
            return ES_ENOMEM;
        nums[i] = (int)x;
    }
    p->irq_names = nm;
    p->irq_nums = nums;
    p->nirq = (int)n;
    return ES_OK;
}

static void sort_regs(struct esk_rf_reg **r, int n)
{
    int i, j;
    for (i = 1; i < n; i++) {
        struct esk_rf_reg *x = r[i];
        for (j = i; j > 0 && r[j - 1]->addr > x->addr; j--)
            r[j] = r[j - 1];
        r[j] = x;
    }
}

static const struct esk_dev_ops rf_map_ops;

es_status esk_rf_create(struct esk_mach *m, const esk_json *desc, char *why, size_t cap)
{
    const esk_json *list = esk_json_get(desc, "peripherals");
    const esk_json *table = esk_json_get(desc, "interrupts");
    struct esk_rf *rf;
    size_t i, np = list ? esk_json_len(list) : 0;
    int k, total = 0;
    es_status st;
    why[0] = 0;
    if (!np || np > RF_MAX_PERIPHS) {
        snprintf(why, cap, "the register description has %s peripherals", np ? "too many" : "no");
        return ES_ESCHEMA;
    }
    rf = (struct esk_rf *)esk_mach_alloc(m, sizeof *rf);
    if (!rf)
        return ES_ENOMEM;
    rf->m = m;
    rf->device = esk_mach_strdup(m, esk_json_get_str(desc, "device", "?"));
    rf->p = (struct esk_rf_periph *)esk_mach_alloc(m, np * sizeof *rf->p);
    if (!rf->p || !rf->device)
        return ES_ENOMEM;
    for (i = 0; i < np; i++) {
        const esk_json *pj = esk_json_at(list, i), *src = pj;
        struct esk_rf_periph *p = &rf->p[i];
        u64 base = 0;
        const char *from = esk_json_get_str(pj, "derived_from", NULL);
        p->name = esk_mach_strdup(m, esk_json_get_str(pj, "name", ""));
        if (!p->name || !p->name[0] || num(esk_json_get(pj, "base"), &base) || base > 0xffff0000u) {
            snprintf(why, cap, "peripheral %zu has no name or base", i);
            return ES_ESCHEMA;
        }
        if (from) {
            src = find_periph(list, from);
            if (!src || esk_json_get_str(src, "derived_from", NULL)) {
                snprintf(why, cap, "%s: derived_from %s is not a peripheral with registers", p->name, from);
                return ES_ESCHEMA;
            }
        }
        p->base = p->end = (u32)base;
        p->sim = m;
        p->rf = rf;
        p->ops = &rf_plain_ops;
        p->ctx = p;
        st = read_regs(m, p, esk_json_get(src, "registers"), p->base, why, cap);
        if (st == ES_OK)
            st = read_irqs(m, p, esk_json_get(pj, "interrupts") ? esk_json_get(pj, "interrupts")
                                                                 : esk_json_get(src, "interrupts"),
                           table, why, cap);
        if (st != ES_OK)
            return st;
        total += p->nreg;
    }
    rf->np = (int)np;
    rf->reg = (struct esk_rf_reg **)esk_mach_alloc(m, (size_t)total * sizeof *rf->reg);
    if (!rf->reg)
        return ES_ENOMEM;
    for (i = 0; i < np; i++)
        for (k = 0; k < rf->p[i].nreg; k++)
            rf->reg[rf->nreg++] = rf->p[i].reg[k];
    sort_regs(rf->reg, rf->nreg);
    for (k = 1; k < rf->nreg; k++)
        if (rf->reg[k]->addr < rf->reg[k - 1]->addr + (u32)rf->reg[k - 1]->bytes) {
            snprintf(why, cap, "%s.%s overlaps %s.%s", rf->reg[k]->p->name, rf->reg[k]->name,
                     rf->reg[k - 1]->p->name, rf->reg[k - 1]->name);
            return ES_ESCHEMA;
        }
    rf->lo = rf->reg[0]->addr & ~0x3ffu;
    rf->hi = rf->reg[rf->nreg - 1]->addr + 4u;
    m->rf = rf;
    return esk_mach_add_dev(m, rf->lo, rf->hi - rf->lo, &rf_map_ops, rf);
}

/* ---- lookups ---------------------------------------------------------------- */

static struct esk_rf_reg *reg_at(struct esk_rf *rf, u32 a, int n)
{
    int lo = 0, hi = rf->nreg; /* the first whose end is past a */
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        struct esk_rf_reg *r = rf->reg[mid];
        if ((u64)r->addr + (u64)r->bytes <= a)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < rf->nreg && rf->reg[lo]->addr < (u64)a + (u64)n)
        return rf->reg[lo];
    return 0;
}

static int reg_index(struct esk_rf *rf, const struct esk_rf_reg *r)
{
    int lo = 0, hi = rf->nreg - 1;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (rf->reg[mid]->addr < r->addr)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

struct esk_rf_periph *esk_rf_periph(struct esk_rf *rf, const char *name)
{
    int i;
    for (i = 0; rf && i < rf->np; i++)
        if (!strcmp(rf->p[i].name, name))
            return &rf->p[i];
    return 0;
}

struct esk_rf_periph *esk_rf_periph_at(struct esk_rf *rf, int i)
{
    return rf && i >= 0 && i < rf->np ? &rf->p[i] : 0;
}

struct esk_rf_reg *esk_rf_reg(struct esk_rf_periph *p, const char *name)
{
    int i;
    for (i = 0; p && i < p->nreg; i++)
        if (!strcmp(p->reg[i]->name, name))
            return p->reg[i];
    return 0;
}

u64 esk_rf_mask(const struct esk_rf_reg *r, const char *field)
{
    int i;
    for (i = 0; r && i < r->nf; i++)
        if (r->f[i].name && !strcmp(r->f[i].name, field))
            return field_mask(&r->f[i]);
    return 0;
}

void esk_rf_set_mwv(struct esk_rf_reg *r, u64 mask, int mwv)
{
    int i;
    for (i = 0; i < r->nf; i++)
        if (field_mask(&r->f[i]) & mask)
            r->f[i].mwv = mwv;
}

void esk_rf_set_reset(struct esk_rf_reg *r, u64 v) { r->reset = r->value = v; }

void esk_rf_set_acc(struct esk_rf_reg *r, u64 mask, int acc)
{
    int i;
    for (i = 0; i < r->nf; i++)
        if (field_mask(&r->f[i]) & mask)
            r->f[i].acc = acc;
}

u64 esk_rf_fields(const struct esk_rf_reg *r)
{
    u64 m = 0;
    int i;
    for (i = 0; i < r->nf; i++)
        m |= field_mask(&r->f[i]);
    return m;
}

int esk_rf_irq(const struct esk_rf_periph *p, const char *want)
{
    int i;
    for (i = 0; want && i < p->nirq; i++)
        if (strstr(p->irq_names[i], want))
            return p->irq_nums[i];
    return p->nirq ? p->irq_nums[0] : -1;
}

void esk_rf_interrupt(struct esk_rf_periph *p, int irq)
{
    if (irq >= 0)
        p->sim->cpu->ops->interrupt(p->sim->cpu, 16 + irq);
}

void esk_rf_hw(struct esk_rf_reg *r, u64 v) { r->value = v; }

/* ---- accesses ----------------------------------------------------------- */

/* a write of the bits wm (wv) of r, as its fields take it */
static u64 write_value(struct esk_rf_reg *r, u64 old, u64 wm, u64 wv)
{
    u64 nv = old;
    int i;
    for (i = 0; i < r->nf; i++) {
        const struct esk_rf_field *f = &r->f[i];
        u64 fm = field_mask(f), m = fm & wm;
        if (!m || f->acc == ESK_ACC_RO)
            continue;
        if (f->acc == ESK_ACC_W1 || f->acc == ESK_ACC_RW1) {
            if (r->once & fm)
                continue;
            r->once |= fm;
        }
        switch (f->mwv) {
        case ESK_MWV_MODIFY: nv = (nv & ~m) | (wv & m); break;
        case ESK_MWV_1CLR: nv &= ~(wv & m); break;
        case ESK_MWV_1SET: nv |= wv & m; break;
        case ESK_MWV_1TOG: nv ^= wv & m; break;
        case ESK_MWV_0CLR: nv &= ~(~wv & m); break;
        case ESK_MWV_0SET: nv |= ~wv & m; break;
        case ESK_MWV_0TOG: nv ^= ~wv & m; break;
        case ESK_MWV_CLEAR: nv &= ~m; break;
        case ESK_MWV_SET: nv |= m; break;
        default: break;
        }
    }
    return nv;
}

static u64 read_value(const struct esk_rf_reg *r, u64 rm)
{
    u64 v = r->value;
    int i;
    for (i = 0; i < r->nf; i++) {
        const struct esk_rf_field *f = &r->f[i];
        if (f->acc == ESK_ACC_WO || f->acc == ESK_ACC_W1)
            v &= ~field_mask(f);
    }
    return v & rm;
}

static void read_action(struct esk_rf_reg *r, u64 rm)
{
    u64 nv = r->value;
    int i;
    for (i = 0; i < r->nf; i++) {
        const struct esk_rf_field *f = &r->f[i];
        u64 fm = field_mask(f);
        if (!(fm & rm))
            continue;
        if (f->ract == ESK_RA_CLEAR)
            nv &= ~fm;
        else if (f->ract == ESK_RA_SET)
            nv |= fm;
    }
    r->value = nv;
}

/* the registers [a, a + n) touches: 0, or -1 when it touches none */
static int access(struct esk_rf *rf, u32 a, int n, u32 *v, int write, int side)
{
    struct esk_rf_reg *r = reg_at(rf, a, n);
    u32 out = 0;
    int i;
    if (!r)
        return -1;
    for (i = reg_index(rf, r); i < rf->nreg; i++) {
        u32 lo;
        u64 e1, e2, rm;
        int cnt, at, sh;
        r = rf->reg[i];
        if ((u64)r->addr >= (u64)a + (u64)n)
            break;
        lo = a > r->addr ? a : r->addr;
        e1 = (u64)a + (u64)n;
        e2 = (u64)r->addr + (u64)r->bytes;
        cnt = (int)((e1 < e2 ? e1 : e2) - lo);
        at = (int)(lo - r->addr);
        sh = (int)(lo - a);
        rm = lanes(at, cnt);
        if (write) {
            u64 wv = ((u64)(*v >> (8 * sh))) << (8 * at);
            r->value = write_value(r, r->value, rm, wv);
        } else {
            u64 x = read_value(r, rm) >> (8 * at);
            if (side)
                read_action(r, rm);
            out |= (u32)(x << (8 * sh));
        }
    }
    if (!write)
        *v = out;
    return 0;
}

u32 esk_rf_read(struct esk_rf_periph *p, u32 off, int n)
{
    u32 v = 0;
    if (access(p->rf, p->base + off, n, &v, 0, !p->sim->bus.debug))
        esk_bus_fault(&p->sim->bus);
    return v;
}

void esk_rf_write(struct esk_rf_periph *p, u32 off, int n, u32 v)
{
    if (access(p->rf, p->base + off, n, &v, 1, 1))
        esk_bus_fault(&p->sim->bus);
}

static const char *access_kind(int n, int write)
{
    static const char *const k[2][5] = {
        { "an access", "a byte read", "a halfword read", "an access", "a word read" },
        { "an access", "a byte write", "a halfword write", "an access", "a word write" },
    };
    return n >= 1 && n <= 4 ? k[write != 0][n] : "an access";
}

/* An access no register answers: a bus fault, and a diagnostic naming it,
 * since a firmware's HardFault rarely says which store it was. */
static void hole(struct esk_rf *rf, u32 a, int n, int write)
{
    struct esk_mach *m = rf->m;
    const struct esk_rf_periph *in = 0;
    int i;
    esk_bus_fault(&m->bus);
    if (m->bus.debug || ++rf->faults > 10)
        return;
    for (i = 0; i < rf->np; i++)
        if (a - rf->p[i].base < rf->p[i].end - rf->p[i].base &&
            (!in || rf->p[i].end - rf->p[i].base < in->end - in->base))
            in = &rf->p[i];
    if (in)
        esk_rf_diag(m, ES_SEV_WARNING, "mcu.bus_error", "%s at 0x%08lx (pc 0x%08lx): %s has no register at +0x%03lx",
                    access_kind(n, write), (unsigned long)a, (unsigned long)m->cpu->ops->pc(m->cpu),
                    in->name, (unsigned long)(a - in->base));
    else
        esk_rf_diag(m, ES_SEV_WARNING, "mcu.bus_error", "%s at 0x%08lx (pc 0x%08lx): no peripheral of %s is there",
                    access_kind(n, write), (unsigned long)a, (unsigned long)m->cpu->ops->pc(m->cpu),
                    rf->device);
}

int esk_rf_gated(const struct esk_rf_periph *p)
{
    return p->gate && !(p->gate->value & p->gate_mask);
}

/* an access while the peripheral's clock is off: the first is a warning */
static void gated_access(struct esk_rf *rf, struct esk_rf_periph *p, u32 a, int n, int write)
{
    struct esk_mach *m = rf->m;
    struct esk_rf_reg *r = reg_at(rf, a, n);
    if (m->bus.debug || p->gate_warned)
        return;
    p->gate_warned = 1;
    esk_rf_diag(m, ES_SEV_WARNING, "mcu.clock_gated", "%s at pc 0x%08lx: %s.%s %s while its clock is off (%s is 0)",
                write ? "a write" : "a read", (unsigned long)m->cpu->ops->pc(m->cpu), p->name,
                r ? r->name : "?", write ? "is ignored" : "reads as 0", p->gate_name);
}

static u32 map_read(void *ctx, u32 off, int n)
{
    struct esk_rf *rf = (struct esk_rf *)ctx;
    u32 a = rf->lo + off, v = 0;
    struct esk_rf_reg *r = reg_at(rf, a, n);
    struct esk_rf_periph *p;
    if (!r) {
        hole(rf, a, n, 0);
        return 0;
    }
    p = r->p;
    if (esk_rf_gated(p)) {
        gated_access(rf, p, a, n, 0);
        return 0;
    }
    if (rf->m->bus.debug) {
        access(rf, a, n, &v, 0, 0);
        return v;
    }
    return p->ops->read ? p->ops->read(p->ctx, a - p->base, n) : 0;
}

static void map_write(void *ctx, u32 off, int n, u32 v)
{
    struct esk_rf *rf = (struct esk_rf *)ctx;
    u32 a = rf->lo + off;
    struct esk_rf_reg *r = reg_at(rf, a, n);
    struct esk_rf_periph *p;
    if (!r) {
        hole(rf, a, n, 1);
        return;
    }
    p = r->p;
    if (esk_rf_gated(p)) {
        gated_access(rf, p, a, n, 1);
        return;
    }
    if (p->ops->write)
        p->ops->write(p->ctx, a - p->base, n, v);
}

void esk_rf_clock(struct esk_rf_periph *p, int *running, int on)
{
    struct esk_rf *rf = p->rf;
    int i;
    on = on != 0;
    if (*running == on)
        return;
    esk_mach_clock(p->sim, running, on);
    if (on) {
        if (rf->ntk < ESK_RF_TICKERS)
            rf->tk[rf->ntk++] = p;
        return;
    }
    for (i = 0; i < rf->ntk; i++)
        if (rf->tk[i] == p) {
            rf->tk[i] = rf->tk[--rf->ntk];
            break;
        }
}

static void map_reset(void *ctx)
{
    struct esk_rf *rf = (struct esk_rf *)ctx;
    int i;
    rf->ntk = 0; /* the models' resets stop their clocks */
    for (i = 0; i < rf->nreg; i++) {
        rf->reg[i]->value = rf->reg[i]->reset;
        rf->reg[i]->once = 0;
    }
    for (i = 0; i < rf->np; i++) {
        rf->p[i].gate_warned = 0;
        if (rf->p[i].ops->reset)
            rf->p[i].ops->reset(rf->p[i].ctx);
    }
    rf->faults = 0;
}

static void map_tick(void *ctx, u32 cycles)
{
    struct esk_rf *rf = (struct esk_rf *)ctx;
    int i;
    for (i = 0; i < rf->ntk; i++)
        if (rf->tk[i]->ops->tick)
            rf->tk[i]->ops->tick(rf->tk[i]->ctx, cycles);
}

static int map_next_event(void *ctx, u32 *cycles)
{
    struct esk_rf *rf = (struct esk_rf *)ctx;
    int any = 0, i;
    for (i = 0; i < rf->ntk; i++) {
        u32 c = 0;
        if (rf->tk[i]->ops->next_event && rf->tk[i]->ops->next_event(rf->tk[i]->ctx, &c) &&
            (!any || c < *cycles)) {
            *cycles = c;
            any = 1;
        }
    }
    return any;
}

static const struct esk_dev_ops rf_map_ops = {
    "registers", map_read, map_write, map_reset, map_tick, map_next_event,
};

/* a peripheral with no model: the register file alone, said once */
static void plain_note(struct esk_rf_periph *p)
{
    if (p->plain_warned || p->sim->bus.debug)
        return;
    p->plain_warned = 1;
    esk_rf_diag(p->sim, ES_SEV_WARNING, "mcu.unsupported_access",
                "%s has no behavioural model: its registers only keep what is written (pc 0x%08lx)",
                p->name, (unsigned long)p->sim->cpu->ops->pc(p->sim->cpu));
}

static u32 plain_read(void *ctx, u32 off, int n)
{
    struct esk_rf_periph *p = (struct esk_rf_periph *)ctx;
    plain_note(p);
    return esk_rf_read(p, off, n);
}

static void plain_write(void *ctx, u32 off, int n, u32 v)
{
    struct esk_rf_periph *p = (struct esk_rf_periph *)ctx;
    plain_note(p);
    esk_rf_write(p, off, n, v);
}

static const struct esk_dev_ops rf_plain_ops = {
    "registers", plain_read, plain_write, 0, 0, 0,
};

es_status esk_rf_bind(struct esk_mach *m, const esk_json *binding, char *missing, size_t cap)
{
    struct esk_rf *rf = m->rf;
    size_t k;
    int i;
    missing[0] = 0;
    if (!rf)
        return ES_ESTATE;
    for (k = 0; k < esk_json_len(binding); k++) {
        const esk_json *b = esk_json_at(binding, k);
        const char *pat = esk_json_get_str(b, "match", ""), *name = esk_json_get_str(b, "model", "");
        const struct esk_rf_model_type *t = esk_rf_model_find(name);
        if (!t) {
            size_t len = strlen(missing);
            if (len + strlen(name) + 3 < cap)
                snprintf(missing + len, cap - len, "%s%s", len ? ", " : "", name);
            continue;
        }
        for (i = 0; i < rf->np; i++) {
            struct esk_rf_periph *p = &rf->p[i];
            void *ctx;
            if (p->ops != &rf_plain_ops || !esk_glob_match(pat, p->name))
                continue;
            ctx = t->create(m, p);
            if (m->oom)
                return ES_ENOMEM;
            if (!ctx)
                continue;
            p->ops = t->ops;
            p->ctx = ctx;
            p->model = t->name;
        }
    }
    return ES_OK;
}
