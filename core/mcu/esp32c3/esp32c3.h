/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* esp32c3.h -- the ESP32-C3's peripheral models (ESP32-C3 Technical
 * Reference Manual v1.4), over the register file converted from
 * Espressif's esp32c3.svd.
 *
 *   intmtx.c  the interrupt matrix (62 sources mapped onto CPU interrupts
 *             1-31) and the CPU's interrupt controller: enable, type,
 *             priority, threshold, the pending state the core claims
 *   uart.c    UART0 and UART1: the FIFOs, frames at the baud rate, the
 *             interrupts
 *   rtc.c     RTC_CNTL: the reset causes, the RTC timer, the RTC watchdog,
 *             the software resets
 *   gpio.c    the GPIO matrix and IO MUX on the pins, the strapping latch
 *   cache.c   the cache and MMU: the flash read in place on both buses
 *   spimem.c  SPI0 and SPI1, the controllers in front of the flash
 *   nor.c     the in-package flash, an XMC XM25QH32C
 *   assist.c  the Debug Assistant: the PC logging
 *   sha.c     the SHA accelerator, Typical SHA mode
 *   efuse.c   the eFuse controller and the chip's factory bits
 *   timg.c    TIMG0 and TIMG1: T0, the MWDT, the RTC_SLOW_CLK calibration
 *   systimer.c the system timer: two 52-bit counters, three comparators
 *   system.c  SYSTEM: the CPU and APB clocks, the peripherals' clock
 *             enables and resets, the software interrupts
 *   regi2c.c  the internal I2C master to the analog blocks (BBPLL, bias,
 *             regulators), as the ROM's regi2c routines use it
 *   apbctrl.c APB_CTRL: the random number generator's RND_DATA
 *   usbjtag.c the USB Serial/JTAG controller's CDC-ACM side, with no host
 *   rmt.c     the remote control peripheral: two transmitters, two
 *             receivers, its RAM
 *   ledc.c    the LED PWM controller: four timers, six PWM generators,
 *             fading
 *   radio.c   the radio's register blocks the TRM leaves out (FE2, FE,
 *             NRX), which keep what is written */
#ifndef ESK_MCU_ESP32C3_H
#define ESK_MCU_ESP32C3_H

#include "mcu/rf.h"

/* the interrupt matrix's sources (TRM table 8.3-1) */
#define ESP32C3_NSOURCES 62

/* a peripheral's interrupt source `src` (0-61) is at `level` */
void esk_esp32c3_source(struct esk_mach *m, int src, int level);

extern const struct esk_dev_ops esk_esp32c3_intmtx_ops;
void *esk_esp32c3_intmtx_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32c3_uart_ops;
void *esk_esp32c3_uart_create(struct esk_mach *s, struct esk_rf_periph *p);

extern const struct esk_dev_ops esk_esp32c3_rtc_ops;
void *esk_esp32c3_rtc_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32c3_gpio_ops;
void *esk_esp32c3_gpio_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32c3_iomux_ops;
void *esk_esp32c3_iomux_create(struct esk_mach *s, struct esk_rf_periph *p);
/* the board drove a pin or let it go: the levels again */
void esk_esp32c3_pins_changed(struct esk_mach *m);
/* where a UART's TXD (tx) or RXD is: 0 and the runtime's port and bit */
int esk_esp32c3_uart_pin(struct esk_mach *m, const char *unit, int tx, int *port, int *bit);
/* a peripheral output signal of the GPIO matrix (table 5.11-1) driven by
 * its model: its level and enable */
void esk_esp32c3_signal(struct esk_mach *m, int sig, int level, int oe);
/* a peripheral input signal's level as the matrix or the IO MUX gives it */
int esk_esp32c3_signal_in(struct esk_mach *m, int sig);
/* fn(ctx) whenever a pin's input changes; 0, or -1 when no room is left */
int esk_esp32c3_signal_watch(struct esk_mach *m, void (*fn)(void *ctx), void *ctx);

extern const struct esk_dev_ops esk_esp32c3_cache_ops;
void *esk_esp32c3_cache_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32c3_spimem_ops;
void *esk_esp32c3_spimem_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32c3_efuse_ops;
void *esk_esp32c3_efuse_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32c3_timg_ops;
void *esk_esp32c3_timg_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32c3_systimer_ops;
void *esk_esp32c3_systimer_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32c3_system_ops;
void *esk_esp32c3_system_create(struct esk_mach *s, struct esk_rf_periph *p);
/* the internal I2C master to the analog blocks at 0x6000E000 (regi2c.c),
 * mapped by the SYSTEM model: 0, or -1 */
int esk_esp32c3_regi2c_create(struct esk_mach *s);
/* the radio's register blocks FE2, FE and NRX (radio.c), mapped by the
 * SYSTEM model: 0, or -1 */
int esk_esp32c3_radio_create(struct esk_mach *s);
extern const struct esk_dev_ops esk_esp32c3_sha_ops;
void *esk_esp32c3_sha_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32c3_assist_ops;
void *esk_esp32c3_assist_create(struct esk_mach *s, struct esk_rf_periph *p);
/* the CPU is reset: the PC logging keeps the PC and SP it was at */
void esk_esp32c3_assist_cpu_reset(struct esk_mach *m);

/* the flash's bytes from `addr` on, `len` of them, were programmed or
 * erased: the cache's pages over them are stale */
void esk_esp32c3_flash_written(struct esk_mach *m, u32 addr, u32 len);

/* the flash chip (nor.c), on the description's `flash` store */
#define ESP32C3_NOR_DEVICE_ID 0x15
struct esk_c3_nor;
struct esk_c3_nor *esk_esp32c3_nor_create(struct esk_mach *m);
void esk_esp32c3_nor_power_on(struct esk_c3_nor *f);
/* one transaction: `ntx` bytes the chip receives (the instruction first),
 * then `nrx` bytes it answers with */
void esk_esp32c3_nor_xfer(struct esk_c3_nor *f, const u8 *tx, int ntx, u8 *rx, int nrx);
/* a program, erase or status write is in progress (BUSY) */
int esk_esp32c3_nor_busy(struct esk_c3_nor *f);

/* APB_CLK, in Hz */
u32 esk_esp32c3_apb_hz(struct esk_mach *m);
/* a reset with TRM table 6.1-1's `code` (0x03 a software system reset,
 * 0x10 the RWDT's system reset, ...), by its class: the CPU alone, the
 * core (the RTC domain kept), or the system */
void esk_esp32c3_reset(struct esk_mach *m, int code);
/* eFuse WDT_DELAY_SEL */
int esk_esp32c3_efuse_wdt_delay_sel(struct esk_mach *m);
extern const struct esk_dev_ops esk_esp32c3_usbjtag_ops;
void *esk_esp32c3_usbjtag_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32c3_rmt_ops;
void *esk_esp32c3_rmt_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32c3_ledc_ops;
void *esk_esp32c3_ledc_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32c3_apbctrl_ops;
void *esk_esp32c3_apbctrl_create(struct esk_mach *s, struct esk_rf_periph *p);

#endif
