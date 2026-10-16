/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* mips.h -- the MIPS32 core and what drives it (SPEC-009 §2, §6).
 *
 *   mips.c    the core: MIPS32 Release 2, little-endian, with branch delay
 *             slots; coprocessor 0 (exceptions, interrupts in
 *             compatibility, vectored and EIC modes, shadow register sets,
 *             the core timer, the TLB); the CPU interface
 *   mips16.c  MIPS16e, the M4K's compressed instruction set
 *   disasm.c  the MIPS32 instruction at an address, for the Debug
 *             perspective
 *
 * The bus is physical: kseg0 and kseg1 (0x80000000 to 0xbfffffff) reach
 * its low 512 MiB, so an image's load addresses (virtual, in kseg0 and
 * kseg1) go through the core's `phys` hook. A PIC32's interrupt
 * controller drives the core in EIC mode (struct mips_eic).
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/mips.h, mips.c,
 * mips16.c and disasm.c (its MIPS32 part), refereed there against QEMU's
 * malta. Changes for EmbSIM: the core state is every function's parameter
 * (no file-scope `ms`, CS-18); the cores are the PIC32MX's M4K and QEMU
 * malta's 24Kc, without the FPU, the DSP ASE and microMIPS (a PIC32MZ's,
 * not modelled yet: their instructions are Reserved Instructions); WAIT
 * and an idle loop put the machine to sleep for the runtime to
 * fast-forward (ADR-019), where EmbCC ended the run. */
#ifndef ESK_MCU_MIPS_H
#define ESK_MCU_MIPS_H

#include "mcu/mach.h"

/* the register sets: the normal one and the shadow sets */
#define MIPS_SETS 8
#define MIPS_TLB 16

/* exception codes (Cause.ExcCode) */
enum {
    EXC_INT = 0, EXC_MOD = 1, EXC_TLBL = 2, EXC_TLBS = 3, EXC_ADEL = 4,
    EXC_ADES = 5, EXC_IBE = 6, EXC_DBE = 7, EXC_SYS = 8, EXC_BP = 9,
    EXC_RI = 10, EXC_CPU = 11, EXC_OV = 12, EXC_TR = 13, EXC_FPE = 15,
    EXC_DSPDIS = 26
};

/* Status */
#define ST_IE 0x00000001u
#define ST_EXL 0x00000002u
#define ST_ERL 0x00000004u
#define ST_UM 0x00000010u
#define ST_NMI 0x00080000u
#define ST_SR 0x00100000u
#define ST_BEV 0x00400000u
#define ST_MX 0x01000000u
#define ST_CU0 0x10000000u
#define ST_CU1 0x20000000u
/* Cause */
#define CA_IV 0x00800000u
#define CA_DC 0x08000000u
#define CA_TI 0x40000000u
#define CA_BD 0x80000000u
/* Debug */
#define DBG_DM 0x40000000u

/* What an external interrupt controller (EIC mode, Config3.VEIC) gives
 * the core: the requested level (RIPL, 0 for none), the handler's offset
 * from EBASE, and the shadow set it runs in. It is told what the core
 * itself requests -- the timer's interrupt and the two software
 * interrupts -- and when the core takes its request. */
struct mips_eic {
    void (*core_irq)(void *ctx, int timer, int sw0, int sw1);
    void (*taken)(void *ctx);
    /* whether the core timer's interrupt is enabled at the controller,
     * so a wait for it can end */
    int (*timer_on)(void *ctx);
    void *ctx;
    int ripl;
    u32 offset;
    int srs;
    /* the vector number, when the core places the handler as the MIPS32
     * architecture has it -- EBase + 0x200 + vector * IntCtl.VS's
     * spacing (a PIC32MX's) -- rather than at `offset`: spaced set */
    int vector, spaced;
};

struct mips_tlbe {
    u32 hi, lo0, lo1, mask;
    int g; /* global: both EntryLo's G */
};

struct mips_state {
    struct esk_cpu cpu; /* the interface; first */
    struct esk_mach *sim;
    int pic32; /* a PIC32's core: EIC mode, its IDs */
    int fmt;   /* fixed mapping, no TLB (the M4K's) */
    int hss;   /* SRSCtl.HSS: shadow sets, less one */

    u32 gpr[MIPS_SETS][32];
    u32 *r;           /* the set executing: gpr[SRSCtl.CSS] */
    u32 hi[4], lo[4]; /* HI and LO ($ac0; the DSP's $ac1-$ac3 unused) */
    u32 pc;           /* the instruction executing */
    u32 npc;          /* the one after it: a branch's target */
    int slot;         /* pc is a branch's delay slot */
    /* while an instruction executes: where execution goes after it, the
     * one after that, and whether the next is a delay slot. These three
     * addresses carry the ISA mode in bit 0 (1: MIPS16e), as a jump's
     * target does; pc does not. */
    u32 nx, nnx;
    int nslot;
    int isa;    /* executing MIPS16e, not MIPS32 */
    int mips16; /* the core has MIPS16e (Config1.CA) */
    /* the delay slot's branch (its address, the ISA bit in bit 0) for EPC,
     * and the size the slot's instruction must be (0 for either) */
    u32 bpc, nbpc;
    int ds, nds;
    int btaken, nbtaken; /* the slot's branch was taken */

    /* coprocessor 0 */
    u32 status, cause, epc, errorepc, badvaddr, ebase, intctl, srsctl;
    u32 srsmap, compare, config0, hwrena, userlocal, lladdr, depc, debug;
    u32 desave, wired, index, entryhi, entrylo0, entrylo1, pagemask;
    u32 context, errctl, taglo[4], watchlo, watchhi;
    u32 prid, config1, config2, config3, config5, config7;
    struct mips_tlbe tlb[MIPS_TLB];
    int llbit;
    u32 llval;      /* what LL read: SC compares it, as QEMU does */
    u32 core_lines; /* what the EIC was last told */

    /* Count: count_base at the cycle count_at, counting every other
     * cycle; it reaches Compare at ti_at */
    u32 count_base;
    u64 count_at, ti_at;

    struct mips_eic *eic; /* EIC mode's controller, or 0 */
    u32 ip_hw;            /* compatibility mode: IP2-IP7 lines */

    /* the instruction executing raised an exception: the first wins, and
     * it writes nothing */
    int exc;
    u32 exc_code, exc_bad, exc_ce;
    int exc_bad_set, exc_refill;
    int watch_stop; /* a debugger's watchpoint: not run */
    u32 insn;
};

/* mips.c, for mips16.c: a MIPS32 instruction executed for the MIPS16e
 * one it encodes the same operation as; an instruction's memory accesses;
 * an exception; an unconditional branch to itself, which puts the machine
 * to sleep when nothing can interrupt it (`slot_nop`: its delay slot does
 * nothing) */
void mips_exec32(struct mips_state *ms, u32 i);
u32 mips_load(struct mips_state *ms, u32 va, int n);
void mips_store(struct mips_state *ms, u32 va, int n, u32 v);
void mips_raise(struct mips_state *ms, u32 code);
void mips_self_loop(struct mips_state *ms, u32 target, int slot_nop);
/* mips16.c: a MIPS16e instruction (an EXTENDed instruction, or jal and
 * jalx, in both halves, the first in bits 31:16), its size, its cost */
int mips16_size(u32 first);
void mips16_exec(struct mips_state *ms, u32 i, int size);
int mips16_cost(u32 i, int size);
/* disasm.c: a MIPS32 instruction's text (its size, 4) */
int esk_mips_disasm(u32 pc, u32 insn, char *text, size_t cap);

/* ---- the interface for the MCU and its devices ------------------------- */

/* a core of `model`: "pic32mx" (the M4K: EIC mode, two register sets,
 * MIPS16e, fixed mapping) or "24kc" (QEMU malta's: compatibility-mode
 * interrupts, the TLB, MIPS16e) */
es_status esk_mips_create(struct esk_mach *m, const char *model, struct esk_cpu **out);
int esk_mips_is(const struct esk_cpu *c);
/* an image's address as the bus has it: kseg0 and kseg1 unmapped */
u32 esk_mips_phys(u32 va);
/* EIC mode: the controller drives the core; its request changed */
void esk_mips_eic_attach(struct esk_mach *m, struct mips_eic *e);
void esk_mips_eic_changed(struct esk_mach *m);
/* the core timer's Count now, for a test */
u32 esk_mips_count(struct esk_mach *m);

#endif
