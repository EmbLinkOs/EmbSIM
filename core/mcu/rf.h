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
 * Tracing is not carried over yet.
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
 *                          across the bus's byte lanes (the RP2040's APB).
 * A register the description marks clr_set_inv has three more after it
 * (a PIC32's): +4 clears the bits a write sets, +8 sets them and +0xC
 * inverts them, as an atomic alias does; a read of them gives 0.
 *
 * The description's interrupt table is kept for the models that find an
 * interrupt by name (esk_rf_irq_named), with each one's vector where
 * requests share them (esk_rf_vector), and its pins table for the models
 * that find a function's pin (esk_rf_pin). The MCU description's
 * alternate-function table (io.alternate, an STM32's AFR choices) is kept
 * for the GPIO model that routes a pin to its peripheral and the
 * peripheral models that find their pins (esk_rf_af_signal,
 * esk_rf_af_pin). */
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
    int sci;   /* CLR, SET and INV registers follow it */
    const char *name;
    struct esk_rf_periph *p;
    struct esk_rf_field *f;
    int nf;
    /* a clock of its own, which stands for its peripheral's gate (an
     * ESP32-C3's TSENS registers in APB_SARADC, on TSENS_CLK_EN): 0 for
     * the peripheral's */
    struct esk_rf_reg *gate;
    u64 gate_mask;
    const char *gate_name;
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
    /* offsets with no register that read 0 and ignore writes, as the
     * part documents them (a PLIC's sources past the last): pairs of
     * offset and size */
    u32 *rsv;
    int nrsv;
    /* its model serves those ranges itself (an OTG core's FIFO windows):
     * its read and write hooks get the accesses there */
    int model_rsv;
    /* in a backup domain (an STM32's RTC): its registers keep their values
     * through every reset but a power-on one (the model sets it) */
    int backup;
    /* clocked from outside the core's domain (an STM32's RTC and IWDG, on
     * LSE or LSI): its time still passes while that domain's clocks are
     * stopped (esk_mach.clocks_stopped); the others' does not */
    int always_on;
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
    /* the description's interrupt table, vectors and pins */
    const char **irq_name;
    int *irq_num, *irq_vec;
    int nirq;
    const char **pin_name;
    int *pin_at; /* port * 32 + bit, port 0 being A */
    int npin;
    /* the configuration words' addresses (program addresses), by name */
    const char **cfg_name;
    u32 *cfg_at;
    int ncfg;
    /* the PPS output functions' numbers, by name */
    const char **pps_name;
    int *pps_fn;
    int npps;
    /* the MCU description's alternate functions (io.alternate): pin
     * (port * 32 + bit), AF number and signal, one entry each */
    int *af_at, *af_num;
    const char **af_sig;
    int naf;
    /* the MCU description's DMA request mapping (dma_requests): the
     * controller, stream, channel and request of each entry */
    const char **dma_ctl, **dma_sig;
    int *dma_stream, *dma_chan;
    int ndma;
    /* the MCU description's analog inputs (io.analog): pin (port * 32 +
     * bit) and its ADC channels' name (ADC123_IN0) */
    int *an_at;
    const char **an_sig;
    int nan;
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
/* the register's clock is off: its own gate, or its peripheral's */
int esk_rf_reg_gated(const struct esk_rf_reg *r);
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
/* the interrupt of that name in the description's table, or -1 */
int esk_rf_irq_named(const struct esk_rf *rf, const char *name);
/* the vector interrupt `irq` is taken through: the description's, else irq */
int esk_rf_vector(const struct esk_rf *rf, int irq);
/* the port pin function `name` is on, as port * 32 + bit (port A 0), or -1 */
int esk_rf_pin(const struct esk_rf *rf, const char *name);
/* the MCU description's alternate-function table (io.alternate) */
es_status esk_rf_alternates(struct esk_mach *m, const esk_json *alt, char *why, size_t cap);
/* the k-th signal (from 0) that alternate function `af` of the pin (port,
 * bit) carries, or 0 */
const char *esk_rf_af_signal(const struct esk_rf *rf, int port, int bit, int af, int k);
/* the k-th pin (from 0, as port * 32 + bit) that carries `sig` on an
 * alternate function, whose number goes to *af; -1 after the last */
int esk_rf_af_pin(const struct esk_rf *rf, const char *sig, int k, int *af);
/* the MCU description's DMA request mapping (dma_requests) */
es_status esk_rf_dma_requests(struct esk_mach *m, const esk_json *map, char *why, size_t cap);
/* the MCU description's analog inputs (io.analog) */
es_status esk_rf_analog(struct esk_mach *m, const esk_json *an, char *why, size_t cap);
/* the pin (port * 32 + bit) of ADC `unit`'s channel `ch`, or -1 when no
 * pin of the package carries it */
int esk_rf_analog_pin(const struct esk_rf *rf, int unit, int ch);
/* whether request `sig` is on controller `ctl`'s stream `stream`, channel
 * `chan` */
int esk_rf_dma_maps(const struct esk_rf *rf, const char *ctl, int stream, int chan, const char *sig);
/* the address of the configuration word of that name, or dflt */
u32 esk_rf_config(const struct esk_rf *rf, const char *name, u32 dflt);
/* the number of the PPS output function of that name (U2TX), or -1 */
int esk_rf_pps_out(const struct esk_rf *rf, const char *name);
/* pend that interrupt in the core */
void esk_rf_interrupt(struct esk_rf_periph *p, int irq);
/* that interrupt's request line is a level (an OTG core's): asserted or
 * not, as the core's irq_level takes it (a pulse when it rises, on a core
 * without one) */
void esk_rf_irq_level(struct esk_rf_periph *p, int irq, int level, int *was);
/* the model's clock starts (1) or stops: while it runs, its tick hook is
 * called after each instruction and its next_event asked */
void esk_rf_clock(struct esk_rf_periph *p, int *running, int on);
/* a diagnostic from a model or the register file, through the host */
void esk_rf_diag(struct esk_mach *m, int severity, const char *code, const char *fmt, ...);

#endif
