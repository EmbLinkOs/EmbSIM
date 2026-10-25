/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* build.c -- a machine from an MCU description (SPEC-006 §4, SPEC-009 §2):
 * its memory regions and aliases, bit-band aliases, the core, the register
 * file from the register description, and the peripheral models bound by
 * the description's globs, in its order (the RCC first: the others' clock
 * gates are its registers). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mcu/build.h"
#include "mcu/mips/mips.h"
#include "mcu/pic24/pic24.h"
#include "mcu/rf.h"
#include "util/num.h"
#include "util/str.h"

static int u32_of(const esk_json *j, u32 *out)
{
    uint64_t v = 0;
    if (!j || esk_json_to_u64(j, &v) != 0 || v > 0xffffffffu)
        return -1;
    *out = (u32)v;
    return 0;
}

static int mem_kind(const char *k)
{
    if (esk_str_eq(k, "flash"))
        return ESK_MEM_FLASH;
    if (esk_str_eq(k, "rom"))
        return ESK_MEM_ROM;
    if (esk_str_eq(k, "alias"))
        return ESK_MEM_ALIAS;
    if (esk_str_eq(k, "external"))
        return ESK_MEM_EXTERNAL;
    if (esk_str_eq(k, "window"))
        return ESK_MEM_WINDOW;
    return ESK_MEM_RAM;
}

/* a memory the core does not address: the description's `erased` word in
 * every 4 bytes, as a new part's flash */
static es_status add_store(struct esk_mach *m, const esk_json *r, u32 size)
{
    struct esk_store *s;
    u32 erased = 0, k;
    if (m->nstore == ESK_MACH_STORES || size == 0)
        return ES_ELIMIT;
    s = &m->store[m->nstore];
    if (esk_json_get(r, "erased"))
        (void)u32_of(esk_json_get(r, "erased"), &erased);
    s->name = esk_mach_strdup(m, esk_json_get_str(r, "name", "?"));
    s->mem = (u8 *)esk_mach_alloc(m, size);
    if (!s->name || !s->mem)
        return ES_ENOMEM;
    s->size = size;
    s->erased = erased;
    for (k = 0; k < size; k++)
        s->mem[k] = (u8)(erased >> (8 * (k & 3)));
    m->nstore++;
    return ES_OK;
}

/* the bytes a region holds from the factory ({offset, bytes}), kept for
 * every erase */
static es_status contents(struct esk_mach *m, struct esk_region *rg, const esk_json *list, u32 size, char *why,
                          size_t cap)
{
    size_t i, k;
    if (!list || !esk_json_len(list))
        return ES_OK;
    for (i = 0; i < esk_json_len(list); i++) {
        const esk_json *c = esk_json_at(list, i);
        const char *hex = esk_json_get_str(c, "bytes", "");
        u32 off = 0;
        size_t n = strlen(hex) / 2;
        if (u32_of(esk_json_get(c, "offset"), &off) || off > size || n > size - off) {
            snprintf(why, cap, "memory contents %zu lie outside their region", i);
            return ES_ESCHEMA;
        }
        for (k = 0; k < n; k++) {
            char two[3];
            two[0] = hex[2 * k];
            two[1] = hex[2 * k + 1];
            two[2] = 0;
            rg->mem[off + k] = (u8)strtoul(two, NULL, 16);
        }
    }
    rg->factory = (u8 *)esk_mach_alloc(m, size);
    if (!rg->factory)
        return ES_ENOMEM;
    memcpy(rg->factory, rg->mem, size);
    return ES_OK;
}

es_status esk_mach_factory(struct esk_mach *m, u32 base, const unsigned char *data, size_t len)
{
    int i;
    for (i = 0; i < m->bus.nrg; i++) {
        struct esk_region *rg = &m->bus.rg[i];
        if (rg->mirror || base < rg->base || base - rg->base > rg->size ||
            len > (size_t)(rg->size - (base - rg->base)))
            continue;
        memcpy(rg->mem + (base - rg->base), data, len);
        if (!rg->factory) {
            rg->factory = (u8 *)esk_mach_alloc(m, rg->size);
            if (!rg->factory)
                return ES_ENOMEM;
        }
        memcpy(rg->factory, rg->mem, rg->size);
        rg->gen++;
        return ES_OK;
    }
    return ES_ENOTFOUND;
}

es_status esk_mach_build(struct esk_mach *m, const esk_json *mcu, const esk_json *regs, u32 hse_hz,
                         char *why, size_t cap)
{
    const esk_json *mem = esk_json_get(mcu, "memory"), *bb = esk_json_get(mcu, "bitband");
    const esk_json *core = esk_json_get(mcu, "core"), *clocks = esk_json_get(mcu, "clocks");
    struct esk_cpu *cpu = NULL;
    char missing[256];
    size_t i;
    int pass;
    es_status st;
    why[0] = 0;
    /* memory first, then the aliases of it */
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < esk_json_len(mem); i++) {
            const esk_json *r = esk_json_at(mem, i);
            int kind = mem_kind(esk_json_get_str(r, "kind", "ram"));
            u32 base = 0, size = 0, target = 0, erased = 0;
            if ((kind == ESK_MEM_ALIAS) != (pass == 1))
                continue;
            if (u32_of(esk_json_get(r, "base"), &base) || u32_of(esk_json_get(r, "size"), &size)) {
                snprintf(why, cap, "memory region %s has no base or size",
                         esk_json_get_str(r, "name", "?"));
                return ES_ESCHEMA;
            }
            if (kind == ESK_MEM_ALIAS) {
                /* the target's bytes from `target` on, `size` of them (the
                 * ESP32-C3's ROM1 at 0x3FF00000 is its ROM from 0x40040000) */
                if (u32_of(esk_json_get(r, "target"), &target) ||
                    esk_bus_add_mirror(&m->bus, base, target) != ES_OK ||
                    esk_bus_remap_mirror(&m->bus, base, target, size) != ES_OK) {
                    snprintf(why, cap, "alias %s names no memory", esk_json_get_str(r, "name", "?"));
                    return ES_ESCHEMA;
                }
                continue;
            }
            if (kind == ESK_MEM_EXTERNAL) {
                st = add_store(m, r, size);
                if (st != ES_OK) {
                    snprintf(why, cap, "memory %s cannot be added", esk_json_get_str(r, "name", "?"));
                    return st;
                }
                continue;
            }
            st = esk_bus_add_region(m, base, size, kind);
            if (st != ES_OK) {
                snprintf(why, cap, "memory region %s cannot be added", esk_json_get_str(r, "name", "?"));
                return st;
            }
            if (esk_json_get(r, "erased"))
                (void)u32_of(esk_json_get(r, "erased"), &erased);
            m->bus.rg[m->bus.nrg - 1].erased = erased;
            esk_bus_erase(&m->bus.rg[m->bus.nrg - 1]);
            st = contents(m, &m->bus.rg[m->bus.nrg - 1], esk_json_get(r, "contents"), size, why, cap);
            if (st != ES_OK)
                return st;
        }
    for (i = 0; bb && i < esk_json_len(bb); i++) {
        const esk_json *a = esk_json_at(bb, i);
        u32 base = 0, size = 0, target = 0;
        if (u32_of(esk_json_get(a, "base"), &base) || u32_of(esk_json_get(a, "size"), &size) ||
            u32_of(esk_json_get(a, "target"), &target) ||
            esk_bus_add_alias(&m->bus, base, size, target) != ES_OK) {
            snprintf(why, cap, "bit-band alias %zu is not usable", i);
            return ES_ESCHEMA;
        }
    }
    if (esk_str_eq(esk_json_get_str(core, "backend", ""), "cortex-m")) {
        st = esk_cortexm_create_smp(m, esk_json_get_str(core, "model", ""),
                                    (int)esk_json_get_int(core, "priority_bits", 4),
                                    (int)esk_json_get_int(core, "cores", 1), &cpu);
        if (st == ES_OK && esk_json_get_bool(core, "mpu", 0))
            st = esk_cortexm_set_mpu(m, 8); /* every Cortex-M0+, M3 and M4 MPU has eight regions */
        if (st == ES_OK && esk_json_get(core, "systick")) {
            const esk_json *stk = esk_json_get(core, "systick");
            u32 calib = 0;
            (void)u32_of(esk_json_get(stk, "calib"), &calib);
            st = esk_cortexm_set_systick(m, (u32)esk_json_get_int(stk, "ref_div", 0), calib);
        }
    } else if (esk_str_eq(esk_json_get_str(core, "backend", ""), "avr")) {
        st = esk_avr_create(m, esk_json_get_str(core, "model", ""), &cpu);
    } else if (esk_str_eq(esk_json_get_str(core, "backend", ""), "riscv")) {
        u32 reset_pc = 0;
        (void)u32_of(esk_json_get(core, "reset_pc"), &reset_pc);
        st = esk_riscv_create(m, esk_json_get_str(core, "model", ""), reset_pc, &cpu);
        if (st == ES_OK && esk_json_get(core, "pmp_entries"))
            esk_riscv_pmp_entries(cpu, (int)esk_json_get_int(core, "pmp_entries", 16));
    } else if (esk_str_eq(esk_json_get_str(core, "backend", ""), "mips")) {
        st = esk_mips_create(m, esk_json_get_str(core, "model", ""), &cpu);
    } else if (esk_str_eq(esk_json_get_str(core, "backend", ""), "pic24")) {
        u32 prog_end = 0, ram_base = 0;
        (void)u32_of(esk_json_get(core, "prog_end"), &prog_end);
        (void)u32_of(esk_json_get(core, "ram_base"), &ram_base);
        st = esk_pic24_create(m, esk_json_get_str(core, "model", ""), prog_end, (u16)ram_base, &cpu);
    } else {
        snprintf(why, cap, "the %s core backend is not available", esk_json_get_str(core, "backend", "?"));
        return ES_EUNSUPPORTED;
    }
    if (st != ES_OK) {
        snprintf(why, cap, "core %s cannot be created", esk_json_get_str(core, "model", "?"));
        return st;
    }
    m->reset_hz = (u32)esk_json_get_int(clocks, "reset_core_hz", 8000000);
    m->hse_hz = hse_hz;
    {
        const esk_json *io = esk_json_get(mcu, "io");
        double dflt = esk_str_eq(esk_json_get_str(io, "family", ""), "cmos_5v") ? 5.0 : 3.3;
        m->vdd = esk_json_get_num(esk_json_get(io, "electrical"), "vdd", dflt);
    }
    if (regs) {
        int rules = esk_rf_rules(esk_json_get(esk_json_get(mcu, "register_file"), "bus_rules"));
        if (rules < 0) {
            snprintf(why, cap, "register_file.bus_rules names a rule there is not");
            return ES_ESCHEMA;
        }
        st = esk_rf_create(m, regs, rules, why, cap);
        if (st != ES_OK)
            return st;
        st = esk_rf_alternates(m, esk_json_get(esk_json_get(mcu, "io"), "alternate"), why, cap);
        if (st == ES_OK)
            st = esk_rf_dma_requests(m, esk_json_get(mcu, "dma_requests"), why, cap);
        if (st == ES_OK)
            st = esk_rf_analog(m, esk_json_get(esk_json_get(mcu, "io"), "analog"), why, cap);
        if (st != ES_OK)
            return st;
        st = esk_rf_bind(m, esk_json_get(mcu, "peripherals"), missing, sizeof missing);
        if (st != ES_OK) {
            snprintf(why, cap, "peripheral models cannot be bound");
            return st;
        }
        if (missing[0])
            esk_rf_diag(m, ES_SEV_INFO, "mcu.model_missing",
                        "no behavioural model yet for %s: those peripherals are plain registers", missing);
        /* the STM32 and ESP32-C3 models report in next_event every
         * interrupt flag, pin edge, trigger and DMA request they make:
         * their clocks run lazily */
        if (esk_str_eq(esk_json_get_str(mcu, "vendor", ""), "st") ||
            esk_str_eq(esk_json_get_str(mcu, "vendor", ""), "espressif"))
            esk_mach_lazy(m);
    }
    return m->oom ? ES_ENOMEM : ES_OK;
}
