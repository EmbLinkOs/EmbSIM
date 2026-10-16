/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* rf.h -- an MCU's peripheral registers as a register file on the bus, and
 * the interface of the behavioural models layered over them (SPEC-009 §5,
 * §6).
 *
 * Every register a register description (`registers.json`, SPEC-006 §5)
 * names is a word of storage with its reset value, and an access to it does
 * what the description says its fields do: read-only, write-only, write
 * once; rc_w0 and rc_w1 (cleared by writing 0 or 1), rs (set by writing 1),
 * t (toggled by writing 1); rc_r (cleared by reading). Bits no field covers
 * read as their reset value and ignore writes; a register without fields is
 * one read-write field. An access where no register is, is a bus fault with
 * a diagnostic naming the peripheral and offset. A peripheral without a
 * model is the register file alone, and says so once (mcu.unsupported_access).
 *
 * A model's read and write hooks get the program's accesses (`off` from the
 * peripheral's base) and pass them to esk_rf_read and esk_rf_write, then do
 * what the hardware does; a change the hardware makes is esk_rf_hw. A
 * model's tick and next_event hooks keep time while its clock runs
 * (esk_rf_clock).
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/svd-map.h. The register
 * file is built from EmbSIM's JSON register descriptions instead of a
 * CMSIS-SVD file (ADR-019): an SVD converts to one with tools/importers/svd2regs.py.
 * Tracing and PIC32 CLR/SET/INV registers are not carried over yet.
 *
 * A part's bus can add rules (the MCU description's register_file
 * bus_rules):
 *   ESK_RF_ATOMIC_ALIASES  each peripheral's registers again at +0x1000,
 *                          where a write XORs its bits into the register,
 *                          +0x2000 sets them and +0x3000 clears them (the
 *                          RP2040's, below 0xD0000000: the SIO has none);
 *                          a read there reads the register;
 *   ESK_RF_WIDE_WRITES     a byte or halfword write to a 32-bit register
 *                          writes the whole register, the value repeated
 *                          across the bus's byte lanes (the RP2040's APB). */
#ifndef ESK_MCU_RF_H
#define ESK_MCU_RF_H

#include "mcu/mach.h"
#include "util/json.h"

/* a field's access, its write behaviour and its read action */
enum { ESK_ACC_RW, ESK_ACC_RO, ESK_ACC_WO, ESK_ACC_W1, ESK_ACC_RW1 };
enum {
    ESK_MWV_MODIFY, ESK_MWV_1CLR, ESK_MWV_1SET, ESK_MWV_1TOG, ESK_MWV_0CLR, ESK_MWV_0SET,
    ESK_MWV_0TOG, ESK_MWV_CLEAR, ESK_MWV_SET
};
enum { ESK_RA_NONE, ESK_RA_CLEAR, ESK_RA_SET };

struct esk_rf_periph;

struct esk_rf_field {
    const char *name; /* 0: a register without fields */
    int lsb, width;
    int acc, mwv, ract;
};

struct esk_rf_reg {
    u32 addr;  /* absolute */
    int bytes; /* 4, 2 for a halfword register, 1 for a byte (an AVR's) */
    u64 value, reset;
    u64 once;  /* the write-once bits written since reset */
    const char *name;
    struct esk_rf_periph *p;
    struct esk_rf_field *f;
    int nf;
};

/* a peripheral model's hooks: struct esk_dev_ops over one peripheral */
struct esk_rf_periph {
    const char *name;
    u32 base, end; /* end: past its last register */
    struct esk_rf_reg **reg;
    int nreg;
    const char *const *irq_names; /* its interrupts, by the description's names */
    const int *irq_nums;
    int nirq;
    struct esk_mach *sim;
    struct esk_rf *rf;
    const struct esk_dev_ops *ops;
    void *ctx;
    const char *model; /* the model's name, 0 for the register file alone */
    /* its clock gate: while `gate`'s `gate_mask` bits are clear, a read
     * gives 0 and a write is ignored (the RCC model sets it); with
     * gate_held, the peripheral is held in reset while they are set: a
     * read gives its registers' reset values */
    struct esk_rf_reg *gate;
    u64 gate_mask;
    int gate_held;
    const char *gate_name; /* RCC.APB1ENR.USART2EN */
    int gate_warned, plain_warned;
};

/* a model type */
struct esk_rf_model_type {
    const char *name;
    const struct esk_dev_ops *ops;
    /* its context, or 0 when the peripheral is not one it can model */
    void *(*create)(struct esk_mach *m, struct esk_rf_periph *p);
};

#define ESK_RF_TICKERS 32

enum { ESK_RF_ATOMIC_ALIASES = 1, ESK_RF_WIDE_WRITES = 2 };

struct esk_rf {
    struct esk_mach *m;
    const char *device;
    struct esk_rf_periph *p;
    int np;
    struct esk_rf_reg **reg; /* every register, by address */
    int nreg;
    u32 lo, hi;              /* the span the bus device covers */
    int rules;               /* ESK_RF_ATOMIC_ALIASES, ESK_RF_WIDE_WRITES */
    struct esk_rf_periph *tk[ESK_RF_TICKERS];
    int ntk;
    int faults;
};

/* Build the register file from a register description and put it on the
 * bus, with the part's bus rules. Errors name the problem in `why`. */
es_status esk_rf_create(struct esk_mach *m, const esk_json *desc, int rules, char *why, size_t why_cap);
/* the rules a description's bus_rules names: -1 for an unknown one */
int esk_rf_rules(const esk_json *names);
/* bind the models of `binding` (an MCU description's "peripherals": an
 * array of {"match": glob, "model": name}); unknown model names are left as
 * plain register files and reported in `missing` (a comma list) */
es_status esk_rf_bind(struct esk_mach *m, const esk_json *binding, char *missing, size_t cap);
const struct esk_rf_model_type *esk_rf_model_find(const char *name);

/* ---- for the models --------------------------------------------------- */

int esk_rf_gated(const struct esk_rf_periph *p);
/* the peripheral's registers to their reset values, and its model's reset */
void esk_rf_periph_reset(struct esk_rf_periph *p);
struct esk_rf_periph *esk_rf_periph(struct esk_rf *rf, const char *name);
struct esk_rf_periph *esk_rf_periph_at(struct esk_rf *rf, int i);
struct esk_rf_reg *esk_rf_reg(struct esk_rf_periph *p, const char *name);
/* the bits of the register's field of that name, or 0 */
u64 esk_rf_mask(const struct esk_rf_reg *r, const char *field);
/* the program's read and write, as the description says the fields behave */
u32 esk_rf_read(struct esk_rf_periph *p, u32 off, int n);
void esk_rf_write(struct esk_rf_periph *p, u32 off, int n, u32 v);
ES_INLINE u32 esk_rf_off(const struct esk_rf_reg *r) { return r->addr - r->p->base; }
/* the hardware sets a register to `v` */
void esk_rf_hw(struct esk_rf_reg *r, u64 v);
void esk_rf_set_mwv(struct esk_rf_reg *r, u64 mask, int mwv);
void esk_rf_set_reset(struct esk_rf_reg *r, u64 v);
void esk_rf_set_acc(struct esk_rf_reg *r, u64 mask, int acc);
u64 esk_rf_fields(const struct esk_rf_reg *r);
/* the peripheral's interrupt: the first whose name has `want` in it, else
 * its first; -1 when it has none */
int esk_rf_irq(const struct esk_rf_periph *p, const char *want);
/* pend that interrupt in the core */
void esk_rf_interrupt(struct esk_rf_periph *p, int irq);
/* the model's clock starts (1) or stops: while it runs, its tick hook is
 * called after each instruction and its next_event asked */
void esk_rf_clock(struct esk_rf_periph *p, int *running, int on);
/* a diagnostic from a model or the register file, through the host */
void esk_rf_diag(struct esk_mach *m, int severity, const char *code, const char *fmt, ...);

#endif
