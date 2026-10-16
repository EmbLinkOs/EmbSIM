/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* pic32.h -- a PIC32MX's peripheral models (SPEC-009 §6), over the register
 * file converted from the part's device pack (tools/importers/edc2regs.py):
 *
 *   evic.c   the interrupt controller: IFS, IEC, IPC, INTCON, INTSTAT,
 *            the core in EIC mode, vectors shared among requests
 *   osc.c    the oscillator (OSCCON): the clock the configuration words
 *            select, the PLL, the switch, PBDIV; and CFG's SYSKEY unlock
 *   tmr.c    Timer1 (type A) and Timer2 to Timer5 (type B, 32-bit pairs)
 *   gpio.c   ports A to G: TRIS, PORT, LAT, ODC, the change notices
 *            (CNCON, CNEN, CNPUE) and port B's analog pins (AD1PCFG)
 *   uart.c   UART1 to UART6, at their baud rate on their pins
 *
 * A model raises an interrupt by setting its flag (esk_pic32_irq); the
 * EVIC decides from there. The models find each other by peripheral name
 * in the register file: no file-scope state (CS-18).
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/pic32.h, pic32-evic.c,
 * pic32-cru.c, pic32-tmr.c, pic32-gpio.c, pic32-uart.c. Changes for
 * EmbSIM: the core clock is the oscillator's (EmbCC ran the peripherals
 * from the core's cycles only); the UART is timed at its baud rate and
 * drives its pins; the ports talk to the session's nets and raise the
 * change notice from them; the PIC32MZ's and PIC24's parts of the shared
 * port model are left out. */
#ifndef ESK_MCU_PIC32_H
#define ESK_MCU_PIC32_H

#include "mcu/rf.h"

/* the configuration words in boot flash */
#define PIC32MX_DEVCFG3 0x1fc02ff0u
#define PIC32MX_FRC_HZ 8000000u
#define PIC32MX_LPRC_HZ 31250u
#define PIC32MX_SOSC_HZ 32768u

/* evic.c: the hardware sets interrupt request irq's flag (IFSx) */
void esk_pic32_irq(struct esk_mach *m, int irq);
/* the request's flag is set, or it is enabled (IECx with a priority) */
int esk_pic32_irq_flag(struct esk_mach *m, int irq);
int esk_pic32_irq_enabled(struct esk_mach *m, int irq);
/* a persistent source: its flag sets again as soon as it is cleared while
 * `cond` holds (a UART transmitter with room) */
void esk_pic32_irq_level(struct esk_mach *m, int irq, int (*cond)(void *ctx), void *ctx);
/* the controller looks at the persistent sources again */
void esk_pic32_irq_update(struct esk_mach *m);
/* a configuration word: DEVCFG0 to DEVCFG3 (all ones when unreadable) */
u32 esk_pic32_devcfg(struct esk_mach *m, int n);

/* osc.c: SYSCLK in Hz, and the core cycles per peripheral bus clock */
u32 esk_pic32_sysclk(struct esk_mach *m);
u32 esk_pic32_pb_div(struct esk_mach *m);
/* SYSKEY's unlock sequence was written, and not locked again since */
int esk_pic32_unlocked(struct esk_mach *m);

/* uart.c: what the UARTs make of a port pin: 0 nothing, 1 a transmitter's
 * TX (driven), 2 a receiver's RX (an input); and where a UART's pins are */
int esk_pic32_uart_role(struct esk_mach *m, int port, int bit);
int esk_pic32_uart_pin(struct esk_mach *m, const char *unit, int tx, int *port, int *bit);

/* gpio.c: the pins' outside levels or a peripheral's use of them changed */
void esk_pic32_pins_changed(struct esk_mach *m);

extern const struct esk_dev_ops esk_pic32_evic_ops, esk_pic32_osc_ops, esk_pic32_cfg_ops,
    esk_pic32_tmr_ops, esk_pic32_gpio_ops, esk_pic32_uart_ops;
void *esk_pic32_evic_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_pic32_osc_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_pic32_cfg_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_pic32_tmr_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_pic32_gpio_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_pic32_uart_create(struct esk_mach *m, struct esk_rf_periph *p);

#endif
