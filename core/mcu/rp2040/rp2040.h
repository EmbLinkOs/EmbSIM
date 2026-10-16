/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* rp2040.h -- the Raspberry Pi RP2040's peripheral models (core/mcu/rp2040),
 * over the register file built from RP2040.svd (the Pico SDK's), and what
 * they share: the clocks, the microsecond tick, the pins.
 *
 * The part's bus has its own rules (rf.h): every peripheral's registers
 * again at its atomic XOR, set and clear aliases, and byte writes across
 * the word. The models find each other by their peripheral's name. Time is
 * the core's cycles, as everywhere in EmbSIM, and the core runs at clk_sys:
 * the program sets the clocks up (the ring oscillator, the crystal, the
 * PLLs, the clock generators), the clock model is the machine's core_hz,
 * and a change of clk_sys ends the runtime's quantum. A timer counts the
 * watchdog's microsecond ticks of clk_ref whatever clk_sys is.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/rp2040*.c, on
 * EmbSIM's register file, pins and serial lines (the UARTs are timed at
 * their baud rate here). */
#ifndef ESK_MCU_RP2040_H
#define ESK_MCU_RP2040_H

#include "mcu/rf.h"

/* the clocks, as the clock generators number them (CLK_GPOUT0..) */
enum {
    RP_CLK_GPOUT0, RP_CLK_GPOUT1, RP_CLK_GPOUT2, RP_CLK_GPOUT3,
    RP_CLK_REF, RP_CLK_SYS, RP_CLK_PERI, RP_CLK_USB, RP_CLK_ADC, RP_CLK_RTC,
    RP_NCLK
};

/* the ring oscillator's nominal frequency at boot (the datasheet's 2.17.1;
 * its 2.15.2.1 says "typically 6MHz"), and the crystal the Pico has when
 * the board names none */
#define RP_ROSC_HZ 6500000u
#define RP_XOSC_HZ 12000000u

/* a clock's frequency now, in Hz: 0 when it is stopped (clocks.c) */
u32 esk_rp2040_hz(struct esk_mach *m, int clk);
/* the crystal's frequency: the board's, else the Pico's 12 MHz */
u32 esk_rp2040_xtal_hz(struct esk_mach *m);
/* a clock may have changed: clk_sys is the core's again, the tick
 * generator and the UARTs see it */
void esk_rp2040_clock_change(struct esk_mach *m);
/* the watchdog's tick generator: the clk_ref cycles a microsecond tick
 * takes (TICK.CYCLES), or 0 while it is stopped */
u32 esk_rp2040_tick_cycles(struct esk_mach *m);

/* ---- the pins (io.c) -------------------------------------------------- */

/* the SIO's outputs to the user bank (GPIO_OUT, GPIO_OE) changed */
void esk_rp2040_sio_pins(struct esk_mach *m, u32 out, u32 oe);
/* what the SIO's GPIO_IN reads: the user bank's inputs, after INOVER */
u32 esk_rp2040_gpio_in(struct esk_mach *m);
/* the board drove a pin or let it go */
void esk_rp2040_pins_changed(struct esk_mach *m);
/* where a UART's TX or RX is: the first pin whose function is that */
int esk_rp2040_uart_pin(struct esk_mach *m, const char *unit, int tx, int *port, int *bit);

/* ---- between the models --------------------------------------------- */

/* n microsecond ticks came: the timer counts (timer.c); and the ticks
 * until its next alarm that raises an interrupt, 0 when none is armed */
void esk_rp2040_timer_ticks(struct esk_mach *m, u32 n);
int esk_rp2040_timer_next(struct esk_mach *m, u32 *ticks);
/* a clock started or stopped */
void esk_rp2040_tick_clock(struct esk_mach *m);
void esk_rp2040_uart_kick(struct esk_mach *m);
/* IO_QSPI's chip select override changed (ssi.c) */
void esk_rp2040_ssi_select(struct esk_mach *m);

/* a field's value by its mask, and v with it set to x */
u32 esk_rp_fld(const struct esk_rf_reg *r, u64 mask);
u64 esk_rp_put(u64 v, u64 mask, u32 x);
/* the model of the peripheral of that name, when it has that model */
void *esk_rp_model(struct esk_mach *m, const char *name, const struct esk_dev_ops *ops);

extern const struct esk_dev_ops esk_rp2040_resets_ops, esk_rp2040_clocks_ops, esk_rp2040_xosc_ops,
    esk_rp2040_rosc_ops, esk_rp2040_pll_ops, esk_rp2040_psm_ops, esk_rp2040_watchdog_ops,
    esk_rp2040_timer_ops, esk_rp2040_pads_ops, esk_rp2040_io_ops, esk_rp2040_sio_ops,
    esk_rp2040_uart_ops, esk_rp2040_ssi_ops;
void *esk_rp2040_resets_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_rp2040_clocks_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_rp2040_xosc_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_rp2040_rosc_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_rp2040_pll_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_rp2040_psm_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_rp2040_watchdog_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_rp2040_timer_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_rp2040_pads_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_rp2040_io_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_rp2040_sio_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_rp2040_uart_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_rp2040_ssi_create(struct esk_mach *m, struct esk_rf_periph *p);

/* boot2's CRC: CRC-32/MPEG-2, the bootrom's (boot.c) */
u32 esk_rp2040_crc32(const u8 *p, u32 n);
/* the bootrom's flash boot, at a reset (boot.c), and the peripherals it
 * takes out of reset (clocks.c) */
void esk_rp2040_boot(struct esk_mach *m);
/* sio.c: core 1 powered off by the PSM (FRCE_OFF.PROC1), or on again into
 * its bootrom */
void esk_rp2040_core1_power(struct esk_mach *m, int on);
void esk_rp2040_bootrom_resets(struct esk_mach *m);

#endif
