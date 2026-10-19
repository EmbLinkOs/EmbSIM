/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* build.c -- a machine from an MCU description (SPEC-006 §4, SPEC-009 §2):
 * its memory regions and aliases, bit-band aliases, the core, the register
 * file from the register description, and the peripheral models bound by
 * the description's globs, in its order (the RCC first: the others' clock
 * gates are its registers). */
#include <stdio.h>
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
    return ESK_MEM_RAM;
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
                if (u32_of(esk_json_get(r, "target"), &target) ||
                    esk_bus_add_mirror(&m->bus, base, target) != ES_OK) {
                    snprintf(why, cap, "alias %s names no memory", esk_json_get_str(r, "name", "?"));
                    return ES_ESCHEMA;
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
    }
    return m->oom ? ES_ENOMEM : ES_OK;
}
