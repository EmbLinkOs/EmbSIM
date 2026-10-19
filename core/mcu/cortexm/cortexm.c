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
 * unreadable vector table locks the core up instead of exiting; WFE and
 * SEV keep the event register (a WFE after a SEV goes on at once), and a
 * second core shares the machine through smp.c. */
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
    switch (cs->access) {
    case ACC_STACK: esk_cm_fault_raise(cs, EXC_BUS, BFSR_STKERR); return;
    case ACC_UNSTACK: esk_cm_fault_raise(cs, EXC_BUS, BFSR_UNSTKERR); return;
    case ACC_LAZYFP: esk_cm_fault_raise(cs, EXC_BUS, BFSR_LSPERR); return;
    default: break;
    }
    esk_cm_fault_raise(cs, EXC_BUS, ifetch ? BFSR_IBUSERR : BFSR_PRECISERR);
    cs->fault_addr = a;
    cs->fault_addr_valid = !ifetch;
}

/* An unprivileged access to the private peripheral bus is a BusFault,
 * except a write to STIR that CCR.USERSETMPEND allows */
static int ppb_refused(struct esk_cm_state *cs, u32 a, int write)
{
    if (a - 0xE0000000u >= 0x100000u || esk_cm_access_privileged(cs))
        return 0;
    if (write && a == 0xE000EF00u && (cs->ccr & 2))
        return 0;
    bus_error(cs, a, 0);
    return 1;
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
    if (watched(cs, a, n, 0) || ppb_refused(cs, a, 0))
        return 0;
    cs->sim->bus.unmapped = 0;
    if (esk_bus_read(&cs->sim->bus, a, n, &v) == 0)
        return v;
    if (cs->sim->bus.unmapped)
        esk_mach_bus_error(cs->sim, cs->sim->bus.fail_addr, n, 0);
    bus_error(cs, cs->sim->bus.fail_addr, 0);
    return 0;
}

void esk_cm_st_bus(struct esk_cm_state *cs, u32 a, int n, u32 v)
{
    if (watched(cs, a, n, 1) || ppb_refused(cs, a, 1))
        return;
    cs->sim->bus.unmapped = 0;
    if (esk_bus_write(&cs->sim->bus, a, n, v)) {
        if (cs->sim->bus.unmapped)
            esk_mach_bus_error(cs->sim, cs->sim->bus.fail_addr, n, 1);
        bus_error(cs, cs->sim->bus.fail_addr, 0);
    }
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

/* whether exception n's own handler can take a fault now: enabled, and
 * of a priority that preempts `prio` */
static int fault_takes(struct esk_cm_state *cs, int n, int prio)
{
    int enabled = 1;
    if (n == EXC_MEM)
        enabled = (cs->shcsr >> 16) & 1;
    else if (n == EXC_BUS)
        enabled = (cs->shcsr >> 17) & 1;
    else if (n == EXC_USAGE)
        enabled = (cs->shcsr >> 18) & 1;
    return enabled && group_prio(cs, n) < prio;
}

/* ExceptionTaken: Handler mode in exception n, with `lr` its EXC_RETURN,
 * and the handler's address from the vector table, read through the
 * default memory map */
static void exc_taken(struct esk_cm_state *cs, int n, u32 lr)
{
    cs->R[14] = lr;
    esk_cm_use_psp(cs, 0);
    cs->ipsr = (u32)n;
    cs->active[n] = 1;
    esk_cm_clr_pend(cs, n);
    cs->itstate = 0;
    cs->excl_valid = 0;
    if (cs->model->fpu)
        cs->control &= ~4u;
    cs->access = ACC_VECTOR;
    u32 v = esk_cm_ld(cs, cs->vtor + 4 * (u32)n, 4);
    cs->access = ACC_NORMAL;
    if (cs->fault_exc) {
        cs->fault_exc = 0;
        lockup(cs, "the vector table cannot be read");
        return;
    }
    /* a vector without the Thumb bit: the first instruction takes an
     * INVSTATE UsageFault */
    cs->tbit = (v & 1) ? 1 : 0;
    cs->pc = v & ~1u;
}

/* A fault of exception entry or return (a stacking, unstacking or lazy
 * state preservation access the MPU or the bus refused): its CFSR bits
 * set, and the exception that takes it, escalated to HardFault when its
 * own handler is disabled or does not preempt `prio`; 0 when even
 * HardFault cannot, a lockup */
static int derived_fault(struct esk_cm_state *cs, int prio)
{
    int exc = cs->fault_exc;
    u32 bits = cs->fault_bits;
    cs->fault_exc = 0;
    if (exc != EXC_HARD) {
        cs->cfsr |= bits;
        if (fault_takes(cs, exc, prio))
            return exc;
        if (cs->model->arch == ARCH_V7M)
            cs->hfsr |= 1u << 30; /* FORCED */
    }
    if (prio <= -1)
        return 0;
    return EXC_HARD;
}

/* ExceptionEntry: push the frame onto the stack in use, with the
 * interrupted code's privilege, and go to the handler. `ret` is the
 * address the frame returns to. With an active floating-point context the
 * frame has room for s0-s15 and FPSCR: written now when FPCCR.LSPEN is
 * clear, else left for the handler's first floating-point instruction
 * (lazy state preservation, FPCCR.LSPACT). A fault while stacking takes
 * its derived exception instead, from the frame as far as it was written;
 * the exception being entered stays pending if it was asynchronous. */
static void exc_entry(struct esk_cm_state *cs, int n, u32 ret)
{
    int fp_frame = cs->model->fpu && (cs->control & 4);
    int lazy = fp_frame && (cs->fpccr & 0x40000000u);
    int was_thread = cs->ipsr == 0, was_priv = esk_cm_privileged(cs);
    int prio_before = esk_cm_exec_prio(cs);
    esk_mach_exc(cs->sim, esk_cortexm_core(cs->sim), n, ret & ~1u);
    u32 frame = fp_frame ? 0x68 : 0x20;
    /* PushStack: with CCR.STKALIGN (always on ARMv6-M) the frame is
     * 8-aligned, and xPSR bit 9 records that 4 bytes were skipped */
    int force = cs->model->arch == ARCH_V6M || (cs->ccr & (1u << 9));
    u32 align = force ? (cs->R[13] >> 2 & 1) : 0;
    u32 sp = (cs->R[13] - frame) & ~(force ? 4u : 0u);
    u32 x = esk_cm_xpsr(cs) & ~(1u << 9);
    if (align)
        x |= 1u << 9;
    cs->access = ACC_STACK;
    esk_cm_st(cs, sp, 4, cs->R[0]);
    esk_cm_st(cs, sp + 4, 4, cs->R[1]);
    esk_cm_st(cs, sp + 8, 4, cs->R[2]);
    esk_cm_st(cs, sp + 12, 4, cs->R[3]);
    esk_cm_st(cs, sp + 16, 4, cs->R[12]);
    esk_cm_st(cs, sp + 20, 4, cs->R[14]);
    esk_cm_st(cs, sp + 24, 4, ret);
    esk_cm_st(cs, sp + 28, 4, x);
    if (fp_frame && !lazy) {
        for (int i = 0; i < 16; i++)
            esk_cm_st(cs, sp + 32 + 4 * (u32)i, 4, cs->S[i]);
        esk_cm_st(cs, sp + 96, 4, cs->fpscr);
    }
    cs->access = ACC_NORMAL;
    cs->R[13] = sp;
    u32 lr;
    if (cs->ipsr)
        lr = 0xFFFFFFF1u;
    else
        lr = cs->psp_active ? 0xFFFFFFFDu : 0xFFFFFFF9u;
    if (fp_frame)
        lr &= ~0x10u;
    if (lazy) {
        /* the frame's FP area is reserved; who could take a fault when
         * it is written is recorded with it */
        u32 f = (cs->fpccr & 0xC0000000u) | 1u;     /* LSPACT */
        if (!was_priv)
            f |= 1u << 1;                           /* USER */
        if (was_thread)
            f |= 1u << 3;                           /* THREAD */
        if (prio_before > -1)
            f |= 1u << 4;                           /* HFRDY */
        if (fault_takes(cs, EXC_MEM, prio_before))
            f |= 1u << 5;                           /* MMRDY */
        if (fault_takes(cs, EXC_BUS, prio_before))
            f |= 1u << 6;                           /* BFRDY */
        cs->fpccr = f;
        cs->fpcar = (sp + 32) & ~7u;
    }
    if (cs->fault_exc) {
        /* the derived fault, escalated against the priority the core ran
         * at, arrives late: the more urgent of it and the exception being
         * entered is taken (on a tie, the lower number), the other pends.
         * An asynchronous exception stays pending (only the one taken is
         * cleared); SVC pends; a fault is raised again when its
         * instruction runs again */
        int d = derived_fault(cs, prio_before);
        if (!d) {
            lockup(cs, "a fault while stacking an exception frame");
            return;
        }
        int pd = group_prio(cs, d), pn = group_prio(cs, n);
        if (pd < pn || (pd == pn && d < n)) {
            if (n == EXC_SVC)
                esk_cm_set_pend(cs, EXC_SVC);
            exc_taken(cs, d, lr);
        } else {
            esk_cm_set_pend(cs, d);
            exc_taken(cs, n, lr);
        }
        return;
    }
    exc_taken(cs, n, lr);
}

/* A synchronous fault, or SVC: taken at once, escalated to HardFault when
 * its own handler is disabled or cannot preempt, and a lockup when even
 * HardFault cannot. */
static void take_sync(struct esk_cm_state *cs, int n, u32 bits, u32 ret)
{
    int ep = esk_cm_exec_prio(cs);
    /* ARMv6-M has no configurable faults: each is a HardFault */
    if (cs->model->arch == ARCH_V6M && n >= EXC_MEM && n <= EXC_USAGE)
        n = EXC_HARD;
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
    esk_mach_exc_return(cs->sim, esk_cortexm_core(cs->sim));
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
    u32 control = cs->control, other = cs->other_sp;
    int psp_was = cs->psp_active;
    esk_cm_use_psp(cs, to_thread && psp);
    if (to_thread)
        cs->control = (cs->control & ~2u) | (psp ? 2u : 0u);
    u32 sp = cs->R[13];
    /* PopStack, with the privilege of the mode returned to */
    cs->access = ACC_UNSTACK;
    cs->force_unpriv = to_thread && (cs->control & 1);
    u32 r0 = esk_cm_ld(cs, sp, 4), r1 = esk_cm_ld(cs, sp + 4, 4);
    u32 r2 = esk_cm_ld(cs, sp + 8, 4), r3 = esk_cm_ld(cs, sp + 12, 4);
    u32 r12 = esk_cm_ld(cs, sp + 16, 4), r14 = esk_cm_ld(cs, sp + 20, 4);
    u32 ra = esk_cm_ld(cs, sp + 24, 4);
    u32 x = esk_cm_ld(cs, sp + 28, 4);
    u32 frame = 0x20, fs[16] = { 0 }, fpscr = cs->fpscr;
    /* an FP area lazy preservation never wrote: the registers still hold
     * the context's values */
    int fp_load = fp_frame && !(cs->fpccr & 1u);
    if (fp_frame) {
        if (fp_load) {
            for (int i = 0; i < 16; i++)
                fs[i] = esk_cm_ld(cs, sp + 32 + 4 * (u32)i, 4);
            fpscr = esk_cm_ld(cs, sp + 96, 4);
        }
        frame = 0x68;
    }
    cs->access = ACC_NORMAL;
    cs->force_unpriv = 0;
    if (cs->fault_exc) {
        /* the return is abandoned: the derived exception is taken from
         * Handler mode, the frame left where it is, LR the EXC_RETURN that
         * failed */
        int d = derived_fault(cs, esk_cm_exec_prio(cs));
        if (!d) {
            lockup(cs, "a fault while unstacking an exception frame");
            return;
        }
        if (psp_was != cs->psp_active)
            esk_cm_use_psp(cs, psp_was);
        cs->control = control;
        cs->other_sp = other;
        exc_taken(cs, d, ret);
        return;
    }
    cs->R[0] = r0;
    cs->R[1] = r1;
    cs->R[2] = r2;
    cs->R[3] = r3;
    cs->R[12] = r12;
    cs->R[14] = r14;
    if (fp_frame) {
        if (fp_load) {
            memcpy(cs->S, fs, sizeof fs);
            cs->fpscr = fpscr;
        }
        cs->fpccr &= ~1u;                   /* LSPACT */
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

/* `bkpt #n` in a ROM whose functions the host does (mach.h): 1 when it was
 * one; its registers, flags and cycles are the core's when no access of
 * it faulted */
int esk_cm_rom_call(struct esk_cm_state *cs, unsigned n)
{
    struct esk_rom_call c;
    memset(&c, 0, sizeof c);
    memcpy(c.r, cs->R, sizeof c.r);
    c.mem = &semi_mem;
    c.mem_ctx = cs;
    if (!cs->sim->rom_call(cs->sim, cs->pc, n, &c))
        return 0;
    if (cs->fault_exc)
        return 1;
    memcpy(cs->R, c.r, sizeof c.r);
    if (c.set_flags) {
        cs->N = (c.nzcv & 8u) != 0;
        cs->Z = (c.nzcv & 4u) != 0;
        cs->C = (c.nzcv & 2u) != 0;
        cs->V = (c.nzcv & 1u) != 0;
    }
    cs->extra_cycles += c.cycles;
    return 1;
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

void esk_cm_wait(struct esk_cm_state *cs, int wfe)
{
    if (wfe && cs->event) {
        cs->event = 0;
        return;
    }
    cs->in_wfe = wfe;
    (void)esk_cm_wfx_idle(cs);
}

void esk_cm_sev(struct esk_cm_state *cs)
{
    if (cs->sev)
        cs->sev(cs->sev_ctx);
    else
        cs->event = 1;
}

/* A halfword of code: from memory, or from a device that answers there (a
 * ROM: the RP2040's, rp2040.c). 0 when nothing does; *rom the read-only
 * region it came from, whose code may be kept decoded */
static int fetch16(struct esk_mach *s, u32 a, u8 *b, const struct esk_region **rom)
{
    struct esk_region *r = esk_bus_region(&s->bus, a, 2);
    *rom = 0;
    if (r) {
        memcpy(b, r->mem + (a - r->base), 2);
        if (r->rom)
            *rom = r;
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
    if ((cs->mpu_on || cs->pc >= 0x40000000u) && esk_cm_mpu_denied(cs, cs->pc, 2, 0, 1)) {
        take_sync(cs, EXC_MEM, MMFSR_IACCVIOL, cs->pc);
        return;
    }
    u32 h[2];
    int size, br;
    u32 cost;
    struct esk_cm_dc *e = &cs->dc[(cs->pc >> 1) & (ESK_CM_DC - 1)];
    if (e->r && e->pc == cs->pc && e->gen == e->r->gen) {
        /* decoded before, from memory that has not changed */
        h[0] = e->h0;
        h[1] = e->h1;
        size = e->size;
        cost = e->cost;
        br = e->br;
        if (size == 4 && (cs->mpu_on || cs->pc + 2 >= 0x40000000u) &&
            esk_cm_mpu_denied(cs, cs->pc + 2, 2, 0, 1)) {
            take_sync(cs, EXC_MEM, MMFSR_IACCVIOL, cs->pc);
            return;
        }
    } else {
        const struct esk_region *r0, *r1 = 0;
        if (!fetch16(s, cs->pc, b, &r0)) {
            take_sync(cs, EXC_BUS, BFSR_IBUSERR, cs->pc);
            return;
        }
        h[0] = (u32)b[0] | (u32)b[1] << 8;
        h[1] = 0;
        size = 2;
        if ((h[0] >> 11) >= 0x1d) {
            if ((cs->mpu_on || cs->pc + 2 >= 0x40000000u) && esk_cm_mpu_denied(cs, cs->pc + 2, 2, 0, 1)) {
                take_sync(cs, EXC_MEM, MMFSR_IACCVIOL, cs->pc);
                return;
            }
            if (!fetch16(s, cs->pc + 2, b + 2, &r1)) {
                take_sync(cs, EXC_BUS, BFSR_IBUSERR, cs->pc);
                return;
            }
            h[1] = (u32)b[2] | (u32)b[3] << 8;
            size = 4;
        }
        cost = (u32)esk_cm_cost(b, (size_t)size, &br);
        /* kept when it all came from one read-only region */
        if (r0 && (size == 2 || r1 == r0) && cost < 256) {
            e->r = r0;
            e->gen = r0->gen;
            e->pc = cs->pc;
            e->h0 = (u16)h[0];
            e->h1 = (u16)h[1];
            e->size = (u8)size;
            e->cost = (u8)cost;
            e->br = (u8)br;
        }
    }
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
        cs->extra_cycles = 0;
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
        if (exc == EXC_MEM && (bits & MMFSR_MMARVALID))
            cs->mmfar = cs->fault_addr;
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
    if (cs->extra_cycles) {
        s->cycles += cs->extra_cycles;
        cost += cs->extra_cycles;
        cs->extra_cycles = 0;
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
    void (*sev)(void *) = k->sev;
    void *sev_ctx = k->sev_ctx;
    int mpu_regions = k->mpu_regions;
    struct esk_cm_dc *dc = k->dc;
    memset(k, 0, sizeof *k);
    k->mpu_regions = mpu_regions;
    k->dc = dc;
    if (dc)
        memset(dc, 0, ESK_CM_DC * sizeof *dc);
    k->cpu = keep;
    k->sim = s;
    k->model = model;
    k->prio_bits = prio_bits;
    k->sev = sev;
    k->sev_ctx = sev_ctx;
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

static int cpu_disasm(struct esk_cpu *c, u32 addr, char *text, size_t cap)
{
    struct esk_cm_state *k = (struct esk_cm_state *)c;
    u32 h1 = 0, h2 = 0;
    if (esk_bus_debug_read(&k->sim->bus, addr & ~1u, 2, &h1) != 0)
        return 0;
    (void)esk_bus_debug_read(&k->sim->bus, (addr & ~1u) + 2, 2, &h2);
    return esk_dis_thumb(addr & ~1u, h1, h2, text, cap);
}

static void cpu_run(struct esk_cpu *c, u64 end)
{
    struct esk_mach *s = ((struct esk_cm_state *)c)->sim;
    while (s->state == ESK_MACH_RUN && s->cycles < end && !s->yield && !s->asleep && !s->bus.watch_hit)
        step(c);
}

static const struct esk_cpu_ops cortexm_ops = {
    "cortex-m", 40, "an ARM image",
    cpu_reset, step, cpu_pc, cpu_reg_read, cpu_reg_write,
    cpu_gdb_g_regs, cpu_gdb_xml, 2, cpu_interrupt, 15, 1, 0, cpu_reg_name, cpu_wake_pending,
    cpu_disasm, cpu_run,
};

/* the core of an MCU: the model, its NVIC's priority bits, and its own
 * devices on the bus (SysTick inside the system control space, so first;
 * the rest of the private peripheral bus last) */
struct esk_cm_state *esk_cm_new(struct esk_mach *s, const char *model, int prio_bits)
{
    const struct cpu_model *m = 0;
    struct esk_cm_state *k;
    size_t i;
    for (i = 0; i < sizeof cpus / sizeof cpus[0]; i++)
        if (!strcmp(cpus[i].name, model))
            m = &cpus[i];
    if (!m || prio_bits < 2 || prio_bits > 8)
        return 0;
    k = (struct esk_cm_state *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    k->cpu.ops = &cortexm_ops;
    k->sim = s;
    k->dc = (struct esk_cm_dc *)esk_mach_alloc(s, ESK_CM_DC * sizeof *k->dc);
    if (!k->dc)
        return 0;
    k->model = m;
    k->prio_bits = prio_bits;
    k->tbit = 1;
    k->fpccr = 0xC0000000u;
    return k;
}

es_status esk_cortexm_create(struct esk_mach *s, const char *model, int prio_bits,
                             struct esk_cpu **out)
{
    struct esk_cm_state *k;
    void *st, *dw;
    size_t i;
    int known = 0;
    for (i = 0; i < sizeof cpus / sizeof cpus[0]; i++)
        if (!strcmp(cpus[i].name, model))
            known = 1;
    if (!known)
        return ES_ENOTFOUND;
    if (prio_bits < 2 || prio_bits > 8)
        return ES_EINVAL;
    k = esk_cm_new(s, model, prio_bits);
    st = esk_systick_create(s);
    dw = esk_dwt_create(s);
    if (!k || !st || !dw)
        return ES_ENOMEM;
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
