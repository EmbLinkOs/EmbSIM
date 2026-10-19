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
 *   gpio.c    the GPIO block: the strapping latch
 *   cache.c   the cache and MMU: the flash read in place on both buses
 *   spimem.c  SPI0 and SPI1, the controllers in front of the flash
 *   nor.c     the in-package flash, an XMC XM25QH32C
 *   assist.c  the Debug Assistant: the PC logging
 *   sha.c     the SHA accelerator, Typical SHA mode
 *   efuse.c   the eFuse controller and the chip's factory bits
 *   timg.c    TIMG0 and TIMG1: T0, the MWDT, the RTC_SLOW_CLK calibration
 *   systimer.c the system timer: two 52-bit counters, three comparators
 *   system.c  SYSTEM: the CPU and APB clocks, the peripherals' clock
 *             enables and resets, the software interrupts */
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

#endif
