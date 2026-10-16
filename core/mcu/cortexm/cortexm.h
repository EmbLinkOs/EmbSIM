/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* cortexm.h -- the Cortex-M core backend (SPEC-009 §3): its state, shared
 * by its files and by the devices that are part of it.
 *
 *   cortexm.c   the state, memory access, exceptions, step, reset, and the
 *               core interface (registers by GDB number too)
 *   thumb.c     the Thumb instruction set, 16- and 32-bit
 *   fpu.c       the floating-point unit (FPv4-SP, FPv5)
 *   scs.c       the system control space: NVIC and SCB registers
 *   systick.c   SysTick;  dwt.c  DWT's cycle counter;  cost.h  cycles
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/cortexm.h. EmbCC kept
 * the executing core in a file-scope pointer `cs`; here every function
 * takes it as its first parameter (CS-18), so two MCUs in one session or
 * two sessions in two threads never share it. */
#ifndef ESK_MCU_CORTEXM_H
#define ESK_MCU_CORTEXM_H

#include "mcu/mach.h"

enum { ARCH_V6M, ARCH_V7M };
enum { FPU_NONE, FPU_SP, FPU_DP };

struct cpu_model {
    const char *name;
    int arch, dsp, fpu;
    u32 cpuid;
};

/* exception numbers */
enum { EXC_RESET = 1, EXC_NMI, EXC_HARD, EXC_MEM, EXC_BUS, EXC_USAGE,
       EXC_SVC = 11, EXC_DEBUG, EXC_PENDSV = 14, EXC_SYSTICK };

/* CFSR bits */
#define UFSR_UNDEFINSTR (1u << 16)
#define UFSR_INVSTATE   (1u << 17)
#define UFSR_INVPC      (1u << 18)
#define UFSR_NOCP       (1u << 19)
#define UFSR_UNALIGNED  (1u << 24)
#define UFSR_DIVBYZERO  (1u << 25)
#define BFSR_IBUSERR    (1u << 8)
#define BFSR_PRECISERR  (1u << 9)

#define NEXC 256                    /* 16 system exceptions + 240 IRQs */

/* fault_exc's value for an access a debugger's watchpoint stops before */
#define FAULT_WATCH (-1)

struct esk_cm_state {
    struct esk_cpu cpu;                 /* the interface; first */
    struct esk_mach *sim;
    const struct cpu_model *model;
    int prio_bits;                  /* NVIC priority bits (the board's) */

    /* the registers */
    u32 R[16];                      /* R[13] is the active stack pointer */
    u32 other_sp;                   /* the banked one */
    int psp_active;                 /* R[13] is PSP */
    int N, Z, C, V, Q;
    u32 GE;
    u32 itstate;
    int tbit;
    u32 ipsr;                       /* 0 in Thread mode */
    u32 primask, faultmask, basepri, control;
    u32 S[64];                      /* s0..s31 (d0..d15), and d16..d31 */
    u32 fpscr;
    int excl_valid;
    u32 excl_addr;

    u32 pc, npc;                    /* this instruction, and the next */
    int pc_written;
    u32 exc_return_value;
    int exc_return_pending;

    /* A fault raised by the instruction executing: the first one wins,
     * and every access after it is suppressed, so the instruction's
     * partial work is discarded with the register snapshot (step). */
    int fault_exc;
    u32 fault_bits, fault_addr;
    int fault_addr_valid;
    int executing;                  /* in an instruction (not stacking) */

    /* the NVIC and the SCB */
    u8 pend[NEXC], active[NEXC], irq_en[NEXC];
    u8 prio[NEXC];                  /* as programmed, top bits significant */
    int npend;
    u32 vtor, aircr_prigroup, ccr, shcsr, cfsr, hfsr, mmfar, bfar, scr;
    u32 cpacr, fpccr, fpcar, fpdscr;
    u32 demcr;
    char xml[4096];                 /* the target description, built on demand */

    /* the event register (WFE and SEV), and whether the wait in progress
     * is a WFE's; a multi-core part's SEV reaches its other cores (smp.c) */
    int event, in_wfe;
    void (*sev)(void *ctx);
    void *sev_ctx;
};


/* cortexm.c */
void esk_cm_undef(struct esk_cm_state *cs);
u32 esk_cm_ld_bus(struct esk_cm_state *cs, u32 a, int n);
void esk_cm_st_bus(struct esk_cm_state *cs, u32 a, int n, u32 v);
void esk_cm_bx_to(struct esk_cm_state *cs, u32 a);
u32 esk_cm_xpsr(struct esk_cm_state *cs);
void esk_cm_set_apsr(struct esk_cm_state *cs, u32 v);
int esk_cm_privileged(struct esk_cm_state *cs);
void esk_cm_use_psp(struct esk_cm_state *cs, int psp);
u32 esk_cm_get_msp(struct esk_cm_state *cs);
u32 esk_cm_get_psp(struct esk_cm_state *cs);
void esk_cm_set_msp(struct esk_cm_state *cs, u32 v);
void esk_cm_set_psp(struct esk_cm_state *cs, u32 v);
void esk_cm_set_pend(struct esk_cm_state *cs, int n);
void esk_cm_clr_pend(struct esk_cm_state *cs, int n);
u32 esk_cm_prio_mask(struct esk_cm_state *cs);
int esk_cm_exec_prio(struct esk_cm_state *cs);
int esk_cm_cond_passed(struct esk_cm_state *cs, u32 cond);
void esk_cm_it_advance(struct esk_cm_state *cs);
int esk_cm_wfx_idle(struct esk_cm_state *cs);
/* WFI (wfe 0) and WFE (wfe 1): the core waits, unless an exception is
 * pending or, for WFE, the event register is set (it is cleared); SEV */
void esk_cm_wait(struct esk_cm_state *cs, int wfe);
void esk_cm_sev(struct esk_cm_state *cs);
/* a core's state without its devices on the bus: smp.c's second core */
struct esk_cm_state *esk_cm_new(struct esk_mach *m, const char *model, int prio_bits);
void esk_cm_semihost(struct esk_cm_state *cs);

/* cortexm-thumb.c */
void esk_cm_exec16(struct esk_cm_state *cs, u32 h);
void esk_cm_exec32(struct esk_cm_state *cs, u32 h1, u32 h2);

/* cortexm-fpu.c */
int esk_cm_fp_begin(struct esk_cm_state *cs);
void esk_cm_vfp_ldst(struct esk_cm_state *cs, u32 h1, u32 h2);
void esk_cm_vfp_xfer(struct esk_cm_state *cs, u32 h1, u32 h2);
void esk_cm_vfp_dp(struct esk_cm_state *cs, u32 h1, u32 h2);

/* ---- the hot helpers, inline ---------------------------------------- */

/* a fault of the instruction executing: the first one wins */
ES_INLINE void esk_cm_fault_raise(struct esk_cm_state *cs, int exc, u32 bits)
{
    if (cs->fault_exc)
        return;
    /* ARMv6-M has no configurable faults: each is a HardFault */
    if (cs->model->arch == ARCH_V6M && exc >= EXC_MEM && exc <= EXC_USAGE)
        exc = EXC_HARD;
    cs->fault_exc = exc;
    cs->fault_bits = bits;
}

/* The core's loads and stores: plain memory directly, the rest (bit-band,
 * devices, a bus error) and every access while a watchpoint is set
 * through the bus. Nothing is accessed once the instruction faulted. A
 * store to the address LDREX marked clears the local monitor. */
ES_INLINE u32 esk_cm_ld(struct esk_cm_state *cs, u32 a, int n)
{
    if (cs->fault_exc)
        return 0;
    struct esk_bus *b = &cs->sim->bus;
    if (!b->nwatch) {
        struct esk_region *r = esk_bus_region(b, a, (u32)n);
        if (r)
            return esk_mem_rd_le(r->mem + (a - r->base), n);
    }
    return esk_cm_ld_bus(cs, a, n);
}

ES_INLINE void esk_cm_st(struct esk_cm_state *cs, u32 a, int n, u32 v)
{
    if (cs->fault_exc)
        return;
    struct esk_bus *b = &cs->sim->bus;
    if (!b->nwatch) {
        struct esk_region *r = esk_bus_region(b, a, (u32)n);
        if (r) {
            if (cs->excl_valid && a - cs->excl_addr < 4)
                cs->excl_valid = 0;
            if (!r->rom)
                esk_mem_wr_le(r->mem + (a - r->base), n, v);
            return;
        }
    }
    esk_cm_st_bus(cs, a, n, v);
}

/* An access an instruction makes: v6-M faults on every unaligned one;
 * v7-M on the ones that must be aligned (multiple, dual, exclusive),
 * and on the rest only with CCR.UNALIGN_TRP. */
ES_INLINE int esk_cm_aligned_ok(struct esk_cm_state *cs, u32 a, int n, int must)
{
    if ((a & (u32)(n - 1)) == 0)
        return 1;
    if (cs->model->arch == ARCH_V6M || must || (cs->ccr & 8)) {
        esk_cm_fault_raise(cs, EXC_USAGE, UFSR_UNALIGNED);
        return 0;
    }
    return 1;
}

ES_INLINE u32 esk_cm_mem_ld(struct esk_cm_state *cs, u32 a, int n, int must)
{
    return esk_cm_aligned_ok(cs, a, n, must) ? esk_cm_ld(cs, a, n) : 0;
}

ES_INLINE void esk_cm_mem_st(struct esk_cm_state *cs, u32 a, int n, u32 v, int must)
{
    if (esk_cm_aligned_ok(cs, a, n, must))
        esk_cm_st(cs, a, n, v);
}

ES_INLINE u32 esk_cm_reg(struct esk_cm_state *cs, int n)
{
    return n == 15 ? cs->pc + 4 : cs->R[n];
}

ES_INLINE void esk_cm_set_sp(struct esk_cm_state *cs, u32 v)
{
    cs->R[13] = v & ~3u;
}

ES_INLINE void esk_cm_branch_to(struct esk_cm_state *cs, u32 a)  /* BranchWritePC */
{
    cs->npc = a & ~1u;
    cs->pc_written = 1;
}

ES_INLINE void esk_cm_set_reg(struct esk_cm_state *cs, int n, u32 v)     /* with ALUWritePC */
{
    if (n == 15)
        esk_cm_branch_to(cs, v);
    else if (n == 13)
        esk_cm_set_sp(cs, v);
    else
        cs->R[n] = v;
}

ES_INLINE int esk_cm_in_it(struct esk_cm_state *cs)
{
    return (cs->itstate & 0xf) != 0;
}

/* disasm.c: the Thumb instruction at pc (h2 is read only for a 32-bit
 * one): its size in bytes, and the text in b */
int esk_dis_thumb(u32 pc, u32 h1, u32 h2, char *b, size_t n);

/* the core's own devices (systick.c, scs.c, dwt.c) */
extern const struct esk_dev_ops esk_systick_ops, esk_scs_ops, esk_dwt_ops;
void *esk_systick_create(struct esk_mach *m);
/* where the SysTick exception goes: `pend(ctx, 15)`, else the machine's core */
void esk_systick_target(void *systick, void (*pend)(void *ctx, int n), void *ctx);
void *esk_dwt_create(struct esk_mach *m);

#endif
