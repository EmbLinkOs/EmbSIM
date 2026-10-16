/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* nrf52.h -- the Nordic nRF52840's peripheral models (core/mcu/nrf52), over
 * the register file built from Nordic's nrf52840.svd, and what they share.
 *
 * Every peripheral has Nordic's tasks, events and interrupts (the nRF52840
 * Product Specification v1.1's "Peripheral interface"): writing 1 to a task
 * register (offsets below 0x100) triggers it; an event register (0x100 +
 * 4n) is set to 1 by the hardware and cleared by writing 0; INTEN (0x300)
 * holds an enable bit per event (bit n for the event at 0x100 + 4n),
 * INTENSET (0x304) and INTENCLR (0x308) set and clear its bits and read it.
 * The peripheral's interrupt is raised while an enabled event is set: when
 * it sets, and again when an access leaves it set. SHORTS connect an event
 * to a task at once.
 *
 * The core runs at 64 MHz whatever the high-frequency clock's source; the
 * 32.768 kHz clock (LFCLK) runs once started, and the RTCs count on it.
 * Pin n of port p is the runtime's pin GPIO(32p + n).
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/nrf52.c (CLOCK, the GPIO
 * ports, the UARTs, TIMER, RTC), on EmbSIM's register file, pins and serial
 * lines: the UARTs are timed at their baud rate and drive their pins, and
 * GPIOTE is written for EmbSIM. */
#ifndef ESK_MCU_NRF52_H
#define ESK_MCU_NRF52_H

#include "mcu/rf.h"

#define NRF_CPU_HZ 64000000u
#define NRF_DATA_RAM 0x20000000u
#define NRF_DATA_RAM_SIZE 0x40000u

/* the peripheral interface: tasks, events, INTEN, the interrupt */
struct nrfp {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    int irq;
    u32 inten;
    struct esk_rf_reg **at; /* the registers by offset / 4, below 0x1000 */
};

int esk_nrfp_init(struct nrfp *x, struct esk_mach *s, struct esk_rf_periph *p);
struct esk_rf_reg *esk_nrfp_reg(const struct nrfp *x, u32 off);
u32 esk_nrfp_rd(const struct nrfp *x, u32 off);
void esk_nrfp_hw(struct nrfp *x, u32 off, u32 v);
/* an enabled event is set */
int esk_nrfp_line(const struct nrfp *x);
/* the interrupt: when it rises, and with `again` when it stays up */
void esk_nrfp_irq(struct nrfp *x, int was, int again);
/* an event happens */
void esk_nrfp_event(struct nrfp *x, u32 off);
/* INTEN, INTENSET, INTENCLR: 1 when the access was one of them */
int esk_nrfp_inten_read(struct nrfp *x, u32 off);
int esk_nrfp_inten_write(struct nrfp *x, u32 off, u32 v);

/* the 32.768 kHz clock runs (clock.c) */
int esk_nrf_lfclk_on(struct esk_mach *m);

/* ---- the pins (gpio.c, gpiote.c, uart.c) ------------------------------ */

/* a pin's number, 32 x port + pin, from a PSEL register's value: -1 when
 * it is disconnected */
int esk_nrf_psel(u32 v);
/* the board drove a pin, or a peripheral took a pin or let it go: both
 * ports again */
void esk_nrf_pins_changed(struct esk_mach *m);
/* a pin's level now, 0 or 1 (as the ports last worked it out) */
int esk_nrf_pin_level(struct esk_mach *m, int pin);

#define NRF_PINS 48

/* what the peripherals do with each pin: role[n] 1 for a UART's TXD
 * (driven, idle high), 2 for its RXD (an input); drive[n] the level a
 * GPIOTE task-mode channel gives it (-1 none), watch[n] whether an
 * event-mode channel watches it */
void esk_nrf_uart_roles(struct esk_mach *m, signed char *role);
void esk_nrf_gpiote_roles(struct esk_mach *m, signed char *drive, u8 *watch);
int esk_nrf_uart_pin(struct esk_mach *m, const char *unit, int tx, int *port, int *bit);
/* the ports' levels and DETECT signal after a change, for GPIOTE */
void esk_nrf_gpiote_levels(struct esk_mach *m, const u32 *levels, int detect);

extern const struct esk_dev_ops esk_nrf_clock_ops, esk_nrf_gpio_ops, esk_nrf_gpiote_ops, esk_nrf_uart_ops,
    esk_nrf_timer_ops, esk_nrf_rtc_ops;
void *esk_nrf_clock_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_nrf_gpio_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_nrf_gpiote_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_nrf_uart_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_nrf_timer_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_nrf_rtc_create(struct esk_mach *m, struct esk_rf_periph *p);

#endif
