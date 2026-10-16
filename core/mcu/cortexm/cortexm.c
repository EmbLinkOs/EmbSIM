/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* cortexm.c -- the Cortex-M core: ARMv6-M and ARMv7-M (SPEC-009 §3).
 *
 * The core fetches its stack pointer and reset vector from the vector
 * table, and executes Thumb (cortexm-thumb.c) with the floating-point
 * unit of the M4 and M7 (cortexm-fpu.c). Exceptions are architectural:
 * fault escalation and lockup, SVC, PendSV, SysTick and the NVIC's
 * interrupts, with the stacked frame and EXC_RETURN.
 *
 * This file is the core's state and what is not an instruction: memory
 * access and its faults, the program status, exception entry and return,
 * one step, reset, and the CPU interface (struct esk_cpu_ops) the run loop
 * and the GDB server drive it through. Its own devices -- the system
 * control space (scs.c), SysTick (systick.c) and DWT (dwt.c) -- are on
 * the bus like any other, added when the core is.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/cortexm.c. Changes for
 * EmbSIM: the core state is a parameter (no file-scope `cs`); WFI, WFE and
 * a branch to itself put the machine to sleep (esk_mach.asleep) instead of
 * skipping time or ending the run, so the MCU runtime decides how far to
 * fast-forward against the session's other events (ADR-019); the cycles
 * of a taken branch reach the devices as well as the core's count; an
 * unreadable vector table locks the core up instead of exiting. */
#include <stdio.h>
#include <string.h>

#include "mcu/cortexm/cost.h"
#include "mcu/cortexm/cortexm.h"

static const struct cpu_model cpus[] = {
    { "cortex-m0", ARCH_V6M, 0, FPU_NONE, 0x410CC200u },
    { "cortex-m0plus", ARCH_V6M, 0, FPU_NONE, 0x410CC601u },
    { "cortex-m3", ARCH_V7M, 0, FPU_NONE, 0x412FC231u },
    { "cortex-m4", ARCH_V7M, 1, FPU_SP, 0x410FC241u },
    { "cortex-m7", ARCH_V7M, 1, FPU_DP, 0x411FC272u },
};

/* the rest of the private peripheral bus reads as zero and ignores writes */
static u32 zero_rd(void *ctx, u32 off, int n)
{
    (void)ctx;
    (void)off;
    (void)n;
    return 0;
}

static const struct esk_dev_ops zero_ops = { "zero", zero_rd, 0, 0, 0, 0 };


/* ---- faults ----------------------------------------------------------- */

static void bus_error(struct esk_cm_state *cs, u32 a, int ifetch)
{
    if (cs->fault_exc)
        return;
    esk_cm_fault_raise(cs, EXC_BUS, ifetch ? BFSR_IBUSERR : BFSR_PRECISERR);
    cs->fault_addr = a;
    cs->fault_addr_valid = !ifetch;
}

void esk_cm_undef(struct esk_cm_state *cs)
{
    esk_cm_fault_raise(cs, EXC_USAGE, UFSR_UNDEFINSTR);
}

/* ---- the NVIC's view of the exceptions -------------------------------- */

void esk_cm_set_pend(struct esk_cm_state *cs, int n)
{
    if (n > 0 && n < NEXC && !cs->pend[n]) {
        cs->pend[n] = 1;
        cs->npend++;
    }
}

void esk_cm_clr_pend(struct esk_cm_state *cs, int n)
{
    if (n > 0 && n < NEXC && cs->pend[n]) {
        cs->pend[n] = 0;
        cs->npend--;
    }
}

u32 esk_cm_prio_mask(struct esk_cm_state *cs)
{
    return (0xffu << (8 - cs->prio_bits)) & 0xffu;
}

/* An exception's group priority, as a number to compare: lower is more
 * urgent; Reset, NMI and HardFault have the fixed -3, -2 and -1. */
static int group_prio(struct esk_cm_state *cs, int n)
{
    if (n == EXC_RESET)
        return -3;
    if (n == EXC_NMI)
        return -2;
    if (n == EXC_HARD)
        return -1;
    u32 p = cs->prio[n] & esk_cm_prio_mask(cs);
    u32 sub = cs->model->arch == ARCH_V6M ? 0 : cs->aircr_prigroup + 1;
    if (sub > 8)
        sub = 8;
    return (int)((p >> sub) << sub);
}

int esk_cm_exec_prio(struct esk_cm_state *cs)
{
    int best = 256;
    for (int n = 1; n < NEXC; n++)
        if (cs->active[n] && group_prio(cs, n) < best)
            best = group_prio(cs, n);
    if (cs->model->arch == ARCH_V7M && (cs->basepri & esk_cm_prio_mask(cs))) {
        u32 sub = cs->aircr_prigroup + 1;
        int b = sub > 7 ? 0
                        : (int)(((cs->basepri & esk_cm_prio_mask(cs)) >> sub) << sub);
        if (b < best)
            best = b;
    }
    if (cs->primask & 1)
        best = best < 0 ? best : 0;
    if (cs->faultmask & 1)
        best = best < -1 ? best : -1;
    return best;
}

/* ---- memory ----------------------------------------------------------- */

/* A watchpoint an instruction's access touches stops the instruction
 * before the access, as a fault does, and QEMU's stub: the debugger then
 * steps it with the watchpoint out. An access of exception entry or
 * return is made, and the stop comes after it. */
static int watched(struct esk_cm_state *cs, u32 a, int n, int write)
{
    struct esk_bus *b = &cs->sim->bus;
    if (!b->nwatch || !esk_bus_watch_check(b, a, n, write) || !cs->executing)
        return 0;
    cs->fault_exc = FAULT_WATCH;
    return 1;
}

/* what is not plain memory, or is watched: through the bus */
u32 esk_cm_ld_bus(struct esk_cm_state *cs, u32 a, int n)
{
    u32 v;
    if (watched(cs, a, n, 0))
        return 0;
    if (esk_bus_read(&cs->sim->bus, a, n, &v) == 0)
        return v;
    bus_error(cs, cs->sim->bus.fail_addr, 0);
    return 0;
}

void esk_cm_st_bus(struct esk_cm_state *cs, u32 a, int n, u32 v)
{
    if (watched(cs, a, n, 1))
        return;
    if (esk_bus_write(&cs->sim->bus, a, n, v))
        bus_error(cs, cs->sim->bus.fail_addr, 0);
}

/* a store to the address LDREX marked clears the local monitor; the bus
 * calls this for each store, a bit-band alias's target included */
static void excl_snoop(void *ctx, u32 a)
{
    struct esk_cm_state *c = ctx;
    if (c->excl_valid && a - c->excl_addr < 4)
        c->excl_valid = 0;
}

/* ---- registers, flags and the program counter ------------------------ */

void esk_cm_bx_to(struct esk_cm_state *cs, u32 a)                    /* BXWritePC, LoadWritePC */
{
    if (cs->ipsr && (a >> 28) == 0xF) {
        cs->exc_return_pending = 1;
        cs->exc_return_value = a;
        cs->pc_written = 1;
        return;
    }
    cs->tbit = (int)(a & 1);
    cs->npc = a & ~1u;
    cs->pc_written = 1;
}

int esk_cm_cond_passed(struct esk_cm_state *cs, u32 cond)
{
    int r;
    switch (cond >> 1) {
    case 0: r = cs->Z; break;
    case 1: r = cs->C; break;
    case 2: r = cs->N; break;
    case 3: r = cs->V; break;
    case 4: r = cs->C && !cs->Z; break;
    case 5: r = cs->N == cs->V; break;
    case 6: r = cs->N == cs->V && !cs->Z; break;
    default: r = 1; break;
    }
    if ((cond & 1) && cond != 15)
        r = !r;
    return r;
}

void esk_cm_it_advance(struct esk_cm_state *cs)
{
    if ((cs->itstate & 7) == 0)
        cs->itstate = 0;
    else
        cs->itstate = (cs->itstate & 0xe0) | ((cs->itstate << 1) & 0x1f);
}

u32 esk_cm_xpsr(struct esk_cm_state *cs)
{
    u32 v = (u32)cs->N << 31 | (u32)cs->Z << 30 | (u32)cs->C << 29 |
            (u32)cs->V << 28 | (u32)cs->Q << 27 | (u32)cs->tbit << 24 |
            (cs->ipsr & 0x1ff);
    if (cs->model->dsp)
        v |= cs->GE << 16;
    v |= (cs->itstate & 3) << 25 | (cs->itstate >> 2) << 10;
    return v;
}

void esk_cm_set_apsr(struct esk_cm_state *cs, u32 v)
{
    cs->N = (int)(v >> 31 & 1);
    cs->Z = (int)(v >> 30 & 1);
    cs->C = (int)(v >> 29 & 1);
    cs->V = (int)(v >> 28 & 1);
    cs->Q = (int)(v >> 27 & 1);
}

int esk_cm_privileged(struct esk_cm_state *cs)
{
    return cs->ipsr != 0 || !(cs->control & 1);
}

/* switch the active stack, keeping the other banked */
void esk_cm_use_psp(struct esk_cm_state *cs, int psp)
{
    if (psp == cs->psp_active)
        return;
    u32 t = cs->R[13];
    cs->R[13] = cs->other_sp;
    cs->other_sp = t;
    cs->psp_active = psp;
}

u32 esk_cm_get_msp(struct esk_cm_state *cs)
{
    return cs->psp_active ? cs->other_sp : cs->R[13];
}

u32 esk_cm_get_psp(struct esk_cm_state *cs)
{
    return cs->psp_active ? cs->R[13] : cs->other_sp;
}

void esk_cm_set_msp(struct esk_cm_state *cs, u32 v)
{
    if (cs->psp_active)
        cs->other_sp = v & ~3u;
    else
        cs->R[13] = v & ~3u;
}

void esk_cm_set_psp(struct esk_cm_state *cs, u32 v)
{
    if (cs->psp_active)
        cs->R[13] = v & ~3u;
    else
        cs->other_sp = v & ~3u;
}

/* ---- exceptions ------------------------------------------------------ */

static void lockup(struct esk_cm_state *cs, const char *why)
{
    esk_mach_end(cs->sim, ESK_MACH_LOCKUP, "lockup: %s at 0x%08x", why, cs->pc);
}

/* ExceptionEntry: push the frame onto the stack in use, and go to the
 * handler. `ret` is the address the frame returns to. */
static void exc_entry(struct esk_cm_state *cs, int n, u32 ret)
{
    int fp_frame = cs->model->fpu && (cs->control & 4);
    u32 frame = fp_frame ? 0x68 : 0x20;
    /* PushStack: with CCR.STKALIGN (always on ARMv6-M) the frame is
     * 8-aligned, and xPSR bit 9 records that 4 bytes were skipped */
    int force = cs->model->arch == ARCH_V6M || (cs->ccr & (1u << 9));
    u32 align = force ? (cs->R[13] >> 2 & 1) : 0;
    u32 sp = (cs->R[13] - frame) & ~(force ? 4u : 0u);
    u32 x = esk_cm_xpsr(cs) & ~(1u << 9);
    if (align)
        x |= 1u << 9;
    esk_cm_st(cs, sp, 4, cs->R[0]);
    esk_cm_st(cs, sp + 4, 4, cs->R[1]);
    esk_cm_st(cs, sp + 8, 4, cs->R[2]);
    esk_cm_st(cs, sp + 12, 4, cs->R[3]);
    esk_cm_st(cs, sp + 16, 4, cs->R[12]);
    esk_cm_st(cs, sp + 20, 4, cs->R[14]);
    esk_cm_st(cs, sp + 24, 4, ret);
    esk_cm_st(cs, sp + 28, 4, x);
    if (fp_frame) {
        for (int i = 0; i < 16; i++)
            esk_cm_st(cs, sp + 32 + 4 * (u32)i, 4, cs->S[i]);
        esk_cm_st(cs, sp + 96, 4, cs->fpscr);
    }
    if (cs->fault_exc) {
        /* a stacking fault: give up on the exception rather than model
         * the derived fault's own stacking */
        cs->fault_exc = 0;
        lockup(cs, "a fault while stacking an exception frame");
        return;
    }
    cs->R[13] = sp;
    u32 lr;
    if (cs->ipsr)
        lr = 0xFFFFFFF1u;
    else
        lr = cs->psp_active ? 0xFFFFFFFDu : 0xFFFFFFF9u;
    if (fp_frame)
        lr &= ~0x10u;
    cs->R[14] = lr;
    esk_cm_use_psp(cs, 0);
    cs->ipsr = (u32)n;
    cs->active[n] = 1;
    esk_cm_clr_pend(cs, n);
    cs->itstate = 0;
    cs->excl_valid = 0;
    if (cs->model->fpu)
        cs->control &= ~4u;
    u32 v = esk_cm_ld(cs, cs->vtor + 4 * (u32)n, 4);
    if (cs->fault_exc) {
        cs->fault_exc = 0;
        lockup(cs, "the vector table cannot be read");
        return;
    }
    if (!(v & 1)) {
        /* a vector without the Thumb bit: the first instruction would
         * take an INVSTATE UsageFault */
        cs->tbit = 0;
    } else {
        cs->tbit = 1;
    }
    cs->pc = v & ~1u;
}

/* A synchronous fault, or SVC: taken at once, escalated to HardFault when
 * its own handler is disabled or cannot preempt, and a lockup when even
 * HardFault cannot. */
static void take_sync(struct esk_cm_state *cs, int n, u32 bits, u32 ret)
{
    int ep = esk_cm_exec_prio(cs);
    if (n != EXC_HARD) {
        int enabled = 1;
        if (n == EXC_MEM)
            enabled = (cs->shcsr >> 16) & 1;
        else if (n == EXC_BUS)
            enabled = (cs->shcsr >> 17) & 1;
        else if (n == EXC_USAGE)
            enabled = (cs->shcsr >> 18) & 1;
        if (n != EXC_SVC)
            cs->cfsr |= bits;
        if (!enabled || group_prio(cs, n) >= ep) {
            if (cs->model->arch == ARCH_V7M)
                cs->hfsr |= 1u << 30;       /* FORCED */
            n = EXC_HARD;
        }
    }
    if (n == EXC_HARD && ep <= -1) {
        lockup(cs, "a fault with HardFault already active");
        return;
    }
    exc_entry(cs, n, ret);
}

/* the most urgent pending exception that can preempt now, or 0 */
static int pending_to_take(struct esk_cm_state *cs)
{
    int best = 0, bp = 0;
    for (int n = 2; n < NEXC; n++) {
        if (!cs->pend[n] || (n >= 16 && !cs->irq_en[n]))
            continue;
        int p = group_prio(cs, n);
        if (!best || p < bp) {
            best = n;
            bp = p;
        }
    }
    if (best && bp < esk_cm_exec_prio(cs))
        return best;
    return 0;
}

static void exc_return(struct esk_cm_state *cs, u32 ret)
{
    int n = (int)cs->ipsr;
    cs->active[n] = 0;
    if (n == EXC_HARD || n == EXC_NMI)
        cs->faultmask = 0;
    int to_thread = (ret & 8) != 0;
    int psp = (ret & 4) != 0;
    int fp_frame = cs->model->fpu && !(ret & 0x10);
    if (!to_thread && psp) {
        take_sync(cs, EXC_USAGE, UFSR_INVPC, cs->pc);
        return;
    }
    esk_cm_use_psp(cs, to_thread && psp);
    if (to_thread)
        cs->control = (cs->control & ~2u) | (psp ? 2u : 0u);
    u32 sp = cs->R[13];
    cs->R[0] = esk_cm_ld(cs, sp, 4);
    cs->R[1] = esk_cm_ld(cs, sp + 4, 4);
    cs->R[2] = esk_cm_ld(cs, sp + 8, 4);
    cs->R[3] = esk_cm_ld(cs, sp + 12, 4);
    cs->R[12] = esk_cm_ld(cs, sp + 16, 4);
    cs->R[14] = esk_cm_ld(cs, sp + 20, 4);
    u32 ra = esk_cm_ld(cs, sp + 24, 4);
    u32 x = esk_cm_ld(cs, sp + 28, 4);
    u32 frame = 0x20;
    if (fp_frame) {
        for (int i = 0; i < 16; i++)
            cs->S[i] = esk_cm_ld(cs, sp + 32 + 4 * (u32)i, 4);
        cs->fpscr = esk_cm_ld(cs, sp + 96, 4);
        frame = 0x68;
    }
    if (cs->fault_exc) {
        cs->fault_exc = 0;
        lockup(cs, "a fault while unstacking an exception frame");
        return;
    }
    cs->R[13] = (sp + frame) | ((x >> 9 & 1) ? 4 : 0);
    if (cs->model->fpu)
        cs->control = (cs->control & ~4u) | (fp_frame ? 4u : 0u);
    esk_cm_set_apsr(cs, x);
    if (cs->model->dsp)
        cs->GE = x >> 16 & 15;
    cs->itstate = (x >> 25 & 3) | (x >> 8 & 0xfc);
    cs->tbit = (int)(x >> 24 & 1);
    cs->ipsr = to_thread ? 0 : (x & 0x1ff);
    cs->excl_valid = 0;
    cs->pc = ra & ~1u;
}

/* ---- semihosting ------------------------------------------------------ */

static u32 semi_ld(void *ctx, u32 a, int n)
{
    struct esk_cm_state *cs = (struct esk_cm_state *)ctx;
    return esk_cm_ld(cs, a, n);
}

static void semi_st(void *ctx, u32 a, int n, u32 v)
{
    struct esk_cm_state *cs = (struct esk_cm_state *)ctx;
    esk_cm_st(cs, a, n, v);
}

static int semi_faulted(void *ctx)
{
    const struct esk_cm_state *cs = (const struct esk_cm_state *)ctx;
    return cs->fault_exc != 0;
}

static const struct esk_semi_ops semi_mem = { semi_ld, semi_st, semi_faulted };

/* `bkpt 0xab`: the operation in r0, its parameter in r1 */
void esk_cm_semihost(struct esk_cm_state *cs)
{
    u32 r0;
    if (esk_semihost(cs->sim, &semi_mem, cs, cs->R[0], cs->R[1], &r0))
        cs->R[0] = r0;
}

/* ---- the run --------------------------------------------------------- */

/* an exception is pending that would end a wait */
static int wake_pending(const struct esk_cm_state *cs)
{
    int n;
    if (!cs->npend)
        return 0;
    for (n = 2; n < NEXC; n++)
        if (cs->pend[n] && (n < 16 || cs->irq_en[n]))
            return 1;
    return 0;
}

/* WFI and WFE: wait for an exception. The machine sleeps; the MCU runtime
 * fast-forwards its devices to their next event, or waits for one from
 * outside, and the interrupt that comes wakes it (cpu_interrupt). */
int esk_cm_wfx_idle(struct esk_cm_state *cs)
{
    if (wake_pending(cs))
        return 0;
    cs->sim->asleep = 1;
    return 1;
}

/* A halfword of code: from memory, or from a device that answers there (a
 * ROM: the RP2040's, rp2040.c). 0 when nothing does. */
static int fetch16(struct esk_mach *s, u32 a, u8 *b)
{
    struct esk_region *r = esk_bus_region(&s->bus, a, 2);
    if (r) {
        memcpy(b, r->mem + (a - r->base), 2);
        return 1;
    }
    u32 v;
    if (esk_bus_read(&s->bus, a, 2, &v))
        return 0;
    b[0] = (u8)v;
    b[1] = (u8)(v >> 8);
    return 1;
}

static void step(struct esk_cpu *c)
{
    struct esk_cm_state *cs = (struct esk_cm_state *)c;
    struct esk_mach *s = cs->sim;
    u32 snap[16];
    int sN = cs->N, sZ = cs->Z, sC = cs->C, sV = cs->V, sQ = cs->Q;
    u32 sit = cs->itstate, sctl = cs->control, sother = cs->other_sp;
    int spsp = cs->psp_active;

    if (cs->npend) {
        int n = pending_to_take(cs);
        if (n) {
            exc_entry(cs, n, cs->pc);
            return;
        }
    }
    if (!cs->tbit) {
        take_sync(cs, EXC_USAGE, UFSR_INVSTATE, cs->pc);
        return;
    }
    u8 b[4];
    if (!fetch16(s, cs->pc, b)) {
        take_sync(cs, EXC_BUS, BFSR_IBUSERR, cs->pc);
        return;
    }
    u32 h[2];
    h[0] = (u32)b[0] | (u32)b[1] << 8;
    h[1] = 0;
    int size = 2;
    if ((h[0] >> 11) >= 0x1d) {
        if (!fetch16(s, cs->pc + 2, b + 2)) {
            take_sync(cs, EXC_BUS, BFSR_IBUSERR, cs->pc);
            return;
        }
        h[1] = (u32)b[2] | (u32)b[3] << 8;
        size = 4;
    }
    int br;
    u32 cost = (u32)esk_cm_cost(b, (size_t)size, &br);
    s->insns++;
    s->cycles += cost;

    memcpy(snap, cs->R, sizeof snap);
    cs->npc = cs->pc + (u32)size;
    cs->pc_written = 0;
    cs->exc_return_pending = 0;
    cs->fault_exc = 0;
    int is_it = size == 2 && (h[0] & 0xff00) == 0xbf00 && (h[0] & 15);
    int was_in_it = esk_cm_in_it(cs);
    int run_it = !was_in_it || esk_cm_cond_passed(cs, cs->itstate >> 4);
    if (run_it) {
        cs->executing = 1;
        if (size == 2)
            esk_cm_exec16(cs, h[0]);
        else
            esk_cm_exec32(cs, h[0], h[1]);
        cs->executing = 0;
    }
    if (cs->fault_exc) {
        int exc = cs->fault_exc;
        u32 bits = cs->fault_bits;
        memcpy(cs->R, snap, sizeof snap);
        cs->N = sN; cs->Z = sZ; cs->C = sC; cs->V = sV; cs->Q = sQ;
        cs->itstate = sit;
        if (cs->control != sctl || cs->psp_active != spsp) {
            esk_cm_use_psp(cs, spsp);
            cs->control = sctl;
        }
        cs->other_sp = sother;
        cs->fault_exc = 0;
        if (exc == FAULT_WATCH) {
            /* not run: the debugger stops here, and the instruction is
             * counted when it does run (its --trace line comes again) */
            s->insns--;
            s->cycles -= cost;
            return;
        }
        if (exc == EXC_BUS && cs->fault_addr_valid) {
            cs->bfar = cs->fault_addr;
            bits |= 1u << 15;
        }
        if (exc == EXC_SVC) {
            /* SVC completes: it returns past itself */
            if (was_in_it && !is_it)
                esk_cm_it_advance(cs);
            take_sync(cs, EXC_SVC, 0, cs->npc);
            return;
        }
        take_sync(cs, exc, bits, cs->pc);
        return;
    }
    if (was_in_it && !is_it)
        esk_cm_it_advance(cs);
    if (br && cs->npc != cs->pc + (u32)size) {
        s->cycles += 2;
        cost += 2;
    }
    if (s->counting)
        esk_mach_advance(s, cost);
    if (cs->exc_return_pending) {
        exc_return(cs, cs->exc_return_value);
        return;
    }
    if (cs->pc_written && cs->npc == cs->pc && !esk_cm_in_it(cs) &&
        s->state == ESK_MACH_RUN && memcmp(snap, cs->R, 15 * sizeof cs->R[0]) == 0) {
        /* a branch to itself that changed nothing else waits, as WFI
         * does, for an exception. A `pop {.., pc}` that lands on itself is
         * not one -- a recursive function's last frame returns to the
         * same pop in its caller, with sp moved on -- and nor is any
         * jump to itself that wrote a register. */
        if (!wake_pending(cs))
            s->asleep = 1;
    }
    cs->pc = cs->npc;
}

/* ---- the CPU interface ------------------------------------------------- */

/* Power-on: every register as the part has it out of reset, then the
 * reset sequence, which reads the stack pointer and the reset vector
 * from the vector table at 0. */
static void cpu_reset(struct esk_cpu *c)
{
    struct esk_cm_state *k = (struct esk_cm_state *)c;
    struct esk_cpu keep = k->cpu;
    struct esk_mach *s = k->sim;
    const struct cpu_model *model = k->model;
    int prio_bits = k->prio_bits;
    memset(k, 0, sizeof *k);
    k->cpu = keep;
    k->sim = s;
    k->model = model;
    k->prio_bits = prio_bits;
    k->tbit = 1;
    k->fpccr = 0xC0000000u;
    struct esk_cm_state *cs = k;

    cs->ccr = cs->model->arch == ARCH_V7M ? 0x200u : 0x208u;   /* STKALIGN */
    cs->vtor = 0;
    u32 sp = esk_cm_ld(cs, 0, 4), entry = esk_cm_ld(cs, 4, 4);
    if (cs->fault_exc) {
        cs->fault_exc = 0;
        esk_mach_end(s, ESK_MACH_LOCKUP, "lockup: the vector table at 0 cannot be read");
        return;
    }
    cs->R[13] = sp & ~3u;
    cs->R[14] = 0xffffffffu;
    cs->tbit = (int)(entry & 1);
    cs->pc = entry & ~1u;
    for (int n = 4; n < NEXC; n++)
        cs->prio[n] = 0;
}

static u32 cpu_pc(struct esk_cpu *c)
{
    return ((struct esk_cm_state *)c)->pc;
}

static void cpu_interrupt(struct esk_cpu *c, int n)
{
    struct esk_cm_state *cs = (struct esk_cm_state *)c;
    esk_cm_set_pend(cs, n);
    if (cs->sim->asleep && wake_pending(cs))
        cs->sim->asleep = 0;
}

static int cpu_wake_pending(struct esk_cpu *c)
{
    return wake_pending((const struct esk_cm_state *)c);
}

/* ---- the registers, as GDB numbers them --------------------------------
 *
 * The numbering of QEMU's stub, so a debugger sees the same machine on
 * either: r0-r12, sp, lr and pc are 0-15 and xpsr is 25
 * (org.gnu.gdb.arm.m-profile); with an FPU, d0-d15 are 26-41 and fpscr
 * 42 (org.gnu.gdb.arm.vfp); then the system registers msp, psp,
 * primask and control, and on ARMv7-M basepri and faultmask
 * (org.gnu.gdb.arm.m-system). A `g` packet carries r0-pc and xpsr. */

enum { GR_XPSR = 25, GR_D0 = 26, GR_FPSCR = 42 };

static int sys_base(const struct esk_cm_state *k)
{
    return k->model->fpu ? GR_FPSCR + 1 : GR_XPSR + 1;
}

static void put32(u8 *b, u32 v)
{
    b[0] = (u8)v;
    b[1] = (u8)(v >> 8);
    b[2] = (u8)(v >> 16);
    b[3] = (u8)(v >> 24);
}

static u32 get32(const u8 *b)
{
    return (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24;
}

static int cpu_reg_read(struct esk_cpu *c, int n, u8 *buf)
{
    struct esk_cm_state *cs = (struct esk_cm_state *)c;
    if (n >= 0 && n < 16) {
        put32(buf, n == 15 ? cs->pc : cs->R[n]);
        return 4;
    }
    if (n == GR_XPSR) {
        put32(buf, esk_cm_xpsr(cs));
        return 4;
    }
    if (cs->model->fpu && n >= GR_D0 && n < GR_D0 + 16) {
        put32(buf, cs->S[2 * (n - GR_D0)]);
        put32(buf + 4, cs->S[2 * (n - GR_D0) + 1]);
        return 8;
    }
    if (cs->model->fpu && n == GR_FPSCR) {
        put32(buf, cs->fpscr);
        return 4;
    }
    int v7 = cs->model->arch == ARCH_V7M;
    switch (n - sys_base(cs)) {
    case 0: put32(buf, esk_cm_get_msp(cs)); return 4;
    case 1: put32(buf, esk_cm_get_psp(cs)); return 4;
    case 2: put32(buf, cs->primask); return 4;
    case 3: put32(buf, cs->control & (cs->model->fpu ? 7u : 3u)); return 4;
    case 4: if (!v7) return 0; put32(buf, cs->basepri); return 4;
    case 5: if (!v7) return 0; put32(buf, cs->faultmask); return 4;
    }
    return 0;
}

static int cpu_reg_write(struct esk_cpu *c, int n, const u8 *buf)
{
    struct esk_cm_state *cs = (struct esk_cm_state *)c;
    u32 v = get32(buf);
    if (n >= 0 && n < 16) {
        if (n == 15)
            cs->pc = v & ~1u;
        else if (n == 13)
            cs->R[13] = v & ~3u;
        else
            cs->R[n] = v;
        return 4;
    }
    if (n == GR_XPSR) {
        /* the flags, the IT bits and the T bit; IPSR is read-only */
        esk_cm_set_apsr(cs, v);
        if (cs->model->dsp)
            cs->GE = v >> 16 & 15;
        cs->itstate = (v >> 25 & 3) | (v >> 8 & 0xfc);
        cs->tbit = (int)(v >> 24 & 1);
        return 4;
    }
    if (cs->model->fpu && n >= GR_D0 && n < GR_D0 + 16) {
        cs->S[2 * (n - GR_D0)] = v;
        cs->S[2 * (n - GR_D0) + 1] = get32(buf + 4);
        return 8;
    }
    if (cs->model->fpu && n == GR_FPSCR) {
        cs->fpscr = v & 0xF7C0009Fu;
        return 4;
    }
    int v7 = cs->model->arch == ARCH_V7M;
    switch (n - sys_base(cs)) {
    case 0: esk_cm_set_msp(cs, v); return 4;
    case 1: esk_cm_set_psp(cs, v); return 4;
    case 2: cs->primask = v & 1; return 4;
    case 3: {
        u32 mask = cs->model->fpu ? 7u : 3u;
        if (!v7)
            mask &= ~1u;
        cs->control = v & mask;
        if (!cs->ipsr)
            esk_cm_use_psp(cs, (cs->control & 2) != 0);
        return 4;
    }
    case 4: if (!v7) return 0; cs->basepri = v & 0xff; return 4;
    case 5: if (!v7) return 0; cs->faultmask = v & 1; return 4;
    }
    return 0;
}

static const int g_regs[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
                              14, 15, GR_XPSR, -1 };

static const int *cpu_gdb_g_regs(struct esk_cpu *c)
{
    (void)c;
    return g_regs;
}

/* The target description: one document, the three features GDB knows
 * for an M-profile core. */
static const char xml_head[] =
    "<?xml version=\"1.0\"?>\n"
    "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
    "<target version=\"1.0\">\n"
    "<architecture>arm</architecture>\n"
    "<feature name=\"org.gnu.gdb.arm.m-profile\">\n"
    "<reg name=\"r0\" bitsize=\"32\"/>\n"
    "<reg name=\"r1\" bitsize=\"32\"/>\n"
    "<reg name=\"r2\" bitsize=\"32\"/>\n"
    "<reg name=\"r3\" bitsize=\"32\"/>\n"
    "<reg name=\"r4\" bitsize=\"32\"/>\n"
    "<reg name=\"r5\" bitsize=\"32\"/>\n"
    "<reg name=\"r6\" bitsize=\"32\"/>\n"
    "<reg name=\"r7\" bitsize=\"32\"/>\n"
    "<reg name=\"r8\" bitsize=\"32\"/>\n"
    "<reg name=\"r9\" bitsize=\"32\"/>\n"
    "<reg name=\"r10\" bitsize=\"32\"/>\n"
    "<reg name=\"r11\" bitsize=\"32\"/>\n"
    "<reg name=\"r12\" bitsize=\"32\"/>\n"
    "<reg name=\"sp\" bitsize=\"32\" type=\"data_ptr\"/>\n"
    "<reg name=\"lr\" bitsize=\"32\"/>\n"
    "<reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>\n"
    "<reg name=\"xpsr\" bitsize=\"32\" regnum=\"25\"/>\n"
    "</feature>\n";

static const char xml_vfp[] =
    "<feature name=\"org.gnu.gdb.arm.vfp\">\n"
    "<reg name=\"d0\" bitsize=\"64\" type=\"float\" regnum=\"26\"/>\n"
    "<reg name=\"d1\" bitsize=\"64\" type=\"float\"/>\n"
    "<reg name=\"d2\" bitsize=\"64\" type=\"float\"/>\n"
    "<reg name=\"d3\" bitsize=\"64\" type=\"float\"/>\n"
    "<reg name=\"d4\" bitsize=\"64\" type=\"float\"/>\n"
    "<reg name=\"d5\" bitsize=\"64\" type=\"float\"/>\n"
    "<reg name=\"d6\" bitsize=\"64\" type=\"float\"/>\n"
    "<reg name=\"d7\" bitsize=\"64\" type=\"float\"/>\n"
    "<reg name=\"d8\" bitsize=\"64\" type=\"float\"/>\n"
    "<reg name=\"d9\" bitsize=\"64\" type=\"float\"/>\n"
    "<reg name=\"d10\" bitsize=\"64\" type=\"float\"/>\n"
    "<reg name=\"d11\" bitsize=\"64\" type=\"float\"/>\n"
    "<reg name=\"d12\" bitsize=\"64\" type=\"float\"/>\n"
    "<reg name=\"d13\" bitsize=\"64\" type=\"float\"/>\n"
    "<reg name=\"d14\" bitsize=\"64\" type=\"float\"/>\n"
    "<reg name=\"d15\" bitsize=\"64\" type=\"float\"/>\n"
    "<reg name=\"fpscr\" bitsize=\"32\" type=\"int\" group=\"float\"/>\n"
    "</feature>\n";

static const char *const sys_names[] = { "msp", "psp", "primask", "control",
                                         "basepri", "faultmask" };

/* append to the core's target-description buffer */
static void xml_put(struct esk_cm_state *k, const char *t)
{
    size_t len = strlen(k->xml), add = strlen(t);
    if (len + add < sizeof k->xml) {
        memcpy(k->xml + len, t, add + 1);
    }
}

static const char *cpu_gdb_xml(struct esk_cpu *c, const char *annex)
{
    struct esk_cm_state *k = (struct esk_cm_state *)c;
    char line[96];
    int i, nsys;
    if (strcmp(annex, "target.xml") != 0)
        return 0;
    k->xml[0] = 0;
    xml_put(k, xml_head);
    if (k->model->fpu)
        xml_put(k, xml_vfp);
    xml_put(k, "<feature name=\"org.gnu.gdb.arm.m-system\">\n");
    nsys = k->model->arch == ARCH_V7M ? 6 : 4;
    for (i = 0; i < nsys; i++) {
        snprintf(line, sizeof line, "<reg name=\"%s\" bitsize=\"32\" regnum=\"%d\" type=\"int\"/>\n",
                 sys_names[i], sys_base(k) + i);
        xml_put(k, line);
    }
    xml_put(k, "</feature>\n</target>\n");
    return k->xml;
}

/* a register's name by its GDB number, for a register view */
static const char *cpu_reg_name(struct esk_cpu *c, int n)
{
    static const char *const core_names[16] = { "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8",
                                                "r9", "r10", "r11", "r12", "sp", "lr", "pc" };
    const struct esk_cm_state *k = (const struct esk_cm_state *)c;
    int nsys = k->model->arch == ARCH_V7M ? 6 : 4;
    if (n >= 0 && n < 16)
        return core_names[n];
    if (n == GR_XPSR)
        return "xpsr";
    if (k->model->fpu && n == GR_FPSCR)
        return "fpscr";
    if (n >= sys_base(k) && n < sys_base(k) + nsys)
        return sys_names[n - sys_base(k)];
    return 0;
}

static const struct esk_cpu_ops cortexm_ops = {
    "cortex-m", 40, "an ARM image",
    cpu_reset, step, cpu_pc, cpu_reg_read, cpu_reg_write,
    cpu_gdb_g_regs, cpu_gdb_xml, 2, cpu_interrupt, 15, 1, 0, cpu_reg_name, cpu_wake_pending,
};

/* the core of an MCU: the model, its NVIC's priority bits, and its own
 * devices on the bus (SysTick inside the system control space, so first;
 * the rest of the private peripheral bus last) */
es_status esk_cortexm_create(struct esk_mach *s, const char *model, int prio_bits,
                             struct esk_cpu **out)
{
    const struct cpu_model *m = 0;
    struct esk_cm_state *k;
    void *st, *dw;
    size_t i;
    for (i = 0; i < sizeof cpus / sizeof cpus[0]; i++)
        if (!strcmp(cpus[i].name, model))
            m = &cpus[i];
    if (!m)
        return ES_ENOTFOUND;
    if (prio_bits < 2 || prio_bits > 8)
        return ES_EINVAL;
    k = (struct esk_cm_state *)esk_mach_alloc(s, sizeof *k);
    st = esk_systick_create(s);
    dw = esk_dwt_create(s);
    if (!k || !st || !dw)
        return ES_ENOMEM;
    k->cpu.ops = &cortexm_ops;
    k->sim = s;
    k->model = m;
    k->prio_bits = prio_bits;
    k->tbit = 1;
    k->fpccr = 0xC0000000u;
    s->cpu = &k->cpu;
    s->bus.snoop = excl_snoop;
    s->bus.snoop_ctx = k;
    if (esk_mach_add_dev(s, 0xE000E010u, 0x10, &esk_systick_ops, st) != ES_OK ||
        esk_mach_add_dev(s, 0xE000E000u, 0x1000, &esk_scs_ops, k) != ES_OK ||
        esk_mach_add_dev(s, 0xE0001000u, 0x1000, &esk_dwt_ops, dw) != ES_OK ||
        esk_mach_add_dev(s, 0xE0000000u, 0x20000000u, &zero_ops, 0) != ES_OK)
        return ES_ELIMIT;
    *out = &k->cpu;
    return ES_OK;
}
