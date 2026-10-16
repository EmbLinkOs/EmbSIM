/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* mips.c -- the MIPS32 core: Release 2, little-endian, as a PIC32MX's M4K
 * runs it (`pic32mx`) and as QEMU's malta runs a 24Kc (`24kc`).
 *
 * The instruction set is the whole MIPS32r2 integer set: the branches and
 * jumps with their delay slots (the likely forms skipping theirs when not
 * taken), HI and LO's multiplies and divides, mul, madd and msub, clz and
 * clo, ext and ins, seb, seh and wsbh, rotr, the traps, ll and sc, lwl,
 * lwr, swl and swr, rdhwr; coprocessor 0's mfc0 and mtc0, di and ei,
 * rdpgpr and wrpgpr, eret, deret, wait and the TLB's instructions; cache,
 * pref, sync and synci as no-ops. MIPS16e (mips16.c) is fetched here, a
 * halfword at a time, while the ISA mode is set: bit 0 of a jump's
 * target, jalx's switch. An encoding no MIPS32r2 core has is a Reserved
 * Instruction exception, as on the part; so are the FPU's (Coprocessor
 * Unusable), the DSP ASE's and microMIPS, which neither core has.
 *
 * Exceptions are the privileged architecture's: EPC and Cause.BD for an
 * instruction in a delay slot, Status.EXL and ERL, the boot vectors while
 * Status.BEV is set and EBase's after, the TLB refill vector, and
 * BadVAddr. Interrupts come in compatibility mode (Cause.IP and Status.IM;
 * the timer on IntCtl.IPTI), vectored mode (IntCtl.VS), or -- on a PIC32
 * -- EIC mode, from the EVIC (mips.h's struct mips_eic): the requested
 * level against Status.IPL, the handler at EBase + 0x200 plus the vector's
 * spacing, and the shadow register set it names. Shadow sets follow
 * SRSCtl: an exception or interrupt saves CSS in PSS and switches, eret
 * switches back, and rdpgpr and wrpgpr reach the previous set. Count
 * counts every other cycle; Compare's match sets Cause.TI, the core
 * timer's interrupt.
 *
 * WAIT, and an unconditional branch to itself whose delay slot does
 * nothing, put the machine to sleep until an interrupt the core would
 * take; the runtime fast-forwards to the devices' next event (ADR-019).
 * sdbbp enters debug mode, which needs a probe: with none it ends the run
 * as a lockup, at the breakpoint -- what XC32's and EmbCC's startups use
 * it for (an exception nobody handles).
 *
 * Memory: kseg0 and kseg1 reach the bus's low 512 MiB unmapped; on the
 * M4K (fixed mapping) kseg2 and kseg3 are unmapped too and kuseg is at
 * 0x40000000 up; on the 24Kc they go through the TLB (kuseg unmapped while
 * Status.ERL is set, as after reset). An access to nothing is a bus error
 * exception. Loads and stores must be aligned, but for lwl, lwr, swl and
 * swr. sc succeeds when ll's address still holds the value ll read (QEMU's
 * rule), and eret clears the link.
 *
 * Provenance: mips.h. */
#include <stdio.h>
#include <string.h>

#include "mcu/mips/cost.h"
#include "mcu/mips/mips.h"

typedef int16_t s16;

/* ---- exceptions ------------------------------------------------------ */

static void raise_exc(struct mips_state *ms, u32 code)
{
    if (ms->exc)
        return;
    ms->exc = 1;
    ms->exc_code = code;
    ms->exc_bad_set = 0;
    ms->exc_refill = 0;
    ms->exc_ce = 0;
}

static void raise_addr(struct mips_state *ms, u32 code, u32 va)
{
    if (ms->exc)
        return;
    raise_exc(ms, code);
    ms->exc_bad = va;
    ms->exc_bad_set = 1;
}

static void raise_cpu(struct mips_state *ms, u32 unit)
{
    if (ms->exc)
        return;
    raise_exc(ms, EXC_CPU);
    ms->exc_ce = unit;
}


static void reserved(struct mips_state *ms)
{
    raise_exc(ms, EXC_RI);
}

static int kernel(struct mips_state *ms)
{
    return !(ms->status & ST_UM) || (ms->status & (ST_EXL | ST_ERL)) ||
           (ms->debug & DBG_DM);
}

/* the register set an exception or interrupt switches to: Release 2's
 * shadow sets, when the core has them and the boot vectors are not in
 * use */
static void switch_set(struct mips_state *ms, int css)
{
    u32 cur, hss = (ms->srsctl >> 26) & 15;
    if (!hss || (ms->status & ST_BEV))
        return;
    if ((u32)css > hss)
        css = 0;
    cur = ms->srsctl & 15;
    ms->srsctl = (ms->srsctl & ~(15u << 6 | 15u)) | cur << 6 | (u32)css;
    ms->r = ms->gpr[css];
}

/* Exception entry: EPC (and Cause.BD) for the instruction at pc unless
 * one is in progress (Status.EXL), Cause's code, the vector. `intoff`
 * is an interrupt's offset from the vector base, or 0 for an exception.
 * EPC's bit 0 is the ISA mode (1: MIPS16e); the handler runs in MIPS32. */
static void enter(struct mips_state *ms, u32 code, u32 intoff, int newset)
{
    u32 st = ms->status, off, base;
    u32 ret = ms->slot ? ms->bpc : ms->pc | (u32)ms->isa;
    if (!(st & ST_EXL)) {
        ms->epc = ret;
        ms->cause = ms->slot ? ms->cause | CA_BD : ms->cause & ~CA_BD;
        off = code == EXC_INT ? intoff : ms->exc_refill ? 0 : 0x180;
        switch_set(ms, newset);
    } else {
        off = 0x180;
    }
    /* CE is the unit of a Coprocessor Unusable exception, and as it was
     * for the others (the architecture leaves it unpredictable; QEMU's
     * keeps it) */
    ms->cause = (ms->cause & ~0x7cu) | code << 2;
    if (code == EXC_CPU)
        ms->cause = (ms->cause & ~(3u << 28)) | ms->exc_ce << 28;
    if (ms->exc_bad_set)
        ms->badvaddr = ms->exc_bad;
    ms->status |= ST_EXL;
    base = (st & ST_BEV) ? 0xbfc00200u : (ms->ebase & 0xfffff000u);
    ms->pc = base + off;
    ms->npc = ms->pc + 4;
    ms->slot = 0;
    ms->isa = 0;
}

/* ---- the core timer and the interrupt lines ------------------------------ */

static u32 count_now(struct mips_state *ms)
{
    if (ms->cause & CA_DC)
        return ms->count_base;
    return ms->count_base + (u32)((ms->sim->cycles - ms->count_at) >> 1);
}

/* when Count next equals Compare: it counts every other cycle, and a
 * Compare equal to Count now is a whole turn away */
static void timer_plan(struct mips_state *ms)
{
    struct esk_mach *s = ms->sim;
    u64 k, d;
    s->bus.touched = 1;
    if (ms->cause & CA_DC) {
        ms->ti_at = ~0ull;
        return;
    }
    k = (s->cycles - ms->count_at) >> 1;
    d = (u32)(ms->compare - count_now(ms));
    if (!d)
        d = 1ull << 32;
    ms->ti_at = ms->count_at + 2 * (k + d);
}

/* what the core requests of an EIC: the timer and the two software
 * interrupts, told when they change */
static void core_lines(struct mips_state *ms)
{
    u32 l = ((ms->cause & CA_TI) ? 1u : 0) | ((ms->cause >> 7) & 6u);
    if (ms->eic && l != ms->core_lines) {
        ms->core_lines = l;
        ms->eic->core_irq(ms->eic->ctx, l & 1, (l >> 1) & 1, (l >> 2) & 1);
    }
    ms->core_lines = l;
}

static void timer_fire(struct mips_state *ms)
{
    ms->cause |= CA_TI;
    ms->ti_at += 2ull << 32;
    core_lines(ms);
    ms->sim->bus.touched = 1;
}

/* the IP bits of compatibility and vectored modes: IP1-IP0 written by
 * the program, IP2-IP7 the hardware's lines, the timer on IntCtl.IPTI */
static u32 ip_bits(struct mips_state *ms)
{
    u32 ip = (ms->cause >> 8) & 3;
    ip |= (ms->ip_hw & 0x3f) << 2;
    if (ms->cause & CA_TI)
        ip |= 1u << ((ms->intctl >> 29) & 7);
    return ip;
}

static u32 ipl(struct mips_state *ms)
{
    return ((ms->status >> 10) & 0x7f) | ((ms->status >> 11) & 0x80);
}

static int interrupts_on(struct mips_state *ms)
{
    return (ms->status & ST_IE) && !(ms->status & (ST_EXL | ST_ERL)) &&
           !(ms->debug & DBG_DM);
}

/* an interrupt the core would take now */
static int interrupt_ready(struct mips_state *ms)
{
    if (!interrupts_on(ms))
        return 0;
    if (ms->eic)
        return (u32)ms->eic->ripl > ipl(ms);
    return (ip_bits(ms) & (ms->status >> 8) & 0xff) != 0;
}

static void take_interrupt(struct mips_state *ms)
{
    u32 off = 0x200, vs = (ms->intctl >> 5) & 31;
    int set = (int)((ms->srsctl >> 12) & 15);
    if (!(ms->cause & CA_IV)) {
        off = 0x180;
    } else if (ms->eic) {
        ms->cause = (ms->cause & ~(0xffu << 10)) | (u32)ms->eic->ripl << 10;
        if (!(ms->status & ST_BEV) && vs)
            off = ms->eic->spaced ? 0x200 + (u32)ms->eic->vector * (vs << 5)
                                  : ms->eic->offset;
        set = ms->eic->srs;
        ms->srsctl = (ms->srsctl & ~(15u << 18)) | (u32)(set & 15) << 18;
    } else if (vs && !(ms->status & ST_BEV)) {
        /* vectored: the highest IP pending and enabled */
        u32 p = ip_bits(ms) & (ms->status >> 8) & 0xff;
        int v = 7;
        while (v > 0 && !(p >> v & 1))
            v--;
        off = 0x200 + (u32)v * (vs << 5);
        set = (int)((ms->srsmap >> (4 * v)) & 15);
    }
    if (ms->eic && ms->eic->taken)
        ms->eic->taken(ms->eic->ctx);
    ms->exc_bad_set = 0;
    ms->exc_refill = 0;
    enter(ms, EXC_INT, off, set);
}

void esk_mips_eic_attach(struct esk_mach *m, struct mips_eic *e)
{
    if (esk_mips_is(m->cpu))
        ((struct mips_state *)m->cpu)->eic = e;
}

/* the controller's request changed: a sleeping core it now interrupts
 * wakes, as a raised line wakes the others */
void esk_mips_eic_changed(struct esk_mach *m)
{
    m->bus.touched = 1;
    if (m->asleep && esk_mips_is(m->cpu) && interrupt_ready((struct mips_state *)m->cpu))
        m->asleep = 0;
}

/* the core timer, as a device: when its interrupt comes, for a wait */
static int ctimer_next(void *ctx, u32 *cycles)
{
    struct mips_state *k = (struct mips_state *)ctx;
    struct esk_mach *s = k->sim;
    u64 d;
    if ((k->cause & (CA_DC | CA_TI)) || k->ti_at == ~0ull)
        return 0;
    if (k->eic ? !(k->eic->timer_on && k->eic->timer_on(k->eic->ctx))
               : !((k->status >> 8) & (1u << ((k->intctl >> 29) & 7))))
        return 0;
    d = k->ti_at > s->cycles ? k->ti_at - s->cycles : 0;
    *cycles = d > 0xffffffffu ? 0xffffffffu : (u32)d;
    return 1;
}

static const struct esk_dev_ops ctimer_ops = { "mips-count", 0, 0, 0, 0, ctimer_next };

/* ---- memory ---------------------------------------------------------- */

u32 esk_mips_phys(u32 va)
{
    return va >= 0x80000000u && va < 0xc0000000u ? va & 0x1fffffffu : va;
}

/* the TLB entry mapping va, or -1 */
static int tlb_find(struct mips_state *ms, u32 va, u32 asid)
{
    for (int i = 0; i < MIPS_TLB; i++) {
        const struct mips_tlbe *t = &ms->tlb[i];
        u32 m = ~(t->mask | 0x1fffu);
        if (!((va ^ t->hi) & m) && (t->g || (t->hi & 0xff) == asid))
            return i;
    }
    return -1;
}

/* 0 and the physical address, or -1 with the exception raised */
static int tlb_xlate(struct mips_state *ms, u32 va, int acc, u32 *pa)
{
    u32 code = acc == 1 ? EXC_TLBS : EXC_TLBL;
    int i = tlb_find(ms, va, ms->entryhi & 0xff);
    if (i < 0) {
        raise_addr(ms, code, va);
        ms->exc_refill = !(ms->status & ST_EXL);
    } else {
        const struct mips_tlbe *t = &ms->tlb[i];
        u32 half = ((t->mask | 0x1fffu) + 1) >> 1;
        u32 lo = (va & half) ? t->lo1 : t->lo0;
        if (!(lo & 2)) {
            raise_addr(ms, code, va);
        } else if (acc == 1 && !(lo & 4)) {
            raise_addr(ms, EXC_MOD, va);
        } else {
            *pa = ((lo >> 6) << 12 & ~(half - 1)) | (va & (half - 1));
            return 0;
        }
    }
    ms->context = (ms->context & 0xff800000u) | (va >> 9 & 0x7ffff0u);
    ms->entryhi = (va & 0xffffe000u) | (ms->entryhi & 0xff);
    return -1;
}

/* the physical address of an access (acc: 0 a load, 1 a store, 2 a
 * fetch), or -1 with its exception raised */
static int xlate(struct mips_state *ms, u32 va, int acc, u32 *pa)
{
    if (va >= 0x80000000u) {
        if (!kernel(ms)) {
            raise_addr(ms, acc == 1 ? EXC_ADES : EXC_ADEL, va);
            return -1;
        }
        if (va < 0xc0000000u) {
            *pa = va & 0x1fffffffu;
            return 0;
        }
        if (ms->fmt) {              /* kseg2 and kseg3, unmapped */
            *pa = va;
            return 0;
        }
        return tlb_xlate(ms, va, acc, pa);
    }
    if (ms->status & ST_ERL) {
        *pa = va;
        return 0;
    }
    if (ms->fmt) {                  /* useg at 0x40000000 up */
        *pa = va + 0x40000000u;
        return 0;
    }
    return tlb_xlate(ms, va, acc, pa);
}

static int watched(struct mips_state *ms, u32 va, int n, int write)
{
    struct esk_bus *b = &ms->sim->bus;
    if (b->nwatch && esk_bus_watch_check(b, va, n, write)) {
        ms->watch_stop = 1;
        ms->exc = 1;
        return 1;
    }
    return 0;
}

/* n aligned bytes at physical pa: 0, or -1 for nothing there */
static int pread(struct mips_state *ms, u32 pa, int n, u32 *v)
{
    struct esk_region *r = esk_bus_region(&ms->sim->bus, pa, (u32)n);
    if (r) {
        *v = esk_mem_rd_le(r->mem + (pa - r->base), n);
        return 0;
    }
    return esk_bus_read(&ms->sim->bus, pa, n, v);
}

/* n bytes, any of 1 to 4, from pa up (swl and swr write 3, and swr at
 * any address): a device gets its aligned accesses, the rest a byte at
 * a time */
static int pwrite(struct mips_state *ms, u32 pa, int n, u32 v)
{
    struct esk_bus *b = &ms->sim->bus;
    struct esk_region *r = esk_bus_region(b, pa, (u32)n);
    if (r) {
        if (!r->rom)
            for (int i = 0; i < n; i++)
                r->mem[pa - r->base + (u32)i] = (u8)(v >> (8 * i));
        return 0;
    }
    if (n == 1 || (n == 2 && !(pa & 1)) || (n == 4 && !(pa & 3)))
        return esk_bus_write(b, pa, n, n == 4 ? v : v & ((1u << (8 * n)) - 1));
    for (int i = 0; i < n; i++)
        if (esk_bus_write(b, pa + (u32)i, 1, (v >> (8 * i)) & 0xff))
            return -1;
    return 0;
}

static u32 load(struct mips_state *ms, u32 va, int n)
{
    u32 pa = 0, v = 0;
    if (ms->exc)
        return 0;
    if (va & (u32)(n - 1)) {
        raise_addr(ms, EXC_ADEL, va);
        return 0;
    }
    if (xlate(ms, va, 0, &pa) || watched(ms, va, n, 0))
        return 0;
    if (pread(ms, pa, n, &v)) {
        raise_exc(ms, EXC_DBE);
        return 0;
    }
    return v;
}

static void store(struct mips_state *ms, u32 va, int n, u32 v)
{
    u32 pa = 0;
    if (ms->exc)
        return;
    if (va & (u32)(n - 1)) {
        raise_addr(ms, EXC_ADES, va);
        return;
    }
    if (xlate(ms, va, 1, &pa) || watched(ms, va, n, 1))
        return;
    if (pwrite(ms, pa, n, v))
        raise_exc(ms, EXC_DBE);
}

u32 mips_load(struct mips_state *ms, u32 va, int n)
{
    return load(ms, va, n);
}

void mips_store(struct mips_state *ms, u32 va, int n, u32 v)
{
    store(ms, va, n, v);
}

void mips_raise(struct mips_state *ms, u32 code)
{
    raise_exc(ms, code);
}

/* ---- the registers ----------------------------------------------------- */

ES_INLINE u32 R(struct mips_state *ms, u32 n)
{
    return ms->r[n];
}

ES_INLINE void W(struct mips_state *ms, u32 n, u32 v)
{
    if (n)
        ms->r[n] = v;
}

/* ---- coprocessor 0 ------------------------------------------------------- */

static u32 status_mask(struct mips_state *ms)
{
    u32 m = ST_CU0 | (1u << 27) | ST_BEV | ST_SR | ST_NMI | ST_UM | ST_ERL |
            ST_EXL | ST_IE | 0x0005ff00u;
    ES_UNUSED(ms);
    return m;
}

static u32 cp0_read(struct mips_state *ms, u32 rd, u32 sel)
{
    switch (rd << 3 | sel) {
    case 0 << 3: return ms->index;
    case 1 << 3: {                      /* Random: from Wired to the last */
        u32 w = ms->wired & 15;
        return w >= MIPS_TLB - 1 ? MIPS_TLB - 1
               : w + (u32)(ms->sim->insns % (MIPS_TLB - w));
    }
    case 2 << 3: return ms->entrylo0;
    case 3 << 3: return ms->entrylo1;
    case 4 << 3: return ms->context;
    case 4 << 3 | 2: return ms->userlocal;
    case 5 << 3: return ms->pagemask;
    case 6 << 3: return ms->wired;
    case 7 << 3: return ms->hwrena;
    case 8 << 3: return ms->badvaddr;
    case 9 << 3: return count_now(ms);
    case 10 << 3: return ms->entryhi;
    case 11 << 3: return ms->compare;
    case 12 << 3: return ms->status;
    case 12 << 3 | 1: return ms->intctl;
    case 12 << 3 | 2: return ms->srsctl;
    case 12 << 3 | 3: return ms->srsmap;
    case 13 << 3:
        if (ms->eic)
            return ms->cause;
        return (ms->cause & ~0xfc00u) | (ip_bits(ms) & 0xfc) << 8;
    case 14 << 3: return ms->epc;
    case 15 << 3: return ms->prid;
    case 15 << 3 | 1: return ms->ebase;
    case 16 << 3: return ms->config0;
    case 16 << 3 | 1: return ms->config1;
    case 16 << 3 | 2: return ms->config2;
    case 16 << 3 | 3: return ms->config3;
    case 16 << 3 | 5: return ms->config5;
    case 16 << 3 | 7: return ms->config7;
    case 17 << 3: return ms->lladdr >> 4;
    case 18 << 3: return ms->watchlo;
    case 19 << 3: return ms->watchhi;
    case 23 << 3: return ms->debug;
    case 24 << 3: return ms->depc;
    case 26 << 3: return ms->errctl;
    case 28 << 3: case 28 << 3 | 1: case 28 << 3 | 2: case 28 << 3 | 3:
        return ms->taglo[sel];
    case 30 << 3: return ms->errorepc;
    case 31 << 3: return ms->desave;
    }
    return 0;
}

static void cp0_write(struct mips_state *ms, u32 rd, u32 sel, u32 v)
{
    struct esk_mach *s = ms->sim;
    switch (rd << 3 | sel) {
    case 0 << 3: ms->index = (ms->index & 0x80000000u) | (v & 15); return;
    case 2 << 3: ms->entrylo0 = v & 0x3fffffffu; return;
    case 3 << 3: ms->entrylo1 = v & 0x3fffffffu; return;
    case 4 << 3: ms->context = (v & 0xff800000u) | (ms->context & 0x7ffff0u); return;
    case 4 << 3 | 2: ms->userlocal = v; return;
    case 5 << 3: ms->pagemask = v & 0x1fffe000u; return;
    case 6 << 3: ms->wired = v & 15; return;
    case 7 << 3: ms->hwrena = v & 0x2000000fu; return;
    case 9 << 3:
        ms->count_base = v;
        ms->count_at = s->cycles;
        timer_plan(ms);
        return;
    case 10 << 3: ms->entryhi = v & 0xffffe0ffu; return;
    case 11 << 3:
        ms->compare = v;
        ms->cause &= ~CA_TI;
        timer_plan(ms);
        core_lines(ms);
        return;
    case 12 << 3: {
        u32 m = status_mask(ms);
        ms->status = (ms->status & ~m) | (v & m);
        s->bus.touched = 1;
        return;
    }
    case 12 << 3 | 1:
        ms->intctl = (ms->intctl & ~(31u << 5)) | (v & (31u << 5));
        return;
    case 12 << 3 | 2: {
        u32 hss = (ms->srsctl >> 26) & 15;
        u32 ess = (v >> 12) & 15, pss = (v >> 6) & 15;
        ms->srsctl = (ms->srsctl & ~(15u << 12 | 15u << 6)) |
                     (ess <= hss ? ess : 0) << 12 | (pss <= hss ? pss : 0) << 6;
        return;
    }
    case 12 << 3 | 3: ms->srsmap = v; return;
    case 13 << 3: {
        u32 m = CA_IV | (1u << 22) | 0x300u | CA_DC;
        u32 old = ms->cause;
        if ((old ^ v) & CA_DC) {
            if (v & CA_DC) {                /* Count stops where it is */
                ms->count_base = count_now(ms);
                ms->cause |= CA_DC;
            } else {
                ms->cause &= ~CA_DC;
                ms->count_at = s->cycles;
            }
            timer_plan(ms);
        }
        ms->cause = (ms->cause & ~m) | (v & m);
        core_lines(ms);
        s->bus.touched = 1;
        return;
    }
    case 14 << 3: ms->epc = v; return;
    case 15 << 3 | 1:
        ms->ebase = (ms->ebase & ~0x3ffff000u) | (v & 0x3ffff000u);
        return;
    case 16 << 3: ms->config0 = (ms->config0 & ~7u) | (v & 7); return;
    case 18 << 3: ms->watchlo = v; return;
    case 19 << 3: ms->watchhi = v & ~7u; return;
    case 23 << 3: ms->debug = (ms->debug & DBG_DM) | (v & ~DBG_DM); return;
    case 24 << 3: ms->depc = v; return;
    case 26 << 3: ms->errctl = v; return;
    case 28 << 3: case 28 << 3 | 1: case 28 << 3 | 2: case 28 << 3 | 3:
        ms->taglo[sel] = v;
        return;
    case 30 << 3: ms->errorepc = v; return;
    case 31 << 3: ms->desave = v; return;
    }
}

static void tlb_write(struct mips_state *ms, u32 i)
{
    struct mips_tlbe *t = &ms->tlb[i % MIPS_TLB];
    t->mask = ms->pagemask;
    t->hi = ms->entryhi & ~(ms->pagemask | 0x1f00u);
    t->lo0 = ms->entrylo0 & ~1u;
    t->lo1 = ms->entrylo1 & ~1u;
    t->g = ms->entrylo0 & ms->entrylo1 & 1;
}

static void eret(struct mips_state *ms)
{
    u32 to, hss;
    if (ms->status & ST_ERL) {
        to = ms->errorepc;
        ms->status &= ~ST_ERL;
    } else {
        to = ms->epc;
        ms->status &= ~ST_EXL;
        hss = (ms->srsctl >> 26) & 15;
        if (hss && !(ms->status & ST_BEV)) {
            u32 pss = (ms->srsctl >> 6) & 15;
            ms->srsctl = (ms->srsctl & ~15u) | pss;
            ms->r = ms->gpr[pss];
        }
    }
    ms->llbit = 0;
    ms->nx = to;
    ms->nnx = to + 4;
    ms->nslot = 0;
    ms->sim->bus.touched = 1;
}

static void cop0(struct mips_state *ms, u32 i)
{
    u32 rs = (i >> 21) & 31, rt = (i >> 16) & 31, rd = (i >> 11) & 31;
    if (!kernel(ms) && !(ms->status & ST_CU0)) {
        raise_cpu(ms, 0);
        return;
    }
    switch (rs) {
    case 0x00:                                  /* MFC0 */
        W(ms, rt, cp0_read(ms, rd, i & 7));
        return;
    case 0x04:                                  /* MTC0 */
        cp0_write(ms, rd, i & 7, R(ms, rt));
        return;
    case 0x0a:                                  /* RDPGPR */
        W(ms, rd, ms->gpr[(ms->srsctl >> 6) & 15][rt]);
        return;
    case 0x0b:                                  /* DI, EI */
        if (rd != 12 || (i & 0x7df) != 0) {
            reserved(ms);
            return;
        }
        W(ms, rt, ms->status);
        if (i & 0x20)
            ms->status |= ST_IE;
        else
            ms->status &= ~ST_IE;
        ms->sim->bus.touched = 1;
        return;
    case 0x0e:                                  /* WRPGPR */
        if (rd)
            ms->gpr[(ms->srsctl >> 6) & 15][rd] = R(ms, rt);
        return;
    }
    if (!(rs & 0x10)) {
        reserved(ms);
        return;
    }
    switch (i & 0x3f) {
    case 0x01: {                                /* TLBR */
        const struct mips_tlbe *t = &ms->tlb[ms->index & 15];
        ms->pagemask = t->mask;
        ms->entryhi = t->hi;
        ms->entrylo0 = t->lo0 | (u32)t->g;
        ms->entrylo1 = t->lo1 | (u32)t->g;
        return;
    }
    case 0x02: tlb_write(ms, ms->index & 15); return;  /* TLBWI */
    case 0x06: tlb_write(ms, cp0_read(ms, 1, 0)); return;  /* TLBWR */
    case 0x08: {                                /* TLBP */
        int k = tlb_find(ms, ms->entryhi & ~0x1fffu, ms->entryhi & 0xff);
        ms->index = k < 0 ? 0x80000000u : (u32)k;
        return;
    }
    case 0x18:                                  /* ERET */
        eret(ms);
        return;
    case 0x1f:                                  /* DERET */
        if (!(ms->debug & DBG_DM)) {
            reserved(ms);
            return;
        }
        ms->debug &= ~DBG_DM;
        ms->nx = ms->depc;
        ms->nnx = ms->depc + 4;
        ms->nslot = 0;
        return;
    case 0x20:                                  /* WAIT */
        /* until an interrupt the core would take; it returns past the wait */
        if (!interrupt_ready(ms))
            ms->sim->asleep = 1;
        return;
    }
    reserved(ms);
}

/* the DSP ASE's instructions and accumulators: neither core has it */
static int dsp_ok(struct mips_state *ms)
{
    reserved(ms);
    return 0;
}

/* ---- the instructions ---------------------------------------------------- */

static void branch(struct mips_state *ms, int taken, u32 target)
{
    if (taken)
        ms->nnx = target;
    ms->nslot = 1;
    ms->nbpc = ms->pc;
    ms->nds = 0;
    ms->nbtaken = taken;
}

static void branch_likely(struct mips_state *ms, int taken, u32 target)
{
    if (taken) {
        ms->nnx = target;
        ms->nslot = 1;
        ms->nbpc = ms->pc;
        ms->nds = 0;
        ms->nbtaken = 1;
    } else {
        ms->nx = ms->pc + 8;
        ms->nnx = ms->pc + 12;
    }
}

/* the word at a, without side effects, or ~0 */
static u32 peek(struct mips_state *ms, u32 a)
{
    u32 v = 0;
    if (esk_bus_debug_read(&ms->sim->bus, esk_mips_phys(a), 4, &v))
        return ~0u;
    return v;
}

/* An unconditional branch to itself (`target` with the ISA bit): if its
 * delay slot does nothing, the core waits, as WAIT does, for an interrupt
 * it would take. */
void mips_self_loop(struct mips_state *ms, u32 target, int slot_nop)
{
    struct esk_mach *s = ms->sim;
    if (target != (ms->pc | (u32)ms->isa) || ms->slot || s->state != ESK_MACH_RUN || !slot_nop)
        return;
    if (!interrupt_ready(ms))
        s->asleep = 1;
}

/* a MIPS32 branch's: a nop, ssnop or ehb in the slot */
static void self_loop(struct mips_state *ms, u32 target)
{
    u32 d = peek(ms, ms->pc + 4);
    mips_self_loop(ms, target, d == 0 || d == 0x40 || d == 0xc0);
}

static void trap_if(struct mips_state *ms, int c)
{
    if (c)
        raise_exc(ms, EXC_TR);
}

static u32 acc_of(u32 i, int at)
{
    return (i >> at) & 3;
}

static void hilo_set(struct mips_state *ms, u32 ac, u64 v)
{
    ms->lo[ac] = (u32)v;
    ms->hi[ac] = (u32)(v >> 32);
}

static u64 hilo(struct mips_state *ms, u32 ac)
{
    return (u64)ms->hi[ac] << 32 | ms->lo[ac];
}

static void special(struct mips_state *ms, u32 i)
{
    u32 rs = (i >> 21) & 31, rt = (i >> 16) & 31, rd = (i >> 11) & 31;
    u32 sa = (i >> 6) & 31, a = R(ms, rs), b = R(ms, rt);
    switch (i & 0x3f) {
    case 0x00: W(ms, rd, b << sa); return;                          /* SLL */
    case 0x01:                                                  /* MOVF, MOVT */
        raise_cpu(ms, 1);                                       /* no FPU */
        return;
    case 0x02:                                                  /* SRL, ROTR */
        if (rs & 1)
            W(ms, rd, sa ? b >> sa | b << (32 - sa) : b);
        else
            W(ms, rd, b >> sa);
        return;
    case 0x03: W(ms, rd, (u32)((s32)b >> sa)); return;              /* SRA */
    case 0x04: W(ms, rd, b << (a & 31)); return;                    /* SLLV */
    case 0x06: {                                                /* SRLV, ROTRV */
        u32 n = a & 31;
        if (sa & 1)
            W(ms, rd, n ? b >> n | b << (32 - n) : b);
        else
            W(ms, rd, b >> n);
        return;
    }
    case 0x07: W(ms, rd, (u32)((s32)b >> (a & 31))); return;        /* SRAV */
    case 0x08:                                                  /* JR */
        branch(ms, 1, a);
        return;
    case 0x09:                                                  /* JALR */
        W(ms, rd, ms->pc + 8);
        branch(ms, 1, a);
        return;
    case 0x0a: if (!b) W(ms, rd, a); return;                        /* MOVZ */
    case 0x0b: if (b) W(ms, rd, a); return;                         /* MOVN */
    case 0x0c: raise_exc(ms, EXC_SYS); return;                      /* SYSCALL */
    case 0x0d: raise_exc(ms, EXC_BP); return;                       /* BREAK */
    case 0x0f: return;                                          /* SYNC */
    case 0x10: {                                                /* MFHI */
        u32 ac = acc_of(i, 21);
        if (ac && !dsp_ok(ms))
            return;
        W(ms, rd, ms->hi[ac]);
        return;
    }
    case 0x11: {                                                /* MTHI */
        u32 ac = acc_of(i, 11);
        if (ac && !dsp_ok(ms))
            return;
        ms->hi[ac] = a;
        return;
    }
    case 0x12: {                                                /* MFLO */
        u32 ac = acc_of(i, 21);
        if (ac && !dsp_ok(ms))
            return;
        W(ms, rd, ms->lo[ac]);
        return;
    }
    case 0x13: {                                                /* MTLO */
        u32 ac = acc_of(i, 11);
        if (ac && !dsp_ok(ms))
            return;
        ms->lo[ac] = a;
        return;
    }
    case 0x18: case 0x19: {                                     /* MULT, MULTU */
        u32 ac = acc_of(i, 11);
        if (ac && !dsp_ok(ms))
            return;
        hilo_set(ms, ac, (i & 1) ? (u64)a * b : (u64)((s64)(s32)a * (s32)b));
        return;
    }
    case 0x1a:                                                  /* DIV */
        /* by zero, and INT_MIN by -1: LO the dividend, HI 0, as QEMU
         * has it (the architecture leaves them unpredictable) */
        if (!b || (a == 0x80000000u && b == 0xffffffffu)) {
            ms->lo[0] = a;
            ms->hi[0] = 0;
        } else {
            ms->lo[0] = (u32)((s32)a / (s32)b);
            ms->hi[0] = (u32)((s32)a % (s32)b);
        }
        return;
    case 0x1b:                                                  /* DIVU */
        if (!b) {
            ms->lo[0] = a;
            ms->hi[0] = 0;
        } else {
            ms->lo[0] = a / b;
            ms->hi[0] = a % b;
        }
        return;
    case 0x20:                                                  /* ADD */
        if (((a + b) ^ a) & ((a + b) ^ b) & 0x80000000u)
            raise_exc(ms, EXC_OV);
        else
            W(ms, rd, a + b);
        return;
    case 0x21: W(ms, rd, a + b); return;                            /* ADDU */
    case 0x22:                                                  /* SUB */
        if ((a ^ b) & (a ^ (a - b)) & 0x80000000u)
            raise_exc(ms, EXC_OV);
        else
            W(ms, rd, a - b);
        return;
    case 0x23: W(ms, rd, a - b); return;                            /* SUBU */
    case 0x24: W(ms, rd, a & b); return;                            /* AND */
    case 0x25: W(ms, rd, a | b); return;                            /* OR */
    case 0x26: W(ms, rd, a ^ b); return;                            /* XOR */
    case 0x27: W(ms, rd, ~(a | b)); return;                         /* NOR */
    case 0x2a: W(ms, rd, (s32)a < (s32)b); return;                  /* SLT */
    case 0x2b: W(ms, rd, a < b); return;                            /* SLTU */
    case 0x30: trap_if(ms, (s32)a >= (s32)b); return;               /* TGE */
    case 0x31: trap_if(ms, a >= b); return;                         /* TGEU */
    case 0x32: trap_if(ms, (s32)a < (s32)b); return;                /* TLT */
    case 0x33: trap_if(ms, a < b); return;                          /* TLTU */
    case 0x34: trap_if(ms, a == b); return;                         /* TEQ */
    case 0x36: trap_if(ms, a != b); return;                         /* TNE */
    }
    reserved(ms);
}

static void regimm(struct mips_state *ms, u32 i)
{
    u32 rs = (i >> 21) & 31, rt = (i >> 16) & 31;
    s32 a = (s32)R(ms, rs);
    u32 imm = (u32)(s32)(s16)i, t = ms->pc + 4 + (imm << 2);
    switch (rt) {
    case 0x00: branch(ms, a < 0, t); return;                        /* BLTZ */
    case 0x01:                                                  /* BGEZ */
        if (!rs)
            self_loop(ms, t);
        branch(ms, a >= 0, t);
        return;
    case 0x02: branch_likely(ms, a < 0, t); return;                 /* BLTZL */
    case 0x03: branch_likely(ms, a >= 0, t); return;                /* BGEZL */
    case 0x08: trap_if(ms, a >= (s32)imm); return;                  /* TGEI */
    case 0x09: trap_if(ms, (u32)a >= imm); return;                  /* TGEIU */
    case 0x0a: trap_if(ms, a < (s32)imm); return;                   /* TLTI */
    case 0x0b: trap_if(ms, (u32)a < imm); return;                   /* TLTIU */
    case 0x0c: trap_if(ms, a == (s32)imm); return;                  /* TEQI */
    case 0x0e: trap_if(ms, a != (s32)imm); return;                  /* TNEI */
    case 0x10:                                                  /* BLTZAL */
        W(ms, 31, ms->pc + 8);
        branch(ms, a < 0, t);
        return;
    case 0x11:                                                  /* BGEZAL, BAL */
        W(ms, 31, ms->pc + 8);
        branch(ms, a >= 0, t);
        return;
    case 0x12:                                                  /* BLTZALL */
        W(ms, 31, ms->pc + 8);
        branch_likely(ms, a < 0, t);
        return;
    case 0x13:                                                  /* BGEZALL */
        W(ms, 31, ms->pc + 8);
        branch_likely(ms, a >= 0, t);
        return;
    case 0x1c:                                                  /* BPOSGE32 */
        dsp_ok(ms);
        return;
    case 0x1f: return;                                          /* SYNCI */
    }
    reserved(ms);
}

static void special2(struct mips_state *ms, u32 i)
{
    u32 rs = (i >> 21) & 31, rt = (i >> 16) & 31, rd = (i >> 11) & 31;
    u32 a = R(ms, rs), b = R(ms, rt), ac = acc_of(i, 11);
    switch (i & 0x3f) {
    case 0x00: case 0x01: case 0x04: case 0x05: {   /* MADD, MADDU, MSUB, MSUBU */
        u64 p;
        if (ac && !dsp_ok(ms))
            return;
        p = (i & 1) ? (u64)a * b : (u64)((s64)(s32)a * (s32)b);
        hilo_set(ms, ac, (i & 4) ? hilo(ms, ac) - p : hilo(ms, ac) + p);
        return;
    }
    case 0x02: W(ms, rd, a * b); return;                            /* MUL */
    case 0x20: case 0x21: {                                     /* CLZ, CLO */
        u32 v = (i & 1) ? ~a : a, n = 0;
        while (n < 32 && !(v >> (31 - n) & 1))
            n++;
        W(ms, rd, n);
        return;
    }
    case 0x3f:                                                  /* SDBBP */
        raise_exc(ms, EXC_BP);
        esk_mach_end(ms->sim, ESK_MACH_LOCKUP, "sdbbp at 0x%08x: a software breakpoint, "
                     "with no debugger to take it", ms->pc);
        return;
    }
    reserved(ms);
}

static void special3(struct mips_state *ms, u32 i)
{
    u32 rs = (i >> 21) & 31, rt = (i >> 16) & 31, rd = (i >> 11) & 31;
    u32 sa = (i >> 6) & 31, a = R(ms, rs), b = R(ms, rt);
    switch (i & 0x3f) {
    case 0x00: {                                                /* EXT */
        u32 size = rd + 1;
        u32 v = a >> sa;
        W(ms, rt, size >= 32 ? v : v & ((1u << size) - 1));
        return;
    }
    case 0x04: {                                                /* INS */
        u32 size, m;
        if (rd < sa) {
            reserved(ms);
            return;
        }
        size = rd - sa + 1;
        m = (size >= 32 ? ~0u : (1u << size) - 1) << sa;
        W(ms, rt, (b & ~m) | ((a << sa) & m));
        return;
    }
    case 0x0a:                                                  /* LWX, LHX, LBUX */
        dsp_ok(ms);
        return;
    case 0x20:                                                  /* BSHFL */
        switch (sa) {
        case 0x02:                                              /* WSBH */
            W(ms, rd, ((b & 0x00ff00ffu) << 8) | ((b >> 8) & 0x00ff00ffu));
            return;
        case 0x10: W(ms, rd, (u32)(s32)(signed char)b); return;     /* SEB */
        case 0x18: W(ms, rd, (u32)(s32)(s16)b); return;             /* SEH */
        }
        break;
    case 0x38:                                                  /* the DSP's */
    case 0x0c: case 0x10: case 0x11: case 0x12: case 0x13:
    case 0x18: case 0x30: case 0x31:
        dsp_ok(ms);
        return;
    case 0x3b:                                                  /* RDHWR */
        if (!kernel(ms) && !(ms->hwrena >> rd & 1)) {
            reserved(ms);
            return;
        }
        switch (rd) {
        case 0: W(ms, rt, ms->ebase & 0x3ff); return;               /* CPUNum */
        case 1: W(ms, rt, 16); return;                              /* SYNCI_Step */
        case 2: W(ms, rt, count_now(ms)); return;                     /* CC */
        case 3: W(ms, rt, 2); return;                               /* CCRes */
        case 29: W(ms, rt, ms->userlocal); return;                  /* ULR */
        }
        reserved(ms);
        return;
    }
    /* the rest of SPECIAL3 is MT's and EVA's, which neither core has */
    reserved(ms);
}

static void exec(struct mips_state *ms, u32 i)
{
    u32 op = i >> 26, rs = (i >> 21) & 31, rt = (i >> 16) & 31;
    u32 a = R(ms, rs), imm = (u32)(s32)(s16)i, uimm = i & 0xffff;
    u32 t = ms->pc + 4 + (imm << 2), va = a + imm, v = 0;
    switch (op) {
    case 0x00: special(ms, i); return;
    case 0x01: regimm(ms, i); return;
    case 0x02: case 0x03: {                                     /* J, JAL */
        u32 to = ((ms->pc + 4) & 0xf0000000u) | (i & 0x03ffffffu) << 2;
        if (op == 3)
            W(ms, 31, ms->pc + 8);
        else
            self_loop(ms, to);
        branch(ms, 1, to);
        return;
    }
    case 0x04:                                                  /* BEQ */
        if (rs == rt)
            self_loop(ms, t);
        branch(ms, a == R(ms, rt), t);
        return;
    case 0x05: branch(ms, a != R(ms, rt), t); return;                   /* BNE */
    case 0x06: branch(ms, (s32)a <= 0, t); return;                  /* BLEZ */
    case 0x07: branch(ms, (s32)a > 0, t); return;                   /* BGTZ */
    case 0x08:                                                  /* ADDI */
        if (((a + imm) ^ a) & ((a + imm) ^ imm) & 0x80000000u)
            raise_exc(ms, EXC_OV);
        else
            W(ms, rt, a + imm);
        return;
    case 0x09: W(ms, rt, a + imm); return;                          /* ADDIU */
    case 0x0a: W(ms, rt, (s32)a < (s32)imm); return;                /* SLTI */
    case 0x0b: W(ms, rt, a < imm); return;                          /* SLTIU */
    case 0x0c: W(ms, rt, a & uimm); return;                         /* ANDI */
    case 0x0d: W(ms, rt, a | uimm); return;                         /* ORI */
    case 0x0e: W(ms, rt, a ^ uimm); return;                         /* XORI */
    case 0x0f: W(ms, rt, uimm << 16); return;                       /* LUI */
    case 0x10: cop0(ms, i); return;
    case 0x11: case 0x13: case 0x31: case 0x35: case 0x39: case 0x3d:
        /* COP1, COP1X, LWC1, LDC1, SWC1, SDC1 */
        raise_cpu(ms, 1);                                           /* no FPU */
        return;
    case 0x12: case 0x32: case 0x36: case 0x3a: case 0x3e:
        raise_cpu(ms, 2);                                           /* COP2 */
        return;
    case 0x14: branch_likely(ms, a == R(ms, rt), t); return;            /* BEQL */
    case 0x15: branch_likely(ms, a != R(ms, rt), t); return;            /* BNEL */
    case 0x16: branch_likely(ms, (s32)a <= 0, t); return;           /* BLEZL */
    case 0x17: branch_likely(ms, (s32)a > 0, t); return;            /* BGTZL */
    case 0x1c: special2(ms, i); return;
    case 0x1d:                                                  /* JALX */
        if (!ms->mips16) {
            reserved(ms);
            return;
        }
        W(ms, 31, ms->pc + 8);
        branch(ms, 1, ((ms->pc + 4) & 0xf0000000u) | (i & 0x03ffffffu) << 2 | 1);
        return;
    case 0x1f: special3(ms, i); return;
    case 0x20: v = (u32)(s32)(signed char)load(ms, va, 1); break;            /* LB */
    case 0x21: v = (u32)(s32)(s16)load(ms, va, 2); break;           /* LH */
    case 0x23: v = load(ms, va, 4); break;                          /* LW */
    case 0x24: v = load(ms, va, 1); break;                          /* LBU */
    case 0x25: v = load(ms, va, 2); break;                          /* LHU */
    case 0x22: case 0x26: {                                     /* LWL, LWR */
        u32 w = load(ms, va & ~3u, 4), k = va & 3, r = R(ms, rt);
        if (op == 0x22)
            v = (w << (8 * (3 - k))) | (k == 3 ? 0 : r & ((1u << (8 * (3 - k))) - 1));
        else
            v = (w >> (8 * k)) | (k == 0 ? 0 : r & ~(0xffffffffu >> (8 * k)));
        break;
    }
    case 0x30:                                                  /* LL */
        v = load(ms, va, 4);
        if (!ms->exc) {
            u32 pa = 0;
            xlate(ms, va, 0, &pa);
            ms->llbit = 1;
            ms->lladdr = pa;
            ms->llval = v;
        }
        break;
    case 0x28: store(ms, va, 1, R(ms, rt)); return;                     /* SB */
    case 0x29: store(ms, va, 2, R(ms, rt)); return;                     /* SH */
    case 0x2b: store(ms, va, 4, R(ms, rt)); return;                     /* SW */
    case 0x2a: case 0x2e: {                                     /* SWL, SWR */
        u32 k = va & 3, pa = 0, r = R(ms, rt);
        u32 at = op == 0x2a ? va & ~3u : va;
        int n = op == 0x2a ? (int)k + 1 : 4 - (int)k;
        u32 val = op == 0x2a ? r >> (8 * (3 - k)) : r;
        if (xlate(ms, at, 1, &pa) || watched(ms, at, n, 1))
            return;
        if (pwrite(ms, pa, n, val))
            raise_exc(ms, EXC_DBE);
        return;
    }
    case 0x38: {                                                /* SC */
        u32 pa = 0, cur = 0;
        int ok;
        if (va & 3) {
            raise_addr(ms, EXC_ADES, va);
            return;
        }
        if (xlate(ms, va, 1, &pa) || watched(ms, va, 4, 1))
            return;
        ok = ms->llbit && ms->lladdr == pa && !pread(ms, pa, 4, &cur) &&
                 cur == ms->llval;
        if (ok && pwrite(ms, pa, 4, R(ms, rt))) {
            raise_exc(ms, EXC_DBE);
            return;
        }
        ms->llbit = 0;
        W(ms, rt, (u32)ok);
        return;
    }
    case 0x2f:                                                  /* CACHE */
        if (!kernel(ms) && !(ms->status & ST_CU0))
            raise_cpu(ms, 0);
        return;
    case 0x33: return;                                          /* PREF */
    default:
        reserved(ms);
        return;
    }
    if (!ms->exc)
        W(ms, rt, v);
}

/* ---- the run ------------------------------------------------------------- */

/* n bytes of the instruction at va (a MIPS16e halfword, or a MIPS32 word):
 * 0, or -1 with its exception entered, or the run ended when the exception
 * vector itself cannot be fetched */
static int fetch(struct mips_state *ms, u32 va, int n, u32 *w)
{
    struct esk_mach *s = ms->sim;
    struct esk_region *r;
    u32 pa = 0, base;
    if (xlate(ms, va, 2, &pa)) {
        enter(ms, ms->exc_code, 0, (int)((ms->srsctl >> 12) & 15));
        return -1;
    }
    r = esk_bus_region(&s->bus, pa, (u32)n);
    if (r) {
        *w = esk_mem_rd_le(r->mem + (pa - r->base), n);
        return 0;
    }
    if (!esk_bus_read(&s->bus, pa, n, w))
        return 0;
    base = (ms->status & ST_BEV) ? 0xbfc00380u : (ms->ebase & 0xfffff000u) + 0x180;
    if ((va & ~3u) == base) {
        esk_mach_end(s, ESK_MACH_LOCKUP, "lockup: the exception vector 0x%08x cannot be fetched",
                     base);
        return -1;
    }
    raise_exc(ms, EXC_IBE);
    enter(ms, EXC_IBE, 0, (int)((ms->srsctl >> 12) & 15));
    return -1;
}

static void step(struct esk_cpu *c)
{
    struct mips_state *ms = (struct mips_state *)c;
    struct esk_mach *s = ms->sim;
    u32 pc, w = 0, h2 = 0, cost;
    int size = 4;
    if (s->cycles >= ms->ti_at)
        timer_fire(ms);
    if (interrupt_ready(ms)) {
        take_interrupt(ms);
        return;
    }
    ms->exc = 0;
    ms->watch_stop = 0;
    pc = ms->pc;
    if (ms->isa) {
        /* MIPS16e: a halfword, and a second for EXTEND, jal and jalx */
        if (fetch(ms, pc, 2, &w))
            return;
        size = mips16_size(w);
        if (size == 4) {
            if (fetch(ms, pc + 2, 2, &h2))
                return;
            w = w << 16 | h2;
        }
        cost = (u32)mips16_cost(w, size);
    } else {
        if (pc & 3) {
            raise_addr(ms, EXC_ADEL, pc);
            enter(ms, ms->exc_code, 0, (int)((ms->srsctl >> 12) & 15));
            return;
        }
        if (fetch(ms, pc, 4, &w))
            return;
        cost = (u32)esk_mips_cost(w);
    }
    s->insns++;
    s->cycles += cost;
    ms->insn = w;
    if (ms->isa && (!ms->slot || !ms->btaken))
        ms->npc = (pc + (u32)size) | 1; /* MIPS16e: past this one */
    ms->nx = ms->npc;
    ms->nnx = ms->npc + 4;
    ms->nslot = 0;
    if (!ms->isa)
        exec(ms, w);
    else if (ms->slot && ms->ds && ms->ds != size)
        reserved(ms); /* a slot whose branch wants the other size */
    else
        mips16_exec(ms, w, size);
    if (ms->exc) {
        if (ms->watch_stop) {
            /* not run: the debugger stops here, and the instruction is
             * counted when it does run */
            s->insns--;
            s->cycles -= cost;
            return;
        }
        if (s->state != ESK_MACH_RUN)
            return; /* sdbbp */
        enter(ms, ms->exc_code, 0, (int)((ms->srsctl >> 12) & 15));
        if (s->counting)
            esk_mach_advance(s, cost);
        return;
    }
    if (s->counting)
        esk_mach_advance(s, cost);
    ms->pc = ms->nx;
    ms->npc = ms->nnx;
    ms->slot = ms->nslot;
    if (ms->nslot) {
        ms->bpc = ms->nbpc | (u32)ms->isa;
        ms->ds = ms->nds;
        ms->btaken = ms->nbtaken;
    }
    /* a jump's target carries the ISA mode it runs in */
    if (ms->mips16) {
        ms->isa = ms->pc & 1;
        ms->pc &= ~1u;
    }
}

void mips_exec32(struct mips_state *ms, u32 i)
{
    exec(ms, i);
}

/* ---- the CPU interface --------------------------------------------------- */

/* Power-on: Status with BEV and ERL, the boot vector, the registers 0 (the
 * part leaves them undefined; QEMU clears them) */
static void cpu_reset(struct esk_cpu *c)
{
    struct mips_state *k = (struct mips_state *)c;
    int i;
    memset(k->gpr, 0, sizeof k->gpr);
    memset(k->hi, 0, sizeof k->hi);
    memset(k->lo, 0, sizeof k->lo);
    memset(k->tlb, 0, sizeof k->tlb);
    for (i = 0; i < MIPS_TLB; i++) /* distinct, unmatched */
        k->tlb[i].hi = 0x80000000u + ((u32)i << 13);
    k->r = k->gpr[0];
    k->status = ST_BEV | ST_ERL;
    k->cause = 0;
    k->epc = k->errorepc = k->badvaddr = k->depc = 0;
    k->ebase = 0x80000000u;
    k->intctl = k->pic32 ? 0 : 7u << 29;
    k->srsctl = (u32)k->hss << 26;
    k->srsmap = 0;
    k->config0 = (k->config0 & ~7u) | 2;
    k->hwrena = k->userlocal = k->wired = k->index = 0;
    k->entryhi = k->entrylo0 = k->entrylo1 = k->pagemask = k->context = 0;
    k->debug = k->errctl = k->desave = k->watchlo = k->watchhi = 0;
    memset(k->taglo, 0, sizeof k->taglo);
    k->llbit = 0;
    k->compare = 0;
    k->count_base = 0;
    k->count_at = k->sim->cycles;
    k->core_lines = 0;
    k->ip_hw = 0;
    k->exc = 0;
    timer_plan(k);
    k->pc = 0xbfc00000u;
    k->npc = k->pc + 4;
    k->slot = 0;
    k->isa = 0;
}

static u32 cpu_pc(struct esk_cpu *c)
{
    return ((struct mips_state *)c)->pc;
}

/* a hardware interrupt line, IP2 to IP7 (compatibility mode); the
 * register file's numbering is ARM's, 16 + n */
static void cpu_interrupt(struct esk_cpu *c, int n)
{
    struct mips_state *k = (struct mips_state *)c;
    if (n >= 16)
        n -= 16;
    if (n >= 2 && n <= 7)
        k->ip_hw |= 1u << (n - 2);
    if (interrupt_ready(k))
        k->sim->asleep = 0;
}

static u32 cpu_phys(struct esk_cpu *c, u32 a)
{
    ES_UNUSED(c);
    return esk_mips_phys(a);
}

/* an interrupt the core would take: the timer's, if its time has come */
static int cpu_wake_pending(struct esk_cpu *c)
{
    struct mips_state *k = (struct mips_state *)c;
    if (k->sim->cycles >= k->ti_at)
        timer_fire(k);
    return interrupt_ready(k);
}

/* ---- the registers, as GDB numbers them --------------------------------
 *
 * QEMU's stub and gdb's MIPS target without a description: r0-r31 0-31,
 * then Status 32, LO 33, HI 34, BadVAddr 35, Cause 36, PC 37, f0-f31
 * 38-69 (read as 0: no FPU), FCSR 70, FIR 71, and 72 (the frame pointer,
 * read as 0), all in the `g` packet. */

enum { GR_STATUS = 32, GR_LO, GR_HI, GR_BADVADDR, GR_CAUSE, GR_PC };

static int put32(u8 *buf, u32 v)
{
    int i;
    for (i = 0; i < 4; i++)
        buf[i] = (u8)(v >> (8 * i));
    return 4;
}

static int cpu_reg_read(struct esk_cpu *c, int n, u8 *buf)
{
    struct mips_state *ms = (struct mips_state *)c;
    if (n >= 0 && n < 32)
        return put32(buf, ms->r[n]);
    switch (n) {
    case GR_STATUS: return put32(buf, ms->status);
    case GR_LO: return put32(buf, ms->lo[0]);
    case GR_HI: return put32(buf, ms->hi[0]);
    case GR_BADVADDR: return put32(buf, ms->badvaddr);
    case GR_CAUSE: return put32(buf, cp0_read(ms, 13, 0));
    case GR_PC: return put32(buf, ms->pc | (u32)ms->isa);
    }
    if (n >= 38 && n <= 72)
        return put32(buf, 0);
    return 0;
}

static int cpu_reg_write(struct esk_cpu *c, int n, const u8 *buf)
{
    struct mips_state *ms = (struct mips_state *)c;
    u32 v = (u32)buf[0] | (u32)buf[1] << 8 | (u32)buf[2] << 16 | (u32)buf[3] << 24;
    if (n >= 0 && n < 32) {
        if (n)
            ms->r[n] = v;
        return 4;
    }
    switch (n) {
    case GR_STATUS: ms->status = v; ms->sim->bus.touched = 1; return 4;
    case GR_LO: ms->lo[0] = v; return 4;
    case GR_HI: ms->hi[0] = v; return 4;
    case GR_BADVADDR: ms->badvaddr = v; return 4;
    case GR_CAUSE: ms->cause = (ms->cause & ~0x0300u) | (v & 0x0300u); return 4;
    case GR_PC:
        ms->pc = ms->mips16 ? v & ~1u : v;
        ms->isa = ms->mips16 && (v & 1);
        ms->npc = ms->pc + 4;
        ms->slot = 0;
        return 4;
    }
    if (n >= 38 && n <= 72)
        return 4;
    return 0;
}

static const int g_regs[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
    38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
    57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, -1,
};

static const int *cpu_gdb_g_regs(struct esk_cpu *c)
{
    ES_UNUSED(c);
    return g_regs;
}

static const char *cpu_gdb_xml(struct esk_cpu *c, const char *annex)
{
    ES_UNUSED(c);
    ES_UNUSED(annex);
    return 0;
}

static const char *const reg_names[32] = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2",
    "t3", "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5", "s6",
    "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra" };

static const char *cpu_reg_name(struct esk_cpu *c, int n)
{
    ES_UNUSED(c);
    if (n >= 0 && n < 32)
        return reg_names[n];
    switch (n) {
    case GR_STATUS: return "status";
    case GR_LO: return "lo";
    case GR_HI: return "hi";
    case GR_BADVADDR: return "badvaddr";
    case GR_CAUSE: return "cause";
    case GR_PC: return "pc";
    }
    return 0;
}

/* the instruction at addr: MIPS32's text; a MIPS16e halfword as such */
static int cpu_disasm(struct esk_cpu *c, u32 addr, char *text, size_t cap)
{
    struct mips_state *k = (struct mips_state *)c;
    u32 w = 0;
    if (k->isa && addr - k->pc < 0x10000u) {
        if (esk_bus_debug_read(&k->sim->bus, esk_mips_phys(addr), 2, &w) != 0)
            return 0;
        snprintf(text, cap, ".half 0x%04x  (MIPS16e)", (unsigned)w);
        return 2;
    }
    if (esk_bus_debug_read(&k->sim->bus, esk_mips_phys(addr), 4, &w) != 0)
        return 0;
    return esk_mips_disasm(addr, w, text, cap);
}

static const struct esk_cpu_ops mips_ops = {
    "mips", 8, "a MIPS image",
    cpu_reset, step, cpu_pc, cpu_reg_read, cpu_reg_write,
    cpu_gdb_g_regs, cpu_gdb_xml, 4, cpu_interrupt, GR_PC, 1, cpu_phys, cpu_reg_name,
    cpu_wake_pending, cpu_disasm,
};

int esk_mips_is(const struct esk_cpu *c)
{
    return c && c->ops == &mips_ops;
}

u32 esk_mips_count(struct esk_mach *m)
{
    return esk_mips_is(m->cpu) ? count_now((struct mips_state *)m->cpu) : 0;
}

/* The cores: `pic32mx`, a PIC32MX's M4K (EIC mode, one shadow set,
 * MIPS16e, fixed mapping instead of a TLB, no caches, no FPU, no DSP); and
 * `24kc`, QEMU malta's (MIPS32r2 and MIPS16e, no FPU, no DSP, the timer on
 * IP7). The IDs are the parts': PRId and Config1's caches and TLB, which a
 * startup reads to set the caches up. */
es_status esk_mips_create(struct esk_mach *m, const char *model, struct esk_cpu **out)
{
    struct mips_state *k;
    int mx = !strcmp(model, "pic32mx") || !strcmp(model, "m4k");
    if (!mx && strcmp(model, "24kc"))
        return ES_ENOTFOUND;
    k = (struct mips_state *)esk_mach_alloc(m, sizeof *k);
    if (!k)
        return ES_ENOMEM;
    k->cpu.ops = &mips_ops;
    k->sim = m;
    k->pic32 = mx;
    k->mips16 = 1;
    if (mx) {
        /* the M4K (PRId 0x87): fixed mapping (Config.MT 3), dual SRAM
         * interfaces, MIPS16e and EJTAG (Config1), EIC and vectored
         * interrupts (Config3); two register sets */
        k->prid = 0x00018700u;
        k->config0 = 0x80000000u | 1u << 16 | 1u << 10 | 3u << 7;
        k->config1 = 0x80000000u | 1u << 2 | 1u << 1;
        k->config2 = 0x80000000u;
        k->config3 = 1u << 6 | 1u << 5;
        k->fmt = 1;
        k->hss = 1;
    } else {
        /* the 24Kc: 64 KiB caches... as QEMU's PRId and Config have it */
        k->prid = 0x00019300u;
        k->config0 = 0x80000000u | 1u << 10 | 1u << 7;
        k->config1 = 0x80000000u | 15u << 25 | 2u << 22 | 3u << 19 | 3u << 16 | 2u << 13 |
                     3u << 10 | 3u << 7 | 1u << 3 | 1u << 2 | 1u << 1;
        k->config2 = 0x80000000u;
        k->config3 = 0;
    }
    m->cpu = &k->cpu;
    cpu_reset(&k->cpu);
    *out = &k->cpu;
    return esk_mach_add_dev(m, 0, 0, &ctimer_ops, k);
}
