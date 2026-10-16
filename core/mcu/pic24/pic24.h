/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* pic24.h -- the PIC24F core (and a dsPIC33E's) and a PIC24FJ's peripheral
 * models (SPEC-009 §2, §6).
 *
 *   pic24.c   the core: the PIC24F instruction set (24-bit words, the W
 *             registers memory-mapped, the addressing modes, REPEAT, the
 *             iterative divide), the data space with its Extended Data
 *             Space and program space visibility, the table reads,
 *             exceptions (traps and interrupts) through the vector table,
 *             DISI, PWRSAV; a dsPIC33E's: the PIC24E's instructions and
 *             timing, the DO loops, DIVF
 *   dsp.c     a dsPIC33E's DSP engine and its address generators' modulo
 *             and bit-reversed addressing
 *   disasm.c  the instruction at an address, for the Debug perspective
 *   intc.c    the interrupt controller: IFS, IEC, IPC, INTCON1/2/4, INTTREG
 *   osc.c     the oscillator (OSCCON's unlock, COSC from FNOSC, CLKDIV) and
 *             the Peripheral Pin Select lock
 *   tmr.c     Timer1 to Timer5, 32-bit pairs
 *   gpio.c    ports A to G: TRIS, PORT, LAT, ODC, ANS, the pulls, the
 *             interrupt-on-change, and the PPS's outputs and inputs
 *   uart.c    UART1 to UART6, at their baud rate on their PPS pins
 *
 * The bus holds both of the part's spaces:
 *   - program memory at 0: the word at program address P (P even) is the
 *     four bytes at 2P -- its low, middle and high bytes and a phantom 0, as
 *     an XC16 image lays them out, so an ELF segment's bytes go to twice its
 *     load address (the core's `phys` hook);
 *   - the data space at PIC24_DATA, by its 24-bit Extended Data Space
 *     address: the SFRs and RAM below 0x8000, EDS page 1 (RAM above 0x8000)
 *     at 0x8000.
 * The machine's cycles are instruction cycles (Tcy): the core's clock is
 * Fcy, half the oscillator's.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/pic24.h, pic24.c,
 * pic24-dsp.c, disasm.c (its PIC24 part), pic24-intc.c, pic24-osc.c,
 * pic24-tmr.c, pic32-gpio.c (its PIC24 part), pic24-uart.c. Changes for
 * EmbSIM: the core state is every function's parameter (no file-scope `ps`,
 * CS-18); PWRSAV and a branch to itself sleep for the runtime (ADR-019),
 * where EmbCC ended the run; traps are diagnostics, not stderr; the UART is
 * timed at its baud rate on the pins the PPS gives it; the ports talk to
 * the session's nets. */
#ifndef ESK_MCU_PIC24_H
#define ESK_MCU_PIC24_H

#include "mcu/rf.h"


#define PIC24_DATA 0x2000000u
/* the configuration memory's program addresses start here */
#define PIC24_CONFIG 0x800000u

/* SR */
#define SR_C 0x0001u
#define SR_Z 0x0002u
#define SR_OV 0x0004u
#define SR_N 0x0008u
#define SR_RA 0x0010u
#define SR_IPL 0x00e0u
#define SR_DC 0x0100u
/* a dsPIC's: the DO loop's, the DSP engine's */
#define SR_DA 0x0200u
#define SR_SAB 0x0400u
#define SR_OAB 0x0800u
#define SR_SB 0x1000u
#define SR_SA 0x2000u
#define SR_OB 0x4000u
#define SR_OA 0x8000u
/* CORCON */
#define CORCON_IF 0x0001u
#define CORCON_RND 0x0002u
#define CORCON_SFA 0x0004u /* the PIC24E's and dsPIC33E's */
#define CORCON_IPL3 0x0008u
#define CORCON_ACCSAT 0x0010u
#define CORCON_SATDW 0x0020u
#define CORCON_SATB 0x0040u
#define CORCON_SATA 0x0080u
#define CORCON_DL 0x0700u
#define CORCON_EDT 0x0800u
#define CORCON_US 0x3000u
#define CORCON_VAR 0x8000u

/* the traps, by their vector (0x000004 + 2n) */
enum { TRAP_OSC, TRAP_ADDR, TRAP_HARD, TRAP_STACK, TRAP_MATH, TRAP_RES5, TRAP_SOFT, TRAP_RES7 };

/* What the interrupt controller gives the core: the request it presents
 * (the highest priority enabled, then the lowest number), and what the core
 * tells it -- that it took one, and of the traps it raises, which set
 * INTCON1's status bits. */
struct pic24_intc {
    /* the priority (1 to 7) of the request presented, and its interrupt
     * number; 0 when there is none */
    int (*request)(void *ctx, int *irq);
    /* the core took interrupt irq, now at priority ipl */
    void (*taken)(void *ctx, int irq, int ipl);
    /* a condition of the math error trap's (PIC24_MATH_*): whether INTCON1
     * makes it one (OVATE, OVBTE, COVTE; the others always), its status
     * bit set (OVAERR...) when it does */
    int (*math)(void *ctx, int why);
    /* the core raised trap n (TRAP_*) */
    void (*trap)(void *ctx, int n);
    /* INTCON1.NSTDIS: interrupts do not nest */
    int (*nstdis)(void *ctx);
    void *ctx;
};

/* What a flash controller gives the core: its write latches, which TBLWTL
 * and TBLWTH write and the table reads read at 0xFA0000 (TBLPAG 0xFA) */
struct pic24_nvm {
    u32 (*latch_read)(void *ctx, u32 addr);
    void (*latch_write)(void *ctx, u32 addr, int high, int n, u16 v);
    void *ctx;
};

#define PIC24_LATCHES 0xfa0000u

/* the math error trap's conditions */
enum { PIC24_MATH_DIV0, PIC24_MATH_OVA, PIC24_MATH_OVB, PIC24_MATH_COVA, PIC24_MATH_COVB, PIC24_MATH_SFTAC };

/* the DO loops' stack (a dsPIC33E's): four loops deep */
#define PIC24_DO_LEVELS 4

struct pic24_state {
    struct esk_cpu cpu; /* the interface; first */
    struct esk_mach *sim;

    u16 w[16];
    u16 winit; /* the W registers written since reset */
    u16 splim, sr, corcon, dsrpag, dswpag, rcount, tblpag, disicnt;
    int splim_set; /* SPLIM written since reset */
    u32 pc;        /* the instruction executing */
    u32 npc;       /* where execution goes after it */
    u16 sw[4], ssr; /* PUSH.S's shadows: W0-W3, SR's flags */
    u32 insn;
    int size;    /* its words */
    int changed; /* it wrote a register or memory */
    int extra;   /* cycles it takes beyond one */
    int trap;    /* the trap it raised (TRAP_* + 1), or 0 */
    u32 trap_pc; /* the last trap's instruction */
    char trap_why[96];
    u8 traps;     /* traps pending, by number */
    int traps_told; /* the diagnostics given for traps */
    int sleeping; /* PWRSAV: until an interrupt */
    u32 prog_end; /* past the last program address */
    u64 disi_at;  /* the cycle DISICNT was last counted at */
    struct pic24_intc *intc;
    struct pic24_nvm *nvm;
    /* NVMKEY's unlock: 0x55 then 0xAA (key_step 2) at instruction key_at */
    int key_step;
    u64 key_at;
    /* the family: the PIC24E's and dsPIC33E's instructions and timing (e),
     * and a dsPIC's DSP engine (dsp) */
    int e, dsp;
    u16 ram_base; /* the RAM's start: W15 out of reset */
    /* the DSP engine's accumulators (40 bits, kept sign-extended) */
    s64 acc[2];
    /* the DO loop: its count, first and last instructions, the levels
     * nested (CORCON.DL) saved; EDT written: it ends at this pass's end */
    u16 dcount;
    u32 dostart, doend;
    struct {
        u16 dcount;
        u32 dostart, doend;
    } dostack[PIC24_DO_LEVELS];
    int edt;
    /* the address generators: MODCON, the modulo buffers, XBREV; the Y data
     * space [y_lo, y_hi) */
    u16 modcon, xmodsrt, xmodend, ymodsrt, ymodend, xbrev, mstrpr;
    u32 y_lo, y_hi;
};

/* ---- the core's own, between pic24.c and dsp.c --------------------------- */

int pic24_core_read(struct pic24_state *ps, u32 ea, int n, u16 *v);
int pic24_core_write(struct pic24_state *ps, u32 ea, int n, u16 v);
void pic24_core_set_w(struct pic24_state *ps, int n, u16 v);
int pic24_core_pointer(struct pic24_state *ps, int n);
int pic24_core_src(struct pic24_state *ps, int mode, int r, int wb, int n, u16 *v);
int pic24_core_dst(struct pic24_state *ps, int mode, int r, int wb, int n, u16 v);
int pic24_core_bad_opcode(struct pic24_state *ps);
int pic24_core_trap(struct pic24_state *ps, int n, const char *why);
void pic24_core_math(struct pic24_state *ps, int why);
int pic24_dsp_exec(struct pic24_state *ps, u32 i);
void pic24_dsp_mul_acc(struct pic24_state *ps, int a, s64 product);
u16 pic24_agu_step(struct pic24_state *ps, int r, u16 w, int d, int write, int y);
u16 pic24_agu_offset(struct pic24_state *ps, int r, u16 ea, int neg, int y);
u16 pic24_acc_read(struct pic24_state *ps, u32 a);
void pic24_acc_write(struct pic24_state *ps, u32 a, u16 v);

/* ---- the interface for the MCU and its devices ------------------------- */

/* a core of `model`: "pic24fj" (a PIC24FJ's, the PIC24FJ1024GB610's),
 * "pic24ep" or "dspic33ep"; its program memory ends at prog_end (a program
 * address), its RAM starts at ram_base (a data address) */
es_status esk_pic24_create(struct esk_mach *m, const char *model, u32 prog_end, u16 ram_base,
                           struct esk_cpu **out);
int esk_pic24_is(const struct esk_cpu *c);
void esk_pic24_intc_attach(struct esk_mach *m, struct pic24_intc *ic);
/* a word of program memory (24 bits) at an even program address */
u32 esk_pic24_prog(struct esk_mach *m, u32 addr);
/* the instruction at `pc` (word `i`, `i2` the next): its text and its size
 * in program address units (2 or 4) */
int esk_pic24_disasm(u32 pc, u32 i, u32 i2, char *text, size_t cap);
/* the oscillator: Fcy (instruction cycles per second) */
u32 esk_pic24_fcy(struct esk_mach *m);
/* the interrupt controller's request changed: a waiting core may wake */
void esk_pic24_intc_changed(struct esk_mach *m);

/* intc.c: the hardware sets interrupt irq's flag (IFSx); whether irq is
 * enabled with a priority; a persistent source (its flag set while cond
 * holds); the request evaluated again */
void esk_pic24_irq(struct esk_mach *m, int irq);
int esk_pic24_irq_enabled(struct esk_mach *m, int irq);
void esk_pic24_irq_level(struct esk_mach *m, int irq, int (*cond)(void *ctx), void *ctx);
void esk_pic24_irq_update(struct esk_mach *m);

/* gpio.c: the pins' outside levels or a peripheral's use of them changed;
 * the port pin (port * 32 + bit) an RP or RPI pin is, or -1; the RP pin a
 * PPS output function drives, the first, or -1; the RP pin an input
 * field of RPINRx (U2RXR) names, or -1 */
void esk_pic24_pins_changed(struct esk_mach *m);
/* the PPS's registers changed: the UARTs' pins found again */
void esk_pic24_pps_changed(struct esk_mach *m);
int esk_pic24_rp_pin(struct esk_mach *m, int rp);
int esk_pic24_pps_out_rp(struct esk_mach *m, int fn);
int esk_pic24_pps_in_rp(struct esk_mach *m, const char *field);
/* uart.c: what the UARTs make of a port pin: 0 nothing, 1 a transmitter's
 * TX (driven), 2 a receiver's RX; and where a UART's pins are */
int esk_pic24_uart_role(struct esk_mach *m, int port, int bit);
int esk_pic24_uart_pin(struct esk_mach *m, const char *unit, int tx, int *port, int *bit);
void esk_pic24_uart_repin(struct esk_mach *m);

extern const struct esk_dev_ops esk_pic24_intc_ops, esk_pic24_osc_ops, esk_pic24_pps_ops, esk_pic24_tmr_ops,
    esk_pic24_gpio_ops, esk_pic24_uart_ops;
void *esk_pic24_intc_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_pic24_osc_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_pic24_pps_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_pic24_tmr_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_pic24_gpio_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_pic24_uart_create(struct esk_mach *m, struct esk_rf_periph *p);

#endif
