/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* avr.h -- the AVR core and the ATmega328P's peripheral models (SPEC-009
 * §2, §6).
 *
 *   avr.c     the core: the AVR5 instruction set of the ATmega328P, SREG,
 *             the data space, interrupts, sleep, the CPU interface
 *   disasm.c  the AVR instruction at an address, for the Debug perspective
 *   port.c    PORTB, PORTC, PORTD: PINx, DDRx, PORTx and the pins
 *   exint.c   INT0, INT1 and the pin-change interrupts
 *   timer.c   Timer/Counter0 and 2 (8-bit) and 1 (16-bit), their OC pins
 *   usart.c   USART0 at its baud rate, on PD1 and PD0
 *   spi.c     the SPI as a master, on PB2 to PB5
 *   twi.c     the TWI (I2C) as a master, on PC4 and PC5
 *   adc.c     the 10-bit ADC from the pins' voltages
 *   cpu.c     CLKPR (the clock prescaler), SMCR, MCUCR's PUD
 *   eeprom.c  the 1 KiB EEPROM
 *   wdt.c     the watchdog timer, its interrupt and its reset
 *
 * Program memory (flash) is on the bus at 0 by byte address; the data
 * space at ESK_AVR_DATA + its address, as GDB's AVR target numbers them.
 * The core answers for the registers r0-r31, SP and SREG itself; the I/O
 * registers are the register file's (an MCU description's peripherals all
 * have base ESK_AVR_DATA, so a register's offset is its data address).
 *
 * Interrupts are levels, as on the part: each source says whether its
 * vector is requested (its flag and enable) and is acknowledged when the
 * core enters the vector, which clears a flag the hardware clears.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/avr.h, avr.c,
 * avr-timer16.c, avr-usart.c. EmbCC had no ports, external interrupts,
 * 8-bit timers, ADC or clock prescaler; those are EmbSIM's. */
#ifndef ESK_MCU_AVR_H
#define ESK_MCU_AVR_H

#include "mcu/rf.h"

#define ESK_AVR_DATA 0x800000u
#define ESK_AVR_VECTORS 26

/* the ATmega328P's core: flash words and RAMEND from the model name */
es_status esk_avr_create(struct esk_mach *m, const char *model, struct esk_cpu **out);
int esk_avr_is(const struct esk_cpu *c);

/* a peripheral's interrupt source on the core: `pending` says whether
 * vector `vec` is requested now, `ack` is the hardware clearing its flag
 * as the vector is entered (0 when the program clears it) */
void esk_avr_irq_source(struct esk_mach *m, int vec, int (*pending)(void *ctx, int vec),
                        void (*ack)(void *ctx, int vec), void *ctx);
/* a source's flag or enable changed: the core looks again */
void esk_avr_irq_changed(struct esk_mach *m);
/* the watchdog's: WDR starts its count again */
void esk_avr_wdr_hook(struct esk_mach *m, void (*wdr)(void *ctx), void *ctx);

/* the instruction at byte address `pc` (w, then w2 for a two-word one):
 * its text and its size in bytes */
int esk_avr_disasm(u32 pc, unsigned w, unsigned w2, char *text, size_t cap);

/* ---- the ATmega328P's peripherals ------------------------------------- */

extern const struct esk_dev_ops esk_avr_port_ops, esk_avr_exint_ops, esk_avr_tc8_ops,
    esk_avr_tc16_ops, esk_avr_usart_ops, esk_avr_adc_ops, esk_avr_cpu_ops, esk_avr_eeprom_ops, esk_avr_spi_ops,
    esk_avr_twi_ops, esk_avr_wdt_ops;
void *esk_avr_port_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_avr_exint_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_avr_tc8_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_avr_tc16_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_avr_usart_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_avr_adc_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_avr_cpu_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_avr_eeprom_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_avr_spi_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_avr_twi_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_avr_wdt_create(struct esk_mach *m, struct esk_rf_periph *p);

/* MCUCR.PUD: the pull-ups are disabled */
int esk_avr_pud(struct esk_mach *m);
/* a port's pins as the program sees them (PINx), and an output compare
 * unit taking a pin over (level -1 gives it back to PORTx) */
int esk_avr_pin_level(struct esk_mach *m, int port, int bit);
/* a pin's level now: its port looks at the circuit again first */
int esk_avr_pin_read(struct esk_mach *m, int port, int bit);
void esk_avr_pin_override(struct esk_mach *m, int port, int bit, int level);
/* a peripheral holds a pin whatever DDRx says: -1 lets go, 0 or 1 drives
 * it, 2 makes it an input (PORTx's pull-up still applies) */
void esk_avr_pin_force(struct esk_mach *m, int port, int bit, int how);
/* the pins' outside levels changed: every port's PINx again, then the
 * external interrupts' edges */
void esk_avr_pins_changed(struct esk_mach *m);
/* exint.c: a pin's new level, for INTn and PCINTn */
void esk_avr_exint_pin(struct esk_mach *m, int port, int bit, int level);
/* USART0's pins: PD1 (TX) and PD0 (RX) */
int esk_avr_usart_pin(struct esk_mach *m, const char *unit, int tx, int *port, int *bit);

#endif
