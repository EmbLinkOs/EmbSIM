/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* lx6.c -- the ESP32's Xtensa LX6 cores (Cadence's Xtensa Instruction Set
 * Architecture Reference Manual; the configuration of Espressif's
 * core-isa.h for the ESP32, esp32_v3_49_prod, LX6.0.3), two of them as
 * the chip's PRO_CPU and APP_CPU behind the machine's one core interface.
 *
 * The configuration, as core-isa.h states it:
 *   - the core instructions with the density (16-bit) instructions,
 *     zero-overhead loops, NSA/NSAU, MIN/MAX, SEXT, CLAMPS, MUL16, MULL,
 *     MULUH/MULSH, QUOS/QUOU/REMS/REMU, ADDX/SUBX, ABS, L32AI/S32RI,
 *     S32C1I with SCOMPARE1 and ATOMCTL, booleans (16), THREADPTR, four
 *     MISC registers, PRID (0xCDCD on PRO_CPU, 0xABAB on APP_CPU), the
 *     external registers' RER/WER, the MAC16 package and the single
 *     precision floating-point coprocessor (CP0, lx6fp.c);
 *   - the windowed registers: 64 physical, WindowBase and WindowStart,
 *     CALL4/8/12, ENTRY, RETW, MOVSP, ROTW, the window overflow check of
 *     every instruction while PS.WOE is set and PS.EXCM clear (the frame
 *     of the first live window above the registers it names spilled by
 *     the WindowOverflow4/8/12 vector), RETW's underflow, RFWO/RFWU, and
 *     L32E/S32E for the handlers;
 *   - the exception architecture XEA2: EPC1, EXCCAUSE, EXCVADDR, the
 *     kernel, user and double exception vectors, DEPC; relocatable
 *     vectors (VECBASE, 0x40000000 at reset);
 *   - 32 interrupts at core-isa.h's levels (1-5, the debug level 6, the
 *     NMI at 7) and types (level, edge, software 7 and 29, timers 6, 15
 *     and 16 from CCOMPARE0-2 against CCOUNT, profiling 11), INTERRUPT,
 *     INTSET, INTCLEAR, INTENABLE, EXCMLEVEL 3, RSIL, WAITI, RFI;
 *   - the debug option's BREAK, BREAK.N, two IBREAKs, two DBREAKs and
 *     ICOUNT, raising the debug exception (level 6) with DEBUGCAUSE;
 *   - region protection: eight 512 MB regions in each of the instruction
 *     and data TLBs, attribute 2 at reset, 15 a region no access reaches
 *     (InstFetchProhibited, LoadProhibited, StoreProhibited);
 *   - unaligned loads and stores done by the hardware (no alignment
 *     exception); the ESP32's instruction bus (0x40000000-0x40BFFFFF:
 *     ROM, IRAM, the flash's instruction window) takes 32-bit aligned
 *     loads and stores only, others raising LoadStoreError, as the
 *     chip's TRM says (chapter 1.3.2.2);
 *   - no MMU, no Xtensa caches (the cache instructions do nothing), one
 *     ring, MEMCTL's loop buffer as storage (it changes no result).
 * An access nothing answers raises the PIF address error (14 for a
 * fetch, 15 for a load or store), and is reported (esk_mach_bus_error).
 *
 * Where it differs inside: each instruction takes an estimated time (one
 * cycle, a taken branch, jump, call or return three, a load one, MULL
 * and the high products two, a division twelve, an exception's entry
 * three); the pipeline, its interlocks and the loop buffer's timing are
 * not modelled. CCOUNT counts those cycles.
 *
 * Two cores share the machine as mcu/cortexm/smp.c's do: each keeps its
 * own time, the one furthest behind runs next, the devices see the
 * earliest. APP_CPU starts held in reset, its clock gated, until the
 * part's DPORT model releases it (esk_lx6_core_reset, _stall).
 *
 * Written for EmbSIM from the ISA reference and core-isa.h; it shares no
 * code with the experimental engine in windows.c and execute.c, whose
 * notes on the ESP32's interrupt levels and types it agrees with. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "mcu/xtensa/lx6.h"

#define NCORES 2

/* PS */
#define PS_INTLEVEL 0xfu
#define PS_EXCM 0x10u
#define PS_UM 0x20u
#define PS_OWB_SHIFT 8
#define PS_OWB (0xfu << PS_OWB_SHIFT)
#define PS_CALLINC_SHIFT 16
#define PS_CALLINC (3u << PS_CALLINC_SHIFT)
#define PS_WOE 0x40000u
#define PS_MASK 0x00070f3fu

/* EXCCAUSE */
#define EXC_ILLEGAL 0u
#define EXC_SYSCALL 1u
#define EXC_LOADSTORE 3u
#define EXC_LEVEL1 4u
#define EXC_ALLOCA 5u
#define EXC_DIVZERO 6u
#define EXC_IFETCH_ADDR 14u
#define EXC_LS_ADDR 15u
#define EXC_IFETCH_PROHIBITED 20u
#define EXC_LOAD_PROHIBITED 28u
#define EXC_STORE_PROHIBITED 29u
#define EXC_CP0_DISABLED 32u

/* DEBUGCAUSE */
#define DBG_ICOUNT 0x1u
#define DBG_IBREAK 0x2u
#define DBG_DBREAK 0x4u
#define DBG_BREAK 0x8u
#define DBG_BREAKN 0x10u

/* the interrupts' types and levels (core-isa.h) */
#define INT_SOFTWARE 0x20000080u
#define INT_EDGE 0x50400400u
#define INT_NMI 0x00004000u
#define INT_LATCHED (INT_SOFTWARE | INT_EDGE | INT_NMI)
static const u32 level_mask[8] = { 0, 0x000637ffu, 0x00380000u, 0x28c08800u, 0x53000000u,
                                   0x84010000u, 0, 0x00004000u };
static const int timer_int[3] = { 6, 15, 16 };
#define EXCM_LEVEL 3u
#define DEBUG_LEVEL 6u

/* the vectors' offsets from VECBASE */
#define VEC_OF4 0x000u
#define VEC_UF4 0x040u
#define VEC_OF8 0x080u
#define VEC_UF8 0x0c0u
#define VEC_OF12 0x100u
#define VEC_UF12 0x140u
static const u32 level_vec[8] = { 0, 0, 0x180u, 0x1c0u, 0x200u, 0x240u, 0x280u, 0x2c0u };
#define VEC_KERNEL 0x300u
#define VEC_USER 0x340u
#define VEC_DOUBLE 0x3c0u

#define CONFIGID0 0xC2BCFFFEu
#define CONFIGID1 0x1CC5FE96u

struct smp;

struct lx6 {
    struct esk_cpu cpu; /* first */
    struct esk_mach *m;
    struct smp *x;
    int id;
    u32 ar[64];
    u32 pc, npc;
    u32 wb, ws; /* WindowBase, WindowStart */
    u32 ps, sar, lbeg, lend, lcount, br, scompare1;
    u32 acclo, acchi, mr[4];
    u32 epc[8], eps[8], excsave[8], depc, exccause, excvaddr, debugcause;
    u32 intenable, lines, latched;
    u32 vecbase, ccompare[3];
    u64 ccount_at; /* the core's time CCOUNT counts from */
    u32 ccount_base;
    u32 cpenable, threadptr, fcr, fsr, f[16], misc[4], prid, atomctl, memctl;
    u32 expstate, f64r_lo, f64r_hi, f64s; /* the ESP32's double-precision accelerator's state */
    u32 ibreakenable, ibreaka[2], dbreaka[2], dbreakc[2], icount, icountlevel, ddr;
    u32 itlb[8], dtlb[8];
    u32 reset_vector;
    /* the core's own view of read-only memory, ahead of the bus (the
     * ESP32's APP_CPU's ROM): base, size and bytes */
    struct {
        u32 base, size;
        const u8 *mem;
    } view[2];
    int nview;
    int exc;   /* the instruction took an exception: nothing more of it */
    int jumped; /* it branched, jumped, called or returned: no loop-back */
    int waiti; /* waiting in WAITI */
    u32 cost;
};

enum { CORE_RUN, CORE_SLEEP, CORE_HELD, CORE_STALLED };

struct smp {
    struct esk_cpu cpu; /* first */
    struct esk_mach *m;
    int n;
    struct lx6 *c[NCORES];
    int state[NCORES];
    int held[NCORES], stalled[NCORES];
    u64 lt[NCORES];
    int cur, stepping;
    int running;
};

/* ---- registers -------------------------------------------------------- */

#define AR(c, i) ((c)->ar[((c)->wb * 4u + (u32)(i)) & 63u])

static u32 sext(u32 v, int bits)
{
    u32 b = 1u << (bits - 1);
    v &= (b << 1) - 1u;
    return (v ^ b) - b;
}

/* the core's time: the machine's while its instruction runs */
static u64 now(const struct lx6 *c)
{
    return c->m->cycles;
}

static u32 ccount(const struct lx6 *c)
{
    return c->ccount_base + (u32)(now(c) - c->ccount_at);
}

static u32 interrupt_reg(const struct lx6 *c)
{
    return (c->lines & ~INT_LATCHED) | c->latched;
}

/* WindowStart turned so that bit 0 is WindowBase's */
static u32 ws_rot(const struct lx6 *c)
{
    u32 w = c->ws | c->ws << 16;
    return (w >> c->wb) & 0xffffu;
}

/* ---- exceptions ------------------------------------------------------- */

static void to(struct lx6 *c, u32 pc)
{
    c->npc = pc;
    c->exc = 1;
}

static void exception(struct lx6 *c, u32 cause)
{
    c->exccause = cause;
    if (c->ps & PS_EXCM) {
        c->depc = c->pc;
        to(c, c->vecbase + VEC_DOUBLE);
    } else {
        c->epc[1] = c->pc;
        to(c, c->vecbase + ((c->ps & PS_UM) ? VEC_USER : VEC_KERNEL));
        c->ps |= PS_EXCM;
    }
    c->cost += 2;
    esk_mach_exc(c->m, c->id, (int)cause, c->pc);
}

static void mem_exception(struct lx6 *c, u32 cause, u32 a)
{
    c->excvaddr = a;
    exception(c, cause);
}

void esk_lx6_illegal(struct lx6 *c)
{
    exception(c, EXC_ILLEGAL);
}

/* a high-priority interrupt or the debug exception at `level` */
static void level_entry(struct lx6 *c, u32 level, u32 epc)
{
    c->epc[level] = epc;
    c->eps[level] = c->ps;
    c->ps = (c->ps & ~PS_INTLEVEL) | level | PS_EXCM;
    to(c, c->vecbase + level_vec[level]);
    c->cost += 2;
}

static void debug_exception(struct lx6 *c, u32 cause)
{
    if ((c->ps & PS_INTLEVEL) >= DEBUG_LEVEL)
        return;
    c->debugcause = cause;
    level_entry(c, DEBUG_LEVEL, c->pc);
    esk_mach_exc(c->m, c->id, 64 + (int)DEBUG_LEVEL, c->pc);
}

/* the window overflow check: registers up to `hi` must be the frame's */
static int win(struct lx6 *c, u32 hi)
{
    u32 n, w, owb, k;
    if (hi < 4 || (c->ps & (PS_WOE | PS_EXCM)) != PS_WOE)
        return 0;
    w = ws_rot(c) >> 1;
    if (!(w & ((1u << (hi >> 2)) - 1u)))
        return 0;
    /* the first live frame above is spilled: rotate to it */
    for (n = 1; !(w & 1u); n++)
        w >>= 1;
    w >>= 1;
    owb = c->wb;
    c->wb = (c->wb + n) & 15u;
    c->ps = (c->ps & ~PS_OWB) | owb << PS_OWB_SHIFT | PS_EXCM;
    c->epc[1] = c->pc;
    for (k = 0; k < 3 && !(w & 1u); k++)
        w >>= 1;
    to(c, c->vecbase + (k == 0 ? VEC_OF4 : k == 1 ? VEC_OF8 : VEC_OF12));
    c->cost += 2;
    return 1;
}

/* ---- memory ----------------------------------------------------------- */

static int prohibited(u32 attr)
{
    return attr == 15u;
}

/* the ESP32's instruction bus: 32-bit aligned data accesses only */
static int ibus_narrow(u32 a, int n)
{
    return a >= 0x40000000u && a < 0x40c00000u && (n != 4 || (a & 3u));
}

static int dbreak_hit(struct lx6 *c, u32 a, int n, int store)
{
    int i;
    for (i = 0; i < 2; i++) {
        u32 ctl = c->dbreakc[i], mask;
        if (!(ctl & (store ? 0x80000000u : 0x40000000u)))
            continue;
        /* MASK's set bits are the low address bits compared (ESP-IDF's
         * 32-byte stack-end watchpoint: 0x20, bits 4:0 ignored) */
        mask = 0xffffffc0u | (ctl & 0x3fu);
        if (((a ^ c->dbreaka[i]) & mask) == 0 ||
            (((a + (u32)n - 1u) ^ c->dbreaka[i]) & mask) == 0) {
            if ((c->ps & PS_INTLEVEL) < DEBUG_LEVEL) {
                c->debugcause = DBG_DBREAK | (u32)i << 8;
                level_entry(c, DEBUG_LEVEL, c->pc);
                return 1;
            }
        }
    }
    return 0;
}

int esk_lx6_load(struct lx6 *c, u32 a, int n, u32 *v)
{
    struct esk_bus *b = &c->m->bus;
    struct esk_region *r;
    int i;
    *v = 0;
    if (prohibited(c->dtlb[a >> 29])) {
        mem_exception(c, EXC_LOAD_PROHIBITED, a);
        return -1;
    }
    if (ibus_narrow(a, n)) {
        mem_exception(c, EXC_LOADSTORE, a);
        return -1;
    }
    if (b->nwatch && esk_bus_watch_check(b, a, n, 0)) {
        c->m->yield = 1;
        c->exc = 2; /* not run: the debugger stops before it */
        return -1;
    }
    if ((c->dbreakc[0] | c->dbreakc[1]) && dbreak_hit(c, a, n, 0))
        return -1;
    for (i = 0; i < c->nview; i++)
        if (a - c->view[i].base < c->view[i].size && c->view[i].size - (a - c->view[i].base) >= (u32)n) {
            const u8 *p = c->view[i].mem + (a - c->view[i].base);
            u32 x = 0;
            int j;
            for (j = n - 1; j >= 0; j--)
                x = x << 8 | p[j];
            *v = x;
            return 0;
        }
    r = esk_bus_region(b, a, (u32)n);
    if (r) {
        const u8 *p = r->mem + (a - r->base);
        u32 x = 0;
        for (i = n - 1; i >= 0; i--)
            x = x << 8 | p[i];
        *v = x;
        if (b->trace)
            esk_bus_traced(b, a, n, 0, x, 0);
        return 0;
    }
    for (i = 0; i < n;) {
        u32 x;
        int k = n - i >= 4 && !((a + (u32)i) & 3u) ? 4 : n - i >= 2 && !((a + (u32)i) & 1u) ? 2 : 1;
        if (esk_bus_read(b, a + (u32)i, k, &x)) {
            esk_bus_traced(b, a + (u32)i, k, 0, 0, 1);
            esk_mach_bus_error(c->m, a, n, 0);
            mem_exception(c, EXC_LS_ADDR, a);
            return -1;
        }
        esk_bus_traced(b, a + (u32)i, k, 0, x, 0);
        *v |= x << (8 * i);
        i += k;
    }
    return 0;
}

int esk_lx6_store(struct lx6 *c, u32 a, int n, u32 v)
{
    struct esk_bus *b = &c->m->bus;
    struct esk_region *r;
    int i;
    if (prohibited(c->dtlb[a >> 29])) {
        mem_exception(c, EXC_STORE_PROHIBITED, a);
        return -1;
    }
    if (ibus_narrow(a, n)) {
        mem_exception(c, EXC_LOADSTORE, a);
        return -1;
    }
    if (b->nwatch && esk_bus_watch_check(b, a, n, 1)) {
        c->m->yield = 1;
        c->exc = 2;
        return -1;
    }
    if ((c->dbreakc[0] | c->dbreakc[1]) && dbreak_hit(c, a, n, 1))
        return -1;
    r = esk_bus_region(b, a, (u32)n);
    if (r) {
        if (!r->rom) {
            u8 *p = r->mem + (a - r->base);
            for (i = 0; i < n; i++)
                p[i] = (u8)(v >> (8 * i));
        }
        if (b->trace)
            esk_bus_traced(b, a, n, 1, v, 0);
        return 0;
    }
    for (i = 0; i < n;) {
        int k = n - i >= 4 && !((a + (u32)i) & 3u) ? 4 : n - i >= 2 && !((a + (u32)i) & 1u) ? 2 : 1;
        u32 x = v >> (8 * i);
        if (k < 4)
            x &= (1u << (8 * k)) - 1u;
        if (esk_bus_write(b, a + (u32)i, k, x)) {
            esk_bus_traced(b, a + (u32)i, k, 1, x, 1);
            esk_mach_bus_error(c->m, a, n, 1);
            mem_exception(c, EXC_LS_ADDR, a);
            return -1;
        }
        esk_bus_traced(b, a + (u32)i, k, 1, x, 0);
        i += k;
    }
    return 0;
}

/* the instruction's bytes at pc: 0, or -1 (nothing answers) */
static int fetch(struct lx6 *c, u32 pc, u32 *w)
{
    struct esk_bus *b = &c->m->bus;
    struct esk_region *r;
    u32 lo, hi = 0;
    int i;
    for (i = 0; i < c->nview; i++)
        if (pc - c->view[i].base < c->view[i].size && c->view[i].size - (pc - c->view[i].base) >= 3u) {
            const u8 *p = c->view[i].mem + (pc - c->view[i].base);
            *w = (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16;
            return 0;
        }
    r = esk_bus_region(b, pc, 3);
    if (r) {
        const u8 *p = r->mem + (pc - r->base);
        *w = (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16;
        return 0;
    }
    r = esk_bus_region(b, pc, 2);
    if (r) { /* a narrow instruction at a region's end */
        const u8 *p = r->mem + (pc - r->base);
        *w = (u32)p[0] | (u32)p[1] << 8;
        if ((p[0] & 15u) < 8u) {
            if (esk_bus_read(b, pc + 2u, 1, &hi))
                return -1;
            *w |= (hi & 0xffu) << 16;
        }
        return 0;
    }
    if (esk_bus_read(b, pc & ~3u, 4, &lo))
        return -1;
    if ((pc & 3u) >= 2u) {
        u32 need = (pc & 3u) == 3u ? 3u : ((lo >> 16) & 15u) < 8u ? 3u : 2u;
        if (need > 4u - (pc & 3u) && esk_bus_read(b, (pc & ~3u) + 4u, 4, &hi))
            return -1;
    }
    *w = (u32)(((u64)hi << 32 | lo) >> (8 * (pc & 3u))) & 0xffffffu;
    return 0;
}

/* ---- special registers ------------------------------------------------ */

static void timer_check(struct lx6 *c, u32 from, u32 cycles)
{
    int i;
    for (i = 0; i < 3; i++) {
        u32 d = c->ccompare[i] - from;
        if (d && d <= cycles)
            c->latched |= 1u << timer_int[i];
    }
}

static void set_ccount(struct lx6 *c, u32 v)
{
    c->ccount_base = v;
    c->ccount_at = now(c);
}

/* RSR: 0, or -1 for no such register */
static int rsr(struct lx6 *c, u32 sr, u32 *v)
{
    switch (sr) {
    case 0: *v = c->lbeg; return 0;
    case 1: *v = c->lend; return 0;
    case 2: *v = c->lcount; return 0;
    case 3: *v = c->sar; return 0;
    case 4: *v = c->br; return 0;
    case 12: *v = c->scompare1; return 0;
    case 16: *v = c->acclo; return 0;
    case 17: *v = sext(c->acchi, 8); return 0;
    case 32: case 33: case 34: case 35: *v = c->mr[sr - 32]; return 0;
    case 72: *v = c->wb; return 0;
    case 73: *v = c->ws; return 0;
    case 96: *v = c->ibreakenable; return 0;
    case 97: *v = c->memctl; return 0;
    case 99: *v = c->atomctl; return 0;
    case 104: *v = c->ddr; return 0;
    case 128: case 129: *v = c->ibreaka[sr - 128]; return 0;
    case 144: case 145: *v = c->dbreaka[sr - 144]; return 0;
    case 160: case 161: *v = c->dbreakc[sr - 160]; return 0;
    case 176: *v = CONFIGID0; return 0;
    case 208: *v = CONFIGID1; return 0;
    case 192: *v = c->depc; return 0;
    case 224: *v = c->cpenable; return 0;
    case 226: *v = interrupt_reg(c); return 0;
    case 228: *v = c->intenable; return 0;
    case 230: *v = c->ps; return 0;
    case 231: *v = c->vecbase; return 0;
    case 232: *v = c->exccause; return 0;
    case 233: *v = c->debugcause; return 0;
    case 234: *v = ccount(c); return 0;
    case 235: *v = c->prid; return 0;
    case 236: *v = c->icount; return 0;
    case 237: *v = c->icountlevel; return 0;
    case 238: *v = c->excvaddr; return 0;
    case 240: case 241: case 242: *v = c->ccompare[sr - 240]; return 0;
    case 244: case 245: case 246: case 247: *v = c->misc[sr - 244]; return 0;
    }
    if (sr >= 177 && sr <= 183) {
        *v = c->epc[sr - 176];
        return 0;
    }
    if (sr >= 194 && sr <= 199) {
        *v = c->eps[sr - 192];
        return 0;
    }
    if (sr >= 209 && sr <= 215) {
        *v = c->excsave[sr - 208];
        return 0;
    }
    return -1;
}

static int wsr(struct lx6 *c, u32 sr, u32 v)
{
    switch (sr) {
    case 0: c->lbeg = v; return 0;
    case 1: c->lend = v; return 0;
    case 2: c->lcount = v; return 0;
    case 3: c->sar = v & 0x3fu; return 0;
    case 4: c->br = v & 0xffffu; return 0;
    case 12: c->scompare1 = v; return 0;
    case 16: c->acclo = v; return 0;
    case 17: c->acchi = v & 0xffu; return 0;
    case 32: case 33: case 34: case 35: c->mr[sr - 32] = v; return 0;
    case 72: c->wb = v & 15u; return 0;
    case 73: c->ws = v & 0xffffu; return 0;
    case 96: c->ibreakenable = v & 3u; return 0;
    case 97: c->memctl = v; return 0;
    case 99: c->atomctl = v & 0x3fu; return 0;
    case 104: c->ddr = v; return 0;
    case 128: case 129: c->ibreaka[sr - 128] = v; return 0;
    case 144: case 145: c->dbreaka[sr - 144] = v; return 0;
    case 160: case 161: c->dbreakc[sr - 160] = v & 0xc000003fu; return 0;
    case 192: c->depc = v; return 0;
    case 224: c->cpenable = v & 0xffu; return 0;
    case 226: c->latched |= v & INT_SOFTWARE; return 0; /* INTSET */
    case 227: c->latched &= ~(v & (INT_SOFTWARE | INT_EDGE)); return 0; /* INTCLEAR */
    case 228: c->intenable = v; return 0;
    case 230: c->ps = v & PS_MASK; return 0;
    case 231: c->vecbase = v & ~0x3ffu; return 0;
    case 232: c->exccause = v & 0x3fu; return 0;
    case 233: return 0; /* DEBUGCAUSE: read-only */
    case 234: set_ccount(c, v); return 0;
    case 235: return 0; /* PRID: read-only */
    case 236: c->icount = v; return 0;
    case 237: c->icountlevel = v & 15u; return 0;
    case 238: c->excvaddr = v; return 0;
    case 240: case 241: case 242:
        c->ccompare[sr - 240] = v;
        c->latched &= ~(1u << timer_int[sr - 240]);
        return 0;
    case 244: case 245: case 246: case 247: c->misc[sr - 244] = v; return 0;
    }
    if (sr >= 177 && sr <= 183) {
        c->epc[sr - 176] = v;
        return 0;
    }
    if (sr >= 194 && sr <= 199) {
        c->eps[sr - 192] = v & PS_MASK;
        return 0;
    }
    if (sr >= 209 && sr <= 215) {
        c->excsave[sr - 208] = v;
        return 0;
    }
    if (sr == 176 || sr == 208)
        return 0; /* CONFIGID: read-only */
    return -1;
}

/* the user registers: THREADPTR, the FPU's FCR and FSR, and the ESP32's
 * EXPSTATE and double-precision accelerator registers F64R_LO, F64R_HI,
 * F64S (core-isa.h's XCHAL_HAVE_DFP_ACCEL; Espressif's xtensa-modules) */
static u32 *dfp_ur(struct lx6 *c, u32 ur)
{
    switch (ur) {
    case 230: return &c->expstate;
    case 234: return &c->f64r_lo;
    case 235: return &c->f64r_hi;
    case 236: return &c->f64s;
    }
    return 0;
}

static int rur(struct lx6 *c, u32 ur, u32 *v)
{
    if (ur == 231) {
        *v = c->threadptr;
        return 0;
    }
    if (dfp_ur(c, ur)) {
        *v = *dfp_ur(c, ur);
        return 0;
    }
    if (ur == 232 || ur == 233) {
        if (!(c->cpenable & 1u)) {
            exception(c, EXC_CP0_DISABLED);
            return 1;
        }
        *v = ur == 232 ? c->fcr : c->fsr;
        return 0;
    }
    return -1;
}

static int wur(struct lx6 *c, u32 ur, u32 v)
{
    if (ur == 231) {
        c->threadptr = v;
        return 0;
    }
    if (dfp_ur(c, ur)) {
        *dfp_ur(c, ur) = v;
        return 0;
    }
    if (ur == 232 || ur == 233) {
        if (!(c->cpenable & 1u)) {
            exception(c, EXC_CP0_DISABLED);
            return 1;
        }
        if (ur == 232)
            c->fcr = v & 0x7fu;
        else
            c->fsr = v & 0xf80u;
        return 0;
    }
    return -1;
}

/* ---- the instructions ------------------------------------------------- */

#define OP0(w) ((w) & 15u)
#define FT(w) (((w) >> 4) & 15u)
#define FS(w) (((w) >> 8) & 15u)
#define FR(w) (((w) >> 12) & 15u)
#define OP1(w) (((w) >> 16) & 15u)
#define OP2(w) (((w) >> 20) & 15u)
#define IMM8(w) (((w) >> 16) & 255u)

static const u32 b4const[16] = { 0xffffffffu, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32, 64, 128, 256 };
static const u32 b4constu[16] = { 32768, 65536, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32, 64, 128, 256 };

static u32 max3(u32 a, u32 b, u32 c)
{
    u32 m = a > b ? a : b;
    return m > c ? m : c;
}

static void branch(struct lx6 *c, int taken, u32 target)
{
    if (taken) {
        c->npc = target;
        c->jumped = 1;
        c->cost += 2;
    }
}

/* a call: return address and CALLINC, then the target */
static void call(struct lx6 *c, u32 n, u32 target, u32 len)
{
    if (n) {
        AR(c, n * 4u) = n << 30 | ((c->pc + len) & 0x3fffffffu);
        c->ps = (c->ps & ~PS_CALLINC) | n << PS_CALLINC_SHIFT;
    } else {
        AR(c, 0) = c->pc + len;
    }
    c->npc = target;
    c->jumped = 1;
    c->cost += 2;
}

static void retw(struct lx6 *c)
{
    u32 a0 = AR(c, 0), n = a0 >> 30, owb = c->wb, m = 0, k;
    for (k = 1; k <= 3; k++)
        if ((c->ws >> ((c->wb - k) & 15u)) & 1u) {
            m = k;
            break;
        }
    if (n == 0 || (m != 0 && m != n) || (c->ps & (PS_WOE | PS_EXCM)) != PS_WOE) {
        esk_lx6_illegal(c);
        return;
    }
    c->wb = (c->wb - n) & 15u;
    c->cost += 2;
    if ((c->ws >> c->wb) & 1u) {
        c->ws &= ~(1u << owb);
        c->jumped = 1;
        c->npc = (c->pc & 0xc0000000u) | (a0 & 0x3fffffffu);
    } else {
        /* the caller's frame was spilled: the underflow handler brings
         * it back, and RETW runs again */
        c->ps = (c->ps & ~PS_OWB) | owb << PS_OWB_SHIFT | PS_EXCM;
        c->epc[1] = c->pc;
        to(c, c->vecbase + (n == 1 ? VEC_UF4 : n == 2 ? VEC_UF8 : VEC_UF12));
    }
}

static void entry(struct lx6 *c, u32 s, u32 imm)
{
    u32 callinc = (c->ps & PS_CALLINC) >> PS_CALLINC_SHIFT, v;
    if (s > 3 || (c->ps & (PS_WOE | PS_EXCM)) != PS_WOE) {
        esk_lx6_illegal(c);
        return;
    }
    if (win(c, callinc * 4u + s))
        return;
    v = AR(c, s) - imm;
    AR(c, callinc * 4u + s) = v;
    c->wb = (c->wb + callinc) & 15u;
    c->ws |= 1u << c->wb;
}

static void rfwo(struct lx6 *c, int underflow)
{
    if (underflow)
        c->ws |= 1u << c->wb;
    else
        c->ws &= ~(1u << c->wb);
    c->wb = (c->ps & PS_OWB) >> PS_OWB_SHIFT;
    c->ps &= ~PS_EXCM;
    c->npc = c->epc[1];
    c->cost += 2;
}

static u32 nsa(u32 v)
{
    u32 n = 0;
    v ^= (u32)((s32)v >> 31);
    if (!v)
        return 31;
    while (!(v & 0x80000000u)) {
        v <<= 1;
        n++;
    }
    return n - 1u;
}

static u32 nsau(u32 v)
{
    u32 n = 0;
    if (!v)
        return 32;
    while (!(v & 0x80000000u)) {
        v <<= 1;
        n++;
    }
    return n;
}

/* the external registers (RER/WER): the debug, trace and performance
 * monitor blocks, which no one drives here: reads are 0 */
static u32 rer(struct lx6 *c, u32 a)
{
    ES_UNUSED(c);
    ES_UNUSED(a);
    return 0;
}

/* the MAC16 package: 16 x 16-bit products of the operands' halves into
 * the 40-bit accumulator ACCHI:ACCLO (UMUL, MUL: set; MULA: add; MULS:
 * subtract), the operands from AR (A) or M0-M3 (D), and the forms that
 * load an M register meanwhile (LDINC, LDDEC: the address register moves
 * by 4 first). op2: 0 DD.LDINC, 1 DD.LDDEC, 2 DD, 3 AD, 4 DA.LDINC,
 * 5 DA.LDDEC, 6 DA, 7 AA, 8 LDINC, 9 LDDEC; op1: the operation (bits 3:2)
 * and the halves (bit 0 the first operand's high half, bit 1 the
 * second's) */
static void mac16(struct lx6 *c, u32 w)
{
    u32 op2 = OP2(w), op1 = OP1(w), r = FR(w), s = FS(w), t = FT(w);
    u32 x, y, oper = op1 >> 2, lda = 0, ldv = 0, hi = 0;
    int ld = op2 <= 1 || op2 == 4 || op2 == 5 || op2 == 8 || op2 == 9;
    int dec = op2 == 1 || op2 == 5 || op2 == 9;
    int first_m = op2 <= 2 || (op2 >= 4 && op2 <= 6);
    int second_m = op2 <= 3;
    s64 acc, p;
    if (op2 > 9 || (op2 >= 8 && (op1 || t)) || (op2 < 8 && oper == 0 && op2 != 7)) {
        esk_lx6_illegal(c);
        return;
    }
    if (op2 < 8) {
        if (!first_m)
            hi = s;
        if (!second_m)
            hi = t > hi ? t : hi;
    }
    if (ld)
        hi = s > hi ? s : hi;
    if (win(c, hi))
        return;
    if (ld) {
        lda = AR(c, s) + (dec ? (u32)-4 : 4u);
        if (esk_lx6_load(c, lda, 4, &ldv))
            return;
    }
    if (op2 < 8) {
        x = first_m ? c->mr[(r >> 2) & 1u] : AR(c, s);
        y = second_m ? c->mr[2u + ((t >> 2) & 1u)] : AR(c, t);
        x = (op1 & 1u) ? x >> 16 : x & 0xffffu;
        y = (op1 & 2u) ? y >> 16 : y & 0xffffu;
        if (oper == 0)
            p = (s64)((u64)(x & 0xffffu) * (u64)(y & 0xffffu));
        else
            p = (s64)(s32)sext(x, 16) * (s64)(s32)sext(y, 16);
        acc = (s64)(((u64)(c->acchi & 0xffu) << 32 | c->acclo) << 24) >> 24;
        acc = oper <= 1 ? p : oper == 2 ? acc + p : acc - p;
        c->acclo = (u32)(u64)acc;
        c->acchi = (u32)((u64)acc >> 32) & 0xffu;
    }
    if (ld) {
        c->mr[r & 3u] = ldv;
        AR(c, s) = lda;
    }
}

/* the ESP32's double-precision accelerator instructions (F64ITER,
 * F64RND, F64ADDC, F64SUBC, F64SIG, F64CMPL, F64CMPH, F64NORM, F64SEXP,
 * RF64R: Espressif's xtensa-modules): Espressif publishes their encodings,
 * not what they do, so the run ends here rather than guessing */
static void dfp(struct lx6 *c, u32 w)
{
    esk_mach_end(c->m, ESK_MACH_LOCKUP,
                 "core %d at 0x%08x: the double-precision accelerator's instruction 0x%06x is not modelled",
                 c->id, (unsigned)c->pc, (unsigned)w);
    c->exc = 1;
    c->npc = c->pc;
}

static int cp0(struct lx6 *c)
{
    if (c->cpenable & 1u)
        return 0;
    exception(c, EXC_CP0_DISABLED);
    return 1;
}

/* RST0's ST0: the calls through a register, returns, syncs, the returns
 * from exceptions, BREAK, SYSCALL, RSIL, WAITI and the boolean groups */
static void st0(struct lx6 *c, u32 w)
{
    u32 r = FR(w), s = FS(w), t = FT(w), v;
    switch (r) {
    case 0: { /* SNM0 */
        u32 m = t >> 2, n = t & 3u;
        if (m == 0 && n == 0) {
            esk_lx6_illegal(c);
        } else if (m == 2 && n == 0) { /* RET */
            if (win(c, 0))
                return;
            c->npc = AR(c, 0);
            c->jumped = 1;
            c->cost += 2;
        } else if (m == 2 && n == 1) { /* RETW */
            retw(c);
        } else if (m == 2 && n == 2) { /* JX */
            if (win(c, s))
                return;
            c->npc = AR(c, s);
            c->jumped = 1;
            c->cost += 2;
        } else if (m == 3) { /* CALLX0, 4, 8, 12 */
            if (win(c, s > n * 4u ? s : n * 4u))
                return;
            v = AR(c, s);
            call(c, n, v, 3);
        } else {
            esk_lx6_illegal(c);
        }
        return;
    }
    case 1: /* MOVSP at, as */
        if (win(c, s > t ? s : t))
            return;
        if (!(((c->ws >> ((c->wb - 1u) & 15u)) | (c->ws >> ((c->wb - 2u) & 15u)) |
               (c->ws >> ((c->wb - 3u) & 15u))) & 1u)) {
            exception(c, EXC_ALLOCA);
            return;
        }
        AR(c, t) = AR(c, s);
        return;
    case 2: /* SYNC: ISYNC RSYNC ESYNC DSYNC EXCW MEMW EXTW NOP */
        if (s || (t > 3 && t != 8 && t != 12 && t != 13 && t != 15))
            esk_lx6_illegal(c);
        return;
    case 3: /* RFEI */
        if (t == 0) {
            switch (s) {
            case 0: /* RFE */
                c->ps &= ~PS_EXCM;
                c->npc = c->epc[1];
                c->cost += 2;
                esk_mach_exc_return(c->m, c->id);
                return;
            case 1: /* RFUE: XEA1 only */
                esk_lx6_illegal(c);
                return;
            case 2: /* RFDE */
                c->npc = c->depc;
                c->cost += 2;
                esk_mach_exc_return(c->m, c->id);
                return;
            case 4: rfwo(c, 0); return;
            case 5: rfwo(c, 1); return;
            }
        } else if (t == 1 && s >= 2 && s <= 7) { /* RFI level */
            c->ps = c->eps[s];
            c->npc = c->epc[s];
            c->cost += 2;
            esk_mach_exc_return(c->m, c->id);
            return;
        }
        esk_lx6_illegal(c);
        return;
    case 4: /* BREAK s, t */
        debug_exception(c, DBG_BREAK);
        return;
    case 5: /* SYSCALL, SIMCALL (no simulator: nothing) */
        if (t == 0 && s == 0)
            exception(c, EXC_SYSCALL);
        else if (!(t == 0 && s == 1))
            esk_lx6_illegal(c);
        return;
    case 6: /* RSIL at, level */
        if (win(c, t))
            return;
        AR(c, t) = c->ps;
        c->ps = (c->ps & ~PS_INTLEVEL) | s;
        return;
    case 7: /* WAITI */
        c->ps = (c->ps & ~PS_INTLEVEL) | s;
        c->waiti = 1;
        return;
    case 8: case 9: case 10: case 11: { /* ANY4 ALL4 ANY8 ALL8 */
        u32 width = r < 10 ? 4u : 8u, mask = (1u << width) - 1u, bits;
        if (s & (width - 1u)) {
            esk_lx6_illegal(c);
            return;
        }
        bits = (c->br >> s) & mask;
        v = (r & 1u) ? bits == mask : bits != 0;
        c->br = (c->br & ~(1u << t)) | v << t;
        return;
    }
    }
    esk_lx6_illegal(c);
}

/* region protection's TLB instructions (RST0's op2 5) */
static void tlb(struct lx6 *c, u32 w)
{
    u32 r = FR(w), s = FS(w), t = FT(w), a, *tl;
    if (win(c, s > t ? s : t))
        return;
    a = AR(c, s);
    tl = r < 8 ? c->itlb : c->dtlb;
    switch (r & 7u) {
    case 3: AR(c, t) = a & 0xe0000000u; return;                   /* R?TLB0 */
    case 4: return;                                               /* I?TLB */
    case 5: AR(c, t) = (a & 0xe0000000u) | 1u; return;            /* P?TLB */
    case 6: tl[a >> 29] = AR(c, t) & 15u; return;                 /* W?TLB */
    case 7: AR(c, t) = (a & 0xe0000000u) | tl[a >> 29]; return;   /* R?TLB1 */
    }
    esk_lx6_illegal(c);
}

/* one instruction `w` of `len` bytes at c->pc */
static void exec(struct lx6 *c, u32 w, u32 len)
{
    u32 op0 = OP0(w), r = FR(w), s = FS(w), t = FT(w), op1 = OP1(w), op2 = OP2(w);
    u32 a, v, x, y;
    switch (op0) {
    case 0: /* QRST */
        switch (op1) {
        case 0: /* RST0 */
            switch (op2) {
            case 0: st0(c, w); return;
            case 1: case 2: case 3: /* AND OR XOR */
                if (win(c, max3(r, s, t)))
                    return;
                x = AR(c, s);
                y = AR(c, t);
                AR(c, r) = op2 == 1 ? x & y : op2 == 2 ? x | y : x ^ y;
                return;
            case 4: /* ST1 */
                switch (r) {
                case 0: case 1: case 2: case 3:
                    if (win(c, s))
                        return;
                    x = AR(c, s);
                    c->sar = r == 0 ? x & 31u : r == 1 ? 32u - (x & 31u) : r == 2 ? (x & 3u) * 8u
                                                                                : 32u - (x & 3u) * 8u;
                    return;
                case 4: /* SSAI */
                    c->sar = s | (t & 1u) << 4;
                    return;
                case 6: /* RER */
                    if (win(c, s > t ? s : t))
                        return;
                    AR(c, t) = rer(c, AR(c, s));
                    return;
                case 7: /* WER */
                    if (win(c, s > t ? s : t))
                        return;
                    return;
                case 8: /* ROTW */
                    c->wb = (c->wb + t) & 15u;
                    return;
                case 14: case 15: /* NSA NSAU */
                    if (win(c, s > t ? s : t))
                        return;
                    AR(c, t) = r == 14 ? nsa(AR(c, s)) : nsau(AR(c, s));
                    return;
                }
                break;
            case 5: tlb(c, w); return;
            case 6: /* NEG ABS */
                if (s > 1)
                    break;
                if (win(c, r > t ? r : t))
                    return;
                x = AR(c, t);
                AR(c, r) = s == 0 ? 0u - x : (x & 0x80000000u) ? 0u - x : x;
                return;
            case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
                if (win(c, max3(r, s, t)))
                    return;
                x = AR(c, s) << (op2 & 3u);
                y = AR(c, t);
                AR(c, r) = op2 < 12 ? x + y : x - y;
                return;
            }
            break;
        case 1: /* RST1 */
            switch (op2) {
            case 0: case 1: /* SLLI */
                if (win(c, r > s ? r : s))
                    return;
                AR(c, r) = (u32)((u64)AR(c, s) << (32u - (t | (op2 & 1u) << 4)));
                return;
            case 2: case 3: /* SRAI */
                if (win(c, r > t ? r : t))
                    return;
                AR(c, r) = (u32)((s32)AR(c, t) >> (s | (op2 & 1u) << 4));
                return;
            case 4: /* SRLI */
                if (win(c, r > t ? r : t))
                    return;
                AR(c, r) = AR(c, t) >> s;
                return;
            case 6: /* XSR */
                if (win(c, t))
                    return;
                if (rsr(c, r << 4 | s, &v)) {
                    esk_lx6_illegal(c);
                    return;
                }
                x = AR(c, t);
                if (wsr(c, r << 4 | s, x)) {
                    esk_lx6_illegal(c);
                    return;
                }
                AR(c, t) = v;
                return;
            case 8: /* SRC */
                if (win(c, max3(r, s, t)))
                    return;
                AR(c, r) = (u32)(((u64)AR(c, s) << 32 | AR(c, t)) >> c->sar);
                return;
            case 9: /* SRL */
                if (win(c, r > t ? r : t))
                    return;
                AR(c, r) = (u32)((u64)AR(c, t) >> c->sar);
                return;
            case 10: /* SLL */
                if (win(c, r > s ? r : s))
                    return;
                AR(c, r) = (u32)(((u64)AR(c, s) << 32) >> c->sar);
                return;
            case 11: /* SRA */
                if (win(c, r > t ? r : t))
                    return;
                AR(c, r) = (u32)((s64)(s32)AR(c, t) >> (c->sar > 63u ? 63u : c->sar));
                return;
            case 12: /* MUL16U */
                if (win(c, max3(r, s, t)))
                    return;
                AR(c, r) = (AR(c, s) & 0xffffu) * (AR(c, t) & 0xffffu);
                return;
            case 13: /* MUL16S */
                if (win(c, max3(r, s, t)))
                    return;
                AR(c, r) = (u32)((s32)sext(AR(c, s), 16) * (s32)sext(AR(c, t), 16));
                return;
            }
            break;
        case 2: /* RST2 */
            if (op2 <= 4) { /* ANDB ANDBC ORB ORBC XORB */
                u32 bs = (c->br >> s) & 1u, bt = (c->br >> t) & 1u, b;
                b = op2 == 0 ? bs & bt : op2 == 1 ? bs & !bt : op2 == 2 ? bs | bt : op2 == 3 ? bs | !bt
                                                                                         : bs ^ bt;
                c->br = (c->br & ~(1u << r)) | b << r;
                return;
            }
            if (op2 < 8 || op2 == 9)
                break;
            if (win(c, max3(r, s, t)))
                return;
            x = AR(c, s);
            y = AR(c, t);
            switch (op2) {
            case 8: AR(c, r) = x * y; c->cost += 1; return; /* MULL */
            case 10: AR(c, r) = (u32)(((u64)x * y) >> 32); c->cost += 1; return; /* MULUH */
            case 11: AR(c, r) = (u32)((u64)((s64)(s32)x * (s32)y) >> 32); c->cost += 1; return; /* MULSH */
            }
            if (!y) {
                exception(c, EXC_DIVZERO);
                return;
            }
            c->cost += 11;
            switch (op2) {
            case 12: AR(c, r) = x / y; return; /* QUOU */
            case 13: /* QUOS */
                AR(c, r) = (x == 0x80000000u && y == 0xffffffffu) ? x : (u32)((s32)x / (s32)y);
                return;
            case 14: AR(c, r) = x % y; return; /* REMU */
            case 15: /* REMS */
                AR(c, r) = (x == 0x80000000u && y == 0xffffffffu) ? 0u : (u32)((s32)x % (s32)y);
                return;
            }
            break;
        case 3: /* RST3 */
            switch (op2) {
            case 0: /* RSR */
                if (win(c, t))
                    return;
                if (rsr(c, r << 4 | s, &v)) {
                    esk_lx6_illegal(c);
                    return;
                }
                AR(c, t) = v;
                return;
            case 1: /* WSR */
                if (win(c, t))
                    return;
                if (wsr(c, r << 4 | s, AR(c, t)))
                    esk_lx6_illegal(c);
                return;
            case 2: /* SEXT */
                if (win(c, r > s ? r : s))
                    return;
                AR(c, r) = sext(AR(c, s), (int)t + 8);
                return;
            case 3: { /* CLAMPS */
                s32 lim = (s32)(1u << (t + 7u)), z;
                if (win(c, r > s ? r : s))
                    return;
                z = (s32)AR(c, s);
                AR(c, r) = (u32)(z < -lim ? -lim : z > lim - 1 ? lim - 1 : z);
                return;
            }
            case 4: case 5: case 6: case 7: /* MIN MAX MINU MAXU */
                if (win(c, max3(r, s, t)))
                    return;
                x = AR(c, s);
                y = AR(c, t);
                if (op2 == 4)
                    AR(c, r) = (s32)x < (s32)y ? x : y;
                else if (op2 == 5)
                    AR(c, r) = (s32)x < (s32)y ? y : x;
                else if (op2 == 6)
                    AR(c, r) = x < y ? x : y;
                else
                    AR(c, r) = x < y ? y : x;
                return;
            case 8: case 9: case 10: case 11: { /* MOVEQZ MOVNEZ MOVLTZ MOVGEZ */
                int mv;
                if (win(c, max3(r, s, t)))
                    return;
                y = AR(c, t);
                mv = op2 == 8 ? y == 0 : op2 == 9 ? y != 0 : op2 == 10 ? (s32)y < 0 : (s32)y >= 0;
                if (mv)
                    AR(c, r) = AR(c, s);
                return;
            }
            case 12: case 13: /* MOVF MOVT */
                if (win(c, r > s ? r : s))
                    return;
                if (((c->br >> t) & 1u) == (u32)(op2 == 13))
                    AR(c, r) = AR(c, s);
                return;
            case 14: { /* RUR */
                int k;
                if (win(c, r))
                    return;
                k = rur(c, s << 4 | t, &v);
                if (k < 0)
                    esk_lx6_illegal(c);
                else if (k == 0)
                    AR(c, r) = v;
                return;
            }
            case 15: /* WUR */
                if (win(c, t))
                    return;
                if (wur(c, r << 4 | s, AR(c, t)) < 0)
                    esk_lx6_illegal(c);
                return;
            }
            break;
        case 4: case 5: /* EXTUI */
            if (win(c, r > t ? r : t))
                return;
            x = AR(c, t) >> (s | (op1 & 1u) << 4);
            AR(c, r) = x & (u32)((1ull << (op2 + 1u)) - 1u);
            return;
        case 8: case 10: case 11: /* LSCX, FP0, FP1 */
            if (op1 == 11 && (op2 == 0 || op2 == 14 || op2 == 15)) {
                dfp(c, w); /* F64ADDC, F64SUBC, F64CMPL, RF64R */
                return;
            }
            if (cp0(c))
                return;
            if (esk_lx6_fp(c, w) < 0)
                esk_lx6_illegal(c);
            return;
        case 14: case 15: /* the double-precision accelerator's other instructions */
            if ((op1 == 14 && op2 <= 8) || (op1 == 15 && op2 == 0)) {
                dfp(c, w);
                return;
            }
            break;
        case 9: /* LSC4: L32E S32E */
            if (op2 != 0 && op2 != 4)
                break;
            if (win(c, s > t ? s : t))
                return;
            a = AR(c, s) + ((r << 2) | 0xffffffc0u);
            if (op2 == 0) {
                if (esk_lx6_load(c, a, 4, &v) == 0)
                    AR(c, t) = v;
            } else {
                (void)esk_lx6_store(c, a, 4, AR(c, t));
            }
            return;
        }
        esk_lx6_illegal(c);
        return;
    case 1: /* L32R */
        if (win(c, t))
            return;
        a = ((c->pc + 3u) & ~3u) + ((0xffff0000u | (w >> 8)) << 2);
        if (esk_lx6_load(c, a, 4, &v) == 0)
            AR(c, t) = v;
        return;
    case 2: { /* LSAI */
        u32 imm = IMM8(w);
        switch (r) {
        case 0: case 1: case 2: case 9: case 11: { /* L8UI L16UI L32I L16SI L32AI */
            int n = r == 0 ? 1 : r == 1 || r == 9 ? 2 : 4;
            if (win(c, s > t ? s : t))
                return;
            a = AR(c, s) + imm * (u32)n;
            if (esk_lx6_load(c, a, n, &v))
                return;
            AR(c, t) = r == 9 ? sext(v, 16) : v;
            return;
        }
        case 4: case 5: case 6: case 15: { /* S8I S16I S32I S32RI */
            int n = r == 4 ? 1 : r == 5 ? 2 : 4;
            if (win(c, s > t ? s : t))
                return;
            a = AR(c, s) + imm * (u32)n;
            (void)esk_lx6_store(c, a, n, AR(c, t));
            return;
        }
        case 7: /* CACHE: no caches, nothing to do (DPFR, IPF, ...) */
            if (win(c, s))
                return;
            return;
        case 10: /* MOVI */
            if (win(c, t))
                return;
            AR(c, t) = sext(imm | s << 8, 12);
            return;
        case 12: /* ADDI */
            if (win(c, s > t ? s : t))
                return;
            AR(c, t) = AR(c, s) + sext(imm, 8);
            return;
        case 13: /* ADDMI */
            if (win(c, s > t ? s : t))
                return;
            AR(c, t) = AR(c, s) + (sext(imm, 8) << 8);
            return;
        case 14: /* S32C1I */
            if (win(c, s > t ? s : t))
                return;
            a = AR(c, s) + imm * 4u;
            if (esk_lx6_load(c, a, 4, &v))
                return;
            if (v == c->scompare1 && esk_lx6_store(c, a, 4, AR(c, t)))
                return;
            AR(c, t) = v;
            return;
        }
        esk_lx6_illegal(c);
        return;
    }
    case 3: /* LSCI: LSI SSI (LSIU, SSIU are not in this configuration) */
        if (r != 0 && r != 4) {
            esk_lx6_illegal(c);
            return;
        }
        if (cp0(c))
            return;
        if (win(c, s))
            return;
        a = AR(c, s) + IMM8(w) * 4u;
        if (r == 0) {
            if (esk_lx6_load(c, a, 4, &v) == 0)
                c->f[t] = v;
        } else {
            (void)esk_lx6_store(c, a, 4, c->f[t]);
        }
        return;
    case 4: /* MAC16 */
        mac16(c, w);
        return;
    case 5: { /* CALLN */
        u32 n = (w >> 4) & 3u;
        if (win(c, n * 4u))
            return;
        call(c, n, (c->pc & ~3u) + 4u + (sext(w >> 6, 18) << 2), 3);
        return;
    }
    case 6: { /* SI */
        u32 n = (w >> 4) & 3u, m = (w >> 6) & 3u;
        if (n == 0) { /* J */
            c->npc = c->pc + 4u + sext(w >> 6, 18);
            c->jumped = 1;
            c->cost += 2;
            return;
        }
        if (n == 1) { /* BZ */
            if (win(c, s))
                return;
            x = AR(c, s);
            branch(c, m == 0 ? x == 0 : m == 1 ? x != 0 : m == 2 ? (s32)x < 0 : (s32)x >= 0,
                   c->pc + 4u + sext(w >> 12, 12));
            return;
        }
        if (n == 2) { /* BI0 */
            if (win(c, s))
                return;
            x = AR(c, s);
            y = b4const[r];
            branch(c, m == 0 ? x == y : m == 1 ? x != y : m == 2 ? (s32)x < (s32)y : (s32)x >= (s32)y,
                   c->pc + 4u + sext(IMM8(w), 8));
            return;
        }
        /* BI1 */
        if (m == 0) { /* ENTRY */
            entry(c, s, (w >> 12) << 3);
            return;
        }
        if (m == 1) { /* B1 */
            switch (r) {
            case 0: case 1: /* BF BT */
                branch(c, ((c->br >> s) & 1u) == r, c->pc + 4u + sext(IMM8(w), 8));
                return;
            case 8: case 9: case 10: /* LOOP LOOPNEZ LOOPGTZ */
                if (win(c, s))
                    return;
                x = AR(c, s);
                c->lcount = x - 1u;
                c->lbeg = c->pc + 3u;
                c->lend = c->pc + 4u + IMM8(w);
                if ((r == 9 && x == 0) || (r == 10 && (s32)x <= 0)) {
                    c->npc = c->lend;
                    c->jumped = 1;
                    c->cost += 2;
                }
                return;
            }
            esk_lx6_illegal(c);
            return;
        }
        if (win(c, s))
            return;
        x = AR(c, s);
        y = b4constu[r];
        branch(c, m == 2 ? x < y : x >= y, c->pc + 4u + sext(IMM8(w), 8)); /* BLTUI BGEUI */
        return;
    }
    case 7: { /* B */
        int taken;
        u32 bit;
        if (win(c, (r & 7u) == 6u || (r & 7u) == 7u ? s : s > t ? s : t))
            return;
        x = AR(c, s);
        switch (r) {
        case 0: taken = (x & AR(c, t)) == 0; break;           /* BNONE */
        case 1: taken = x == AR(c, t); break;                  /* BEQ */
        case 2: taken = (s32)x < (s32)AR(c, t); break;         /* BLT */
        case 3: taken = x < AR(c, t); break;                   /* BLTU */
        case 4: taken = (~x & AR(c, t)) == 0; break;           /* BALL */
        case 5: taken = !((x >> (AR(c, t) & 31u)) & 1u); break; /* BBC */
        case 6: case 7: bit = t | (r & 1u) << 4; taken = !((x >> bit) & 1u); break; /* BBCI */
        case 8: taken = (x & AR(c, t)) != 0; break;           /* BANY */
        case 9: taken = x != AR(c, t); break;                  /* BNE */
        case 10: taken = (s32)x >= (s32)AR(c, t); break;       /* BGE */
        case 11: taken = x >= AR(c, t); break;                 /* BGEU */
        case 12: taken = (~x & AR(c, t)) != 0; break;         /* BNALL */
        case 13: taken = (x >> (AR(c, t) & 31u)) & 1u; break;  /* BBS */
        default: bit = t | (r & 1u) << 4; taken = (x >> bit) & 1u; break; /* BBSI */
        }
        branch(c, taken, c->pc + 4u + sext(IMM8(w), 8));
        return;
    }
    case 8: /* L32I.N */
        if (win(c, s > t ? s : t))
            return;
        if (esk_lx6_load(c, AR(c, s) + r * 4u, 4, &v) == 0)
            AR(c, t) = v;
        return;
    case 9: /* S32I.N */
        if (win(c, s > t ? s : t))
            return;
        (void)esk_lx6_store(c, AR(c, s) + r * 4u, 4, AR(c, t));
        return;
    case 10: /* ADD.N */
        if (win(c, max3(r, s, t)))
            return;
        AR(c, r) = AR(c, s) + AR(c, t);
        return;
    case 11: /* ADDI.N */
        if (win(c, r > s ? r : s))
            return;
        AR(c, r) = AR(c, s) + (t ? t : 0xffffffffu);
        return;
    case 12: /* ST2: MOVI.N BEQZ.N BNEZ.N */
        if (win(c, s))
            return;
        if (!(t & 8u)) {
            v = r | (t & 7u) << 4;
            AR(c, s) = v >= 96u ? v - 128u : v;
        } else {
            x = AR(c, s);
            branch(c, (t & 4u) ? x != 0 : x == 0, c->pc + 4u + (r | (t & 3u) << 4));
        }
        return;
    case 13: /* ST3 */
        if (r == 0) { /* MOV.N */
            if (win(c, s > t ? s : t))
                return;
            AR(c, t) = AR(c, s);
            return;
        }
        if (r == 15) {
            switch (t) {
            case 0: /* RET.N */
                c->npc = AR(c, 0);
                c->jumped = 1;
                c->cost += 2;
                return;
            case 1: retw(c); return;
            case 2: debug_exception(c, DBG_BREAKN); return;
            case 3: return; /* NOP.N */
            }
        }
        esk_lx6_illegal(c);
        return;
    }
    ES_UNUSED(len);
    esk_lx6_illegal(c);
}


/* ---- interrupts and a step -------------------------------------------- */

/* the level of the interrupt the core would take now, or 0 */
static u32 irq_level(const struct lx6 *c)
{
    u32 pend = interrupt_reg(c) & c->intenable, th = c->ps & PS_INTLEVEL, l;
    if (!pend)
        return 0;
    if ((c->ps & PS_EXCM) && th < EXCM_LEVEL)
        th = EXCM_LEVEL;
    if (pend & level_mask[7])
        return 7;
    for (l = 5; l > th; l--)
        if (pend & level_mask[l])
            return l;
    return 0;
}

static int take_irq(struct lx6 *c)
{
    u32 l = irq_level(c);
    if (!l)
        return 0;
    if (l == 1)
        exception(c, EXC_LEVEL1);
    else
        level_entry(c, l, c->pc);
    if (l == 7)
        c->latched &= ~INT_NMI;
    if (l != 1)
        esk_mach_exc(c->m, c->id, 64 + (int)l, c->pc);
    c->waiti = 0;
    return 1;
}

static void core_step(struct lx6 *c)
{
    struct esk_mach *m = c->m;
    u32 w = 0, len = 3, before = ccount(c);
    c->exc = 0;
    c->cost = 1;
    if (take_irq(c))
        goto done;
    if (c->waiti) {
        m->asleep = 1;
        return;
    }
    if (c->ibreakenable && (c->ps & PS_INTLEVEL) < DEBUG_LEVEL &&
        (((c->ibreakenable & 1u) && c->pc == c->ibreaka[0]) ||
         ((c->ibreakenable & 2u) && c->pc == c->ibreaka[1]))) {
        c->debugcause = DBG_IBREAK;
        level_entry(c, DEBUG_LEVEL, c->pc);
        goto done;
    }
    if (prohibited(c->itlb[c->pc >> 29])) {
        c->excvaddr = c->pc;
        exception(c, EXC_IFETCH_PROHIBITED);
        goto done;
    }
    if (fetch(c, c->pc, &w)) {
        if (c->pc == c->vecbase + VEC_KERNEL || c->pc == c->vecbase + VEC_DOUBLE) {
            esk_mach_end(m, ESK_MACH_LOCKUP, "core %d: the exception vector 0x%08x cannot be fetched",
                         c->id, (unsigned)c->pc);
            return;
        }
        esk_mach_bus_error(m, c->pc, 3, 0);
        c->excvaddr = c->pc;
        exception(c, EXC_IFETCH_ADDR);
        goto done;
    }
    if ((w & 15u) >= 8u) {
        len = 2;
        w &= 0xffffu;
    }
    c->npc = c->pc + len;
    c->jumped = 0;
    m->insns++;
    if ((w & 15u) >= 14u)
        esk_lx6_illegal(c);
    else
        exec(c, w, len);
    if (c->exc == 3) { /* the core was reset by its own store */
        c->exc = 0;
        m->cycles += c->cost;
        return;
    }
    if (c->exc == 2) { /* a watchpoint: the debugger stops before it */
        m->insns--;
        c->exc = 0;
        return;
    }
    if (!c->exc) {
        if (!c->jumped && c->npc == c->pc + len && c->npc == c->lend && c->lcount && !(c->ps & PS_EXCM)) {
            c->lcount--;
            c->npc = c->lbeg;
        }
        if (c->icountlevel > (c->ps & PS_INTLEVEL) && ++c->icount == 0) {
            c->pc = c->npc;
            c->debugcause = DBG_ICOUNT;
            level_entry(c, DEBUG_LEVEL, c->pc);
        }
    }
done:
    c->pc = c->npc;
    m->cycles += c->cost;
    timer_check(c, before, c->cost);
}

static int core_wake_pending(const struct lx6 *c)
{
    return irq_level(c) != 0;
}

static void core_reset(struct lx6 *c)
{
    int i;
    u32 prid = c->prid, rv = c->reset_vector;
    memset(c->ar, 0, sizeof c->ar);
    c->pc = c->npc = rv ? 0x40000400u : 0x50000000u;
    c->wb = 0;
    c->ws = 1;
    c->ps = 0x1fu; /* INTLEVEL 15, EXCM */
    c->sar = c->lbeg = c->lend = c->lcount = c->br = c->scompare1 = 0;
    c->acclo = c->acchi = 0;
    memset(c->mr, 0, sizeof c->mr);
    memset(c->epc, 0, sizeof c->epc);
    memset(c->eps, 0, sizeof c->eps);
    memset(c->excsave, 0, sizeof c->excsave);
    c->depc = c->exccause = c->excvaddr = c->debugcause = 0;
    c->intenable = c->latched = 0;
    c->vecbase = 0x40000000u;
    memset(c->ccompare, 0, sizeof c->ccompare);
    c->ccount_base = 0;
    c->ccount_at = c->m->cycles;
    c->cpenable = c->threadptr = c->fcr = c->fsr = 0;
    c->expstate = c->f64r_lo = c->f64r_hi = c->f64s = 0;
    memset(c->f, 0, sizeof c->f);
    memset(c->misc, 0, sizeof c->misc);
    c->prid = prid;
    c->atomctl = 0x28u;
    c->memctl = 1u;
    c->ibreakenable = c->icount = c->icountlevel = c->ddr = 0;
    memset(c->ibreaka, 0, sizeof c->ibreaka);
    memset(c->dbreaka, 0, sizeof c->dbreaka);
    memset(c->dbreakc, 0, sizeof c->dbreakc);
    for (i = 0; i < 8; i++)
        c->itlb[i] = c->dtlb[i] = 2u;
    c->waiti = 0;
    c->exc = 0;
}

/* ---- two cores, one interface ----------------------------------------- */

static void smp_step(struct esk_cpu *cpu);

static struct smp *smp_of(struct esk_mach *m)
{
    return m->cpu && m->cpu->ops->step == smp_step ? (struct smp *)m->cpu : 0;
}

static int next_core(const struct smp *x)
{
    int i, best = -1;
    for (i = 0; i < x->n; i++)
        if (x->state[i] == CORE_RUN && (best < 0 || x->lt[i] < x->lt[best]))
            best = i;
    return best < 0 ? 0 : best;
}

static int current(const struct smp *x)
{
    return x->stepping ? x->cur : next_core(x);
}

static int any_running(const struct smp *x)
{
    int i;
    for (i = 0; i < x->n; i++)
        if (x->state[i] == CORE_RUN)
            return 1;
    return 0;
}

static void settle(struct smp *x, int i)
{
    int st = x->held[i] ? CORE_HELD : x->stalled[i] ? CORE_STALLED
           : x->c[i]->waiti && !core_wake_pending(x->c[i]) ? CORE_SLEEP : CORE_RUN;
    if (st == CORE_RUN && x->state[i] != CORE_RUN) {
        if (x->lt[i] < x->m->cycles)
            x->lt[i] = x->m->cycles;
        x->m->asleep = 0;
    }
    x->state[i] = st;
}

static void smp_reset(struct esk_cpu *cpu)
{
    struct smp *x = (struct smp *)cpu;
    int i;
    for (i = 0; i < x->n; i++) {
        core_reset(x->c[i]);
        x->held[i] = i != 0; /* APP_CPU: DPORT holds it until released */
        x->stalled[i] = 0;
        x->lt[i] = x->m->cycles;
        x->state[i] = i == 0 ? CORE_RUN : CORE_HELD;
    }
    x->stepping = 0;
    x->cur = 0;
}

static void smp_step(struct esk_cpu *cpu)
{
    struct smp *x = (struct smp *)cpu;
    struct esk_mach *m = x->m;
    u64 g0 = m->cycles, g = ~(u64)0;
    int i = next_core(x), counting = m->counting, j;
    if (x->state[i] != CORE_RUN) {
        m->asleep = 1;
        return;
    }
    m->cycles = x->lt[i];
    m->counting = 0;
    m->asleep = 0;
    x->cur = i;
    x->stepping = 1;
    esk_mach_step_core(m, i, &x->c[i]->cpu);
    x->stepping = 0;
    x->lt[i] = m->cycles;
    m->counting += counting;
    if (x->c[i]->waiti && x->state[i] == CORE_RUN && !core_wake_pending(x->c[i]))
        x->state[i] = CORE_SLEEP;
    for (j = 0; j < x->n; j++)
        if (x->state[j] == CORE_RUN && x->lt[j] < g)
            g = x->lt[j];
    if (g == ~(u64)0)
        g = x->lt[i] > g0 ? x->lt[i] : g0;
    m->cycles = g0;
    if (g > g0) {
        m->cycles = g;
        if (m->counting)
            esk_mach_advance(m, (u32)(g - g0));
    }
    m->asleep = !any_running(x);
}

/* the core interface on one core: the analyses step it */
static void one_step(struct esk_cpu *cpu)
{
    core_step((struct lx6 *)cpu);
}

/* the CPU timers of the cores that wait: they wake at CCOMPARE */
static void tm_tick(void *ctx, u32 cycles)
{
    struct smp *x = (struct smp *)ctx;
    int i;
    for (i = 0; i < x->n; i++) {
        struct lx6 *c = x->c[i];
        if (x->state[i] != CORE_SLEEP)
            continue;
        timer_check(c, ccount(c) - cycles, cycles);
        if (core_wake_pending(c))
            settle(x, i);
    }
}

static int tm_next(void *ctx, u32 *cycles)
{
    struct smp *x = (struct smp *)ctx;
    int i, k, any = 0;
    for (i = 0; i < x->n; i++) {
        struct lx6 *c = x->c[i];
        if (x->state[i] != CORE_SLEEP)
            continue;
        for (k = 0; k < 3; k++) {
            u32 d = c->ccompare[k] - ccount(c);
            if (!(c->intenable & (1u << timer_int[k])) || (c->latched & (1u << timer_int[k])))
                continue;
            if (!d)
                d = 0xffffffffu;
            if (!any || d < *cycles)
                *cycles = d;
            any = 1;
        }
    }
    return any;
}

static void tm_reset(void *ctx)
{
    ES_UNUSED(ctx);
}

static const struct esk_dev_ops timer_ops = {
    "lx6-timers", 0, 0, tm_reset, tm_tick, tm_next,
};

/* ---- registers, as GDB numbers them -----------------------------------
 *
 * Espressif's xtensa-esp32-elf-gdb: pc 0, the 64 physical ARs 1-64, lbeg,
 * lend, lcount, sar, windowbase, windowstart, configid0, configid1, ps,
 * threadptr, br, scompare1, acclo, acchi, m0-m3, expstate, f64r_lo,
 * f64r_hi, f64s, f0-f15, fcr, fsr (65-104). The window's a0-a15 follow
 * here at 105-120 for a register view. */

enum { GR_PC = 0, GR_AR = 1, GR_LBEG = 65, GR_F = 87, GR_FCR = 103, GR_FSR = 104, GR_A = 105, GR_N = 121 };

static int put(u8 *b, u32 v)
{
    b[0] = (u8)v;
    b[1] = (u8)(v >> 8);
    b[2] = (u8)(v >> 16);
    b[3] = (u8)(v >> 24);
    return 4;
}

static u32 get(const u8 *b)
{
    return (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24;
}

static u32 *reg_ptr(struct lx6 *c, int n)
{
    if (n == GR_PC)
        return &c->pc;
    if (n >= GR_AR && n < GR_AR + 64)
        return &c->ar[n - GR_AR];
    if (n >= GR_A && n < GR_A + 16)
        return &AR(c, n - GR_A);
    if (n >= GR_F && n < GR_F + 16)
        return &c->f[n - GR_F];
    switch (n) {
    case 65: return &c->lbeg;
    case 66: return &c->lend;
    case 67: return &c->lcount;
    case 68: return &c->sar;
    case 69: return &c->wb;
    case 70: return &c->ws;
    case 73: return &c->ps;
    case 74: return &c->threadptr;
    case 75: return &c->br;
    case 76: return &c->scompare1;
    case 77: return &c->acclo;
    case 78: return &c->acchi;
    case 79: case 80: case 81: case 82: return &c->mr[n - 79];
    case GR_FCR: return &c->fcr;
    case GR_FSR: return &c->fsr;
    }
    switch (n) {
    case 83: return &c->expstate;
    case 84: return &c->f64r_lo;
    case 85: return &c->f64r_hi;
    case 86: return &c->f64s;
    }
    return 0;
}

static int core_reg_read(struct lx6 *c, int n, u8 *buf)
{
    u32 *p;
    if (n == 71)
        return put(buf, CONFIGID0);
    if (n == 72)
        return put(buf, CONFIGID1);
    p = reg_ptr(c, n);
    return p ? put(buf, *p) : 0;
}

static int core_reg_write(struct lx6 *c, int n, const u8 *buf)
{
    u32 *p = n == 71 || n == 72 ? 0 : reg_ptr(c, n);
    if (!p)
        return 0;
    *p = get(buf);
    c->wb &= 15u;
    c->ws &= 0xffffu;
    c->ps &= PS_MASK;
    if (n == GR_PC)
        c->npc = c->pc;
    return 4;
}

static const char *core_reg_name(int n)
{
    static const char *const a[16] = { "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
                                       "a8", "a9", "a10", "a11", "a12", "a13", "a14", "a15" };
    static const char *const sr[] = { "lbeg", "lend", "lcount", "sar", "windowbase", "windowstart",
                                      "configid0", "configid1", "ps", "threadptr", "br", "scompare1",
                                      "acclo", "acchi", "m0", "m1", "m2", "m3", "expstate",
                                      "f64r_lo", "f64r_hi", "f64s" };
    static const char *const f[16] = { "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
                                       "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15" };
    static const char *const ar[64] = {
        "ar0", "ar1", "ar2", "ar3", "ar4", "ar5", "ar6", "ar7", "ar8", "ar9", "ar10", "ar11",
        "ar12", "ar13", "ar14", "ar15", "ar16", "ar17", "ar18", "ar19", "ar20", "ar21", "ar22",
        "ar23", "ar24", "ar25", "ar26", "ar27", "ar28", "ar29", "ar30", "ar31", "ar32", "ar33",
        "ar34", "ar35", "ar36", "ar37", "ar38", "ar39", "ar40", "ar41", "ar42", "ar43", "ar44",
        "ar45", "ar46", "ar47", "ar48", "ar49", "ar50", "ar51", "ar52", "ar53", "ar54", "ar55",
        "ar56", "ar57", "ar58", "ar59", "ar60", "ar61", "ar62", "ar63" };
    if (n == GR_PC)
        return "pc";
    if (n >= GR_AR && n < GR_AR + 64)
        return ar[n - GR_AR];
    if (n >= GR_LBEG && n < GR_F)
        return sr[n - GR_LBEG];
    if (n >= GR_F && n < GR_F + 16)
        return f[n - GR_F];
    if (n == GR_FCR)
        return "fcr";
    if (n == GR_FSR)
        return "fsr";
    if (n >= GR_A && n < GR_A + 16)
        return a[n - GR_A];
    return 0;
}

static const int g_regs[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
    26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71,
    72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94,
    95, 96, 97, 98, 99, 100, 101, 102, 103, 104, -1 };

/* the disassembly: the instruction's bytes and size (the debugger and
 * the trace show the mnemonic from the ELF's own tools) */
static int core_disasm(struct lx6 *c, u32 addr, char *text, size_t cap)
{
    u32 lo, hi = 0, w;
    int len;
    if (esk_bus_debug_read(&c->m->bus, addr, 2, &lo) != 0)
        return 0;
    len = (lo & 15u) >= 8u ? 2 : 3;
    if (len == 3 && esk_bus_debug_read(&c->m->bus, addr + 2u, 1, &hi) != 0)
        return 0;
    w = (lo & 0xffffu) | (hi & 0xffu) << 16;
    snprintf(text, cap, len == 3 ? ".byte 0x%02x, 0x%02x, 0x%02x" : ".byte 0x%02x, 0x%02x",
             (unsigned)(w & 0xffu), (unsigned)((w >> 8) & 0xffu), (unsigned)((w >> 16) & 0xffu));
    return len;
}

/* the interface's hooks: the core that runs next */
static struct lx6 *cur(struct esk_cpu *cpu)
{
    struct smp *x = (struct smp *)cpu;
    return x->c[current(x)];
}

static u32 smp_pc(struct esk_cpu *cpu) { return cur(cpu)->pc; }
static int smp_reg_read(struct esk_cpu *cpu, int n, u8 *buf) { return core_reg_read(cur(cpu), n, buf); }
static int smp_reg_write(struct esk_cpu *cpu, int n, const u8 *buf) { return core_reg_write(cur(cpu), n, buf); }
static const int *smp_g_regs(struct esk_cpu *cpu) { ES_UNUSED(cpu); return g_regs; }
static const char *smp_xml(struct esk_cpu *cpu, const char *annex)
{
    ES_UNUSED(cpu);
    ES_UNUSED(annex);
    return 0;
}
static void smp_interrupt(struct esk_cpu *cpu, int n)
{
    struct smp *x = (struct smp *)cpu;
    struct lx6 *c = x->c[current(x)];
    if (n >= 16)
        n -= 16;
    if (n >= 0 && n < 32 && ((1u << n) & INT_SOFTWARE)) {
        c->latched |= 1u << n;
        settle(x, current(x));
    }
}
static const char *smp_reg_name(struct esk_cpu *cpu, int n) { ES_UNUSED(cpu); return core_reg_name(n); }
static int smp_wake_pending(struct esk_cpu *cpu)
{
    struct smp *x = (struct smp *)cpu;
    int i;
    for (i = 0; i < x->n; i++)
        if (!x->held[i] && !x->stalled[i] && core_wake_pending(x->c[i]))
            return 1;
    return 0;
}
static int smp_disasm(struct esk_cpu *cpu, u32 addr, char *text, size_t cap)
{
    return core_disasm(cur(cpu), addr, text, cap);
}

static const struct esk_cpu_ops smp_ops = {
    "xtensa", 94, "an Xtensa image",
    smp_reset, smp_step, smp_pc, smp_reg_read, smp_reg_write,
    smp_g_regs, smp_xml, 3, smp_interrupt, GR_PC, 1, 0, smp_reg_name, smp_wake_pending,
    smp_disasm, 0, 0,
};

/* each core's own interface, for the analyses' step (esk_mach_step_core) */
static u32 one_pc(struct esk_cpu *cpu) { return ((struct lx6 *)cpu)->pc; }
static int one_reg_read(struct esk_cpu *cpu, int n, u8 *buf) { return core_reg_read((struct lx6 *)cpu, n, buf); }
static int one_reg_write(struct esk_cpu *cpu, int n, const u8 *buf) { return core_reg_write((struct lx6 *)cpu, n, buf); }
static void one_reset(struct esk_cpu *cpu) { core_reset((struct lx6 *)cpu); }
static int one_wake_pending(struct esk_cpu *cpu) { return core_wake_pending((struct lx6 *)cpu); }
static int one_disasm(struct esk_cpu *cpu, u32 addr, char *text, size_t cap)
{
    return core_disasm((struct lx6 *)cpu, addr, text, cap);
}

static const struct esk_cpu_ops one_ops = {
    "xtensa", 94, "an Xtensa image",
    one_reset, one_step, one_pc, one_reg_read, one_reg_write,
    smp_g_regs, smp_xml, 3, 0, GR_PC, 1, 0, smp_reg_name, one_wake_pending,
    one_disasm, 0, 0,
};

int esk_lx6_is(const struct esk_cpu *c)
{
    return c && c->ops == &smp_ops;
}

es_status esk_lx6_create(struct esk_mach *m, const char *model, int cores, struct esk_cpu **out)
{
    struct smp *x;
    int i;
    es_status st;
    if (strcmp(model, "esp32-lx6") || cores < 1 || cores > NCORES)
        return ES_ENOTFOUND;
    x = (struct smp *)esk_mach_alloc(m, sizeof *x);
    if (!x)
        return ES_ENOMEM;
    x->cpu.ops = &smp_ops;
    x->m = m;
    x->n = cores;
    for (i = 0; i < cores; i++) {
        struct lx6 *c = (struct lx6 *)esk_mach_alloc(m, sizeof *c);
        if (!c)
            return ES_ENOMEM;
        c->cpu.ops = &one_ops;
        c->m = m;
        c->x = x;
        c->id = i;
        c->prid = i == 0 ? 0xCDCDu : 0xABABu;
        c->reset_vector = 1;
        x->c[i] = c;
    }
    st = esk_mach_add_dev(m, 0, 0, &timer_ops, x);
    if (st != ES_OK)
        return st;
    esk_mach_clock(m, &x->running, 1); /* the CPU timers' clock never stops */
    m->steps_cores = 1;
    m->cpu = &x->cpu;
    smp_reset(&x->cpu);
    *out = &x->cpu;
    return ES_OK;
}

/* ---- the part's controls ---------------------------------------------- */

int esk_lx6_cores(struct esk_mach *m)
{
    struct smp *x = smp_of(m);
    return x ? x->n : 0;
}

int esk_lx6_core(struct esk_mach *m)
{
    struct smp *x = smp_of(m);
    return x ? current(x) : 0;
}

void esk_lx6_irq(struct esk_mach *m, int core, int line, int level)
{
    struct smp *x = smp_of(m);
    struct lx6 *c;
    u32 bit;
    if (!x || core < 0 || core >= x->n || line < 0 || line > 31)
        return;
    c = x->c[core];
    bit = 1u << line;
    if (level) {
        if (!(c->lines & bit) && (bit & (INT_EDGE | INT_NMI)))
            c->latched |= bit;
        c->lines |= bit;
    } else {
        c->lines &= ~bit;
    }
    if (x->state[core] == CORE_SLEEP && core_wake_pending(c))
        settle(x, core);
}

void esk_lx6_core_reset(struct esk_mach *m, int core, int held)
{
    struct smp *x = smp_of(m);
    if (!x || core < 0 || core >= x->n || x->held[core] == (held != 0))
        return;
    x->held[core] = held != 0;
    if (!held) {
        core_reset(x->c[core]);
        x->lt[core] = m->cycles;
    }
    settle(x, core);
}

void esk_lx6_core_stall(struct esk_mach *m, int core, int why, int stalled)
{
    struct smp *x = smp_of(m);
    if (!x || core < 0 || core >= x->n || why < 0 || why > 7)
        return;
    if (stalled)
        x->stalled[core] |= 1 << why;
    else
        x->stalled[core] &= ~(1 << why);
    settle(x, core);
}

void esk_lx6_core_restart(struct esk_mach *m, int core)
{
    struct smp *x = smp_of(m);
    if (!x || core < 0 || core >= x->n)
        return;
    core_reset(x->c[core]);
    x->lt[core] = m->cycles;
    if (x->stepping && x->cur == core)
        x->c[core]->exc = 3; /* the instruction in progress is gone with it */
    settle(x, core);
}

int esk_lx6_view(struct esk_mach *m, int core, u32 base, u32 size, const u8 *mem)
{
    struct smp *x = smp_of(m);
    struct lx6 *c;
    if (!x || core < 0 || core >= x->n)
        return -1;
    c = x->c[core];
    if (c->nview >= 2)
        return -1;
    c->view[c->nview].base = base;
    c->view[c->nview].size = size;
    c->view[c->nview].mem = mem;
    c->nview++;
    return 0;
}

void esk_lx6_reset_vector(struct esk_mach *m, int core, int sel)
{
    struct smp *x = smp_of(m);
    if (x && core >= 0 && core < x->n)
        x->c[core]->reset_vector = sel != 0;
}

u32 esk_lx6_pc(struct esk_mach *m, int core)
{
    struct smp *x = smp_of(m);
    return x && core >= 0 && core < x->n ? x->c[core]->pc : 0;
}

u32 esk_lx6_sp(struct esk_mach *m, int core)
{
    struct smp *x = smp_of(m);
    return x && core >= 0 && core < x->n ? AR(x->c[core], 1) : 0;
}

/* ---- for lx6fp.c ------------------------------------------------------ */

u32 *esk_lx6_fregs(struct lx6 *c) { return c->f; }
u32 *esk_lx6_fcr(struct lx6 *c) { return &c->fcr; }
u32 *esk_lx6_fsr(struct lx6 *c) { return &c->fsr; }
u32 esk_lx6_fp_rd(struct lx6 *c, int n) { return c->f[n & 15]; }
void esk_lx6_fp_wr(struct lx6 *c, int n, u32 v) { c->f[n & 15] = v; }
u32 esk_lx6_ar(struct lx6 *c, int n) { return AR(c, (u32)n); }
void esk_lx6_set_ar(struct lx6 *c, int n, u32 v) { AR(c, (u32)n) = v; }
u32 esk_lx6_br(struct lx6 *c) { return c->br; }
void esk_lx6_set_br(struct lx6 *c, u32 v) { c->br = v & 0xffffu; }
int esk_lx6_win(struct lx6 *c, u32 hi) { return win(c, hi); }
