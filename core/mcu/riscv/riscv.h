/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* riscv.h -- the RISC-V core and the SiFive FE310's peripheral models
 * (SPEC-009 §2, §6).
 *
 *   riscv.c   the core: RV32IMAC, Zicsr, the machine-mode traps and CSRs,
 *             the CPU interface
 *   disasm.c  the instruction at an address, for the Debug perspective
 *   clint.c   the CLINT: msip, mtimecmp and mtime from the RTC clock
 *   plic.c    the PLIC: priorities, pending, enables, threshold, claim
 *   gpio.c    the FE310's GPIO: 32 pins with their interrupts and IOFs
 *   uart.c    the FE310's UART: FIFOs, watermarks, the baud divisor
 *   prci.c    the clocks: HFROSC, HFXOSC and the PLL
 *
 * The devices drive the core's interrupt lines (MSIP, MTIP, MEIP in mip)
 * with esk_riscv_line; the CLINT gives the core its `time`.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/riscv.h, riscv.c,
 * clint.c, disasm.c (the RISC-V part). The FE310 models are EmbSIM's. */
#ifndef ESK_MCU_RISCV_H
#define ESK_MCU_RISCV_H

#include "mcu/rf.h"


/* exception causes (mcause without the interrupt bit) */
enum {
    EXC_IFETCH_ACCESS = 1, EXC_ILLEGAL = 2, EXC_BREAKPOINT = 3, EXC_LOAD_ALIGN = 4,
    EXC_LOAD_ACCESS = 5, EXC_STORE_ALIGN = 6, EXC_STORE_ACCESS = 7, EXC_ECALL_U = 8,
    EXC_ECALL_M = 11
};

/* not a trap: a debugger's watchpoint stops the instruction before its
 * access, which is not made, and the instruction is not counted */
#define TRAP_WATCH (~0ull)

/* interrupts: mcause's code, and its bit in mip and mie */
#define IRQ_MSI 3
#define IRQ_MTI 7
#define IRQ_MEI 11
#define MIP_MSIP (1u << IRQ_MSI)
#define MIP_MTIP (1u << IRQ_MTI)
#define MIP_MEIP (1u << IRQ_MEI)

#define MSTATUS_MIE (1u << 3)
#define MSTATUS_MPIE (1u << 7)
#define MSTATUS_MPP (3u << 11)
#define MSTATUS_FS (3u << 13)
#define MSTATUS_MPRV (1u << 17)
#define MSTATUS_TW (1u << 21)

enum { PRV_U = 0, PRV_M = 3 };


struct rv_state {
    struct esk_cpu cpu; /* the interface; first */
    struct esk_mach *sim;
    int xlen;
    u64 x[32];
    u64 pc, npc; /* this instruction, and the next */
    int priv;
    u32 reset_pc;

    /* the machine-mode CSRs */
    u64 mstatus, mie, mtvec, mepc, mcause, mtval, mscratch;
    u64 medeleg, mideleg, mcounteren, mcountinhibit, menvcfg;
    u64 pmpcfg[16], pmpaddr[64];
    u64 mcycle_off, minstret_off; /* what a write added to the counts */
    u64 cyc_now, insn_now;        /* the counts before this instruction */

    /* LR's reservation: the address, its width and the value read */
    int resv;
    u64 resv_addr, resv_val;

    /* A trap raised by the instruction executing: the first wins, and
     * nothing the instruction would still do is done. */
    int trap;
    u64 cause, tval;
    u32 insn;    /* its bits (16 for a compressed one) */
    int changed; /* it wrote a register a new value */

    /* the interrupt lines the devices drive (MIP_*), and mtime's source */
    u32 lines;
    u64 (*mtime)(void *ctx);
    void *mtime_ctx;
    char xml[4096]; /* the GDB target description being answered */
};

/* a value at the core's width */
ES_INLINE u64 rv_xl(struct rv_state *rs, u64 v) { return rs->xlen == 32 ? (u64)(u32)v : v; }

/* a value of the core's width, sign-extended */
ES_INLINE s64 rv_sx(struct rv_state *rs, u64 v)
{
    return rs->xlen == 32 ? (s64)(int32_t)(u32)v : (s64)v;
}

ES_INLINE void rv_wx(struct rv_state *rs, u32 rd, u64 v)
{
    if (rd) {
        v = rv_xl(rs, v);
        if (rs->x[rd] != v)
            rs->changed = 1;
        rs->x[rd] = v;
    }
}

/* riscv.c */
void rv_trap(struct rv_state *rs, u64 cause, u64 tval);
void rv_illegal(struct rv_state *rs);
u64 rv_load(struct rv_state *rs, u64 a, int n);
void rv_store(struct rv_state *rs, u64 a, int n, u64 v);
u64 rv_mtime(struct rv_state *rs);
u64 rv_mip(struct rv_state *rs);
/* a compressed instruction's 32-bit equivalent at width xlen, or 0 */
u32 esk_rvc_expand(u32 h, int xlen);

/* ---- the interface for the MCU and its devices ------------------------- */

/* an RV32 core of `model` ("rv32imac") starting at reset_pc */
es_status esk_riscv_create(struct esk_mach *m, const char *model, u32 reset_pc, struct esk_cpu **out);
int esk_riscv_is(const struct esk_cpu *c);
/* a device raises (1) or lowers an interrupt line: MIP_MSIP, MIP_MTIP or
 * MIP_MEIP */
void esk_riscv_line(struct esk_mach *m, u32 bit, int level);
/* the CLINT gives the core its `time` CSR */
void esk_riscv_mtime_source(struct esk_mach *m, u64 (*mtime)(void *ctx), void *ctx);
/* the instruction `insn` at `pc` (16 bits for a compressed one): its text
 * and its size in bytes */
int esk_riscv_disasm(u32 pc, u32 insn, char *text, size_t cap);

/* ---- the FE310's peripherals ------------------------------------------- */

extern const struct esk_dev_ops esk_fe310_clint_ops, esk_fe310_plic_ops, esk_fe310_gpio_ops,
    esk_fe310_uart_ops, esk_fe310_prci_ops;
void *esk_fe310_clint_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_fe310_plic_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_fe310_gpio_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_fe310_uart_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_fe310_prci_create(struct esk_mach *m, struct esk_rf_periph *p);

/* plic.c: a source's level changed (GPIO n is source 8 + n, UART0 3) */
void esk_fe310_plic_source(struct esk_mach *m, int source, int level);
/* gpio.c: whether pin n is given to IOF0 or IOF1 (iof_en, iof_sel) */
int esk_fe310_gpio_iof(struct esk_mach *m, int pin, int *sel);
void esk_fe310_pins_changed(struct esk_mach *m);
int esk_fe310_uart_pin(struct esk_mach *m, const char *unit, int tx, int *port, int *bit);
/* the UART's TX pin level while its IOF drives it */
void esk_fe310_gpio_iof_level(struct esk_mach *m, int pin, int level);

#endif
