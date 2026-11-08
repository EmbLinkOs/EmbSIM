/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* esp32.h -- the ESP32's peripheral models (ESP32 Technical Reference
 * Manual v5), over the register file converted from Espressif's esp32.svd,
 * for the framework's Xtensa LX6 cores (mcu/xtensa/lx6.c).
 *
 *   efuse.c   the eFuse controller and the chip's factory bits
 *   dport.c   DPORT: the APP_CPU's reset, clock and stall, the CPU clock,
 *             the caches and their flash MMU, the interrupt matrix, the
 *             cross-CPU interrupts
 *   rtc.c     RTC_CNTL: the reset causes, the RTC timer and watchdog, the
 *             software resets and stalls, the store registers
 *   uart.c    UART0-2: FIFOs, frames at the baud rate, interrupts
 *   timg.c    TIMG0 and TIMG1: T0, T1, the MWDT, RTC_SLOW_CLK's calibration
 *   gpio.c    the GPIO matrix, the IO MUX and the strapping latch
 *   spi.c     SPI0 and SPI1 in front of the flash, and the flash itself
 *   apbctrl.c APB_CTRL: the CPU's and APB's clock dividers
 *   sha.c     the SHA accelerator
 *   misc.c    the random number generator, the AHB view of the
 *             peripherals */
#ifndef ESK_MCU_ESP32_SOC_H
#define ESK_MCU_ESP32_SOC_H

#include "mcu/rf.h"

/* the interrupt matrix's sources (TRM table 4.3-2) */
#define ESP32_NSOURCES 69

/* a peripheral's interrupt source `src` (0-68) is at `level`, for both
 * CPUs or (_cpu) one CPU's matrix (GPIO's PRO_CPU and APP_CPU interrupts) */
void esk_esp32_source(struct esk_mach *m, int src, int level);
void esk_esp32_source_cpu(struct esk_mach *m, int cpu, int src, int level);

extern const struct esk_dev_ops esk_esp32_efuse_ops;
void *esk_esp32_efuse_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32_dport_ops;
void *esk_esp32_dport_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32_rtc_ops;
void *esk_esp32_rtc_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32_uart_ops;
void *esk_esp32_uart_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32_timg_ops;
void *esk_esp32_timg_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32_gpio_ops;
void *esk_esp32_gpio_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32_iomux_ops;
void *esk_esp32_iomux_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32_spi_ops;
void *esk_esp32_spi_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32_apbctrl_ops;
void *esk_esp32_apbctrl_create(struct esk_mach *s, struct esk_rf_periph *p);
extern const struct esk_dev_ops esk_esp32_sha_ops;
void *esk_esp32_sha_create(struct esk_mach *s, struct esk_rf_periph *p);
/* the RNG and the AHB view of the peripherals (misc.c): 0, or -1 */
int esk_esp32_misc_add(struct esk_mach *s);
/* the internal I2C master to the analog blocks (regi2c.c): 0, or -1 */
int esk_esp32_regi2c_add(struct esk_mach *s);

/* the chip's eFuse fields the hardware reads */
int esk_esp32_efuse_flash_crypt(struct esk_mach *m);
u32 esk_esp32_efuse_word(struct esk_mach *m, int block, int word);

/* the clocks: the CPU's (CPU_CLK), the APB's, the crystal's, in Hz */
u32 esk_esp32_cpu_hz(struct esk_mach *m);
u32 esk_esp32_apb_hz(struct esk_mach *m);
#define ESP32_XTAL_HZ 40000000u
/* RTC_CNTL CLK_CONF's SOC_CLK_SEL, and RTC_SLOW_CLK in Hz */
u32 esk_esp32_rtc_soc_clk_sel(struct esk_mach *m);
double esk_esp32_rtc_slow_hz(struct esk_mach *m);
/* DPORT CPU_PER_CONF's CPUPERIOD_SEL */
u32 esk_esp32_dport_cpuperiod(struct esk_mach *m);

/* a reset of TRM table 4.3-? `code` (1 POWERON, 3 SW_SYS, 12 SW_CPU, ...),
 * of the whole chip (system) or one CPU */
void esk_esp32_reset(struct esk_mach *m, int code);
void esk_esp32_cpu_reset(struct esk_mach *m, int core, int code);
/* the reset cause each CPU reports (RTC_CNTL RESET_STATE) */
void esk_esp32_rtc_reset_cause(struct esk_mach *m, int core, int code);

/* the flash's bytes from `addr` on, `len` of them, changed: the caches'
 * pages over them are stale */
void esk_esp32_flash_written(struct esk_mach *m, u32 addr, u32 len);

/* the strapping pins' levels latched at reset (GPIO_STRAP) */
u32 esk_esp32_strap(struct esk_mach *m);
/* where a UART's TXD (tx) or RXD is: 0 and the runtime's port and bit */
int esk_esp32_uart_pin(struct esk_mach *m, const char *unit, int tx, int *port, int *bit);
/* the board drove a pin or let it go: the levels again */
void esk_esp32_pins_changed(struct esk_mach *m);
/* a peripheral output signal of the GPIO matrix (TRM table 4-2) driven by
 * its model: its level and enable */
void esk_esp32_signal(struct esk_mach *m, int sig, int level, int oe);
/* a peripheral input signal's level as the matrix or the IO MUX gives it */
int esk_esp32_signal_in(struct esk_mach *m, int sig);

/* the flash chip (spi.c) */
struct esk_esp32_nor;
struct esk_esp32_nor *esk_esp32_nor_of(struct esk_mach *m);
struct esk_esp32_nor *esk_esp32_nor_create(struct esk_mach *m);
void esk_esp32_nor_power_on(struct esk_esp32_nor *f);
int esk_esp32_nor_busy(struct esk_esp32_nor *f);
/* one transaction: `ntx` bytes the chip receives (the instruction first),
 * then `nrx` bytes it answers with */
void esk_esp32_nor_xfer(struct esk_esp32_nor *f, const u8 *tx, int ntx, u8 *rx, int nrx);
#endif
