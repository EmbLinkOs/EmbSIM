/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* stm32.h -- the STM32 peripheral models (SPEC-009 §6), over the register
 * file (mcu/rf.h), and what they tell each other and the MCU runtime.
 *
 *   rcc.c      reset and clock control: ready bits, clock gates, HCLK, PCLKs
 *   gpio_f1.c  an F1 GPIO port (CRL/CRH): pins to the runtime, IDR from it
 *   afio_f1.c  the F1's alternate-function I/O (remaps, EXTI line sources)
 *   exti.c     external interrupt and event lines from the pins
 *   usart.c    a USART of the SR/DR layout, at its baud rate
 *   tim.c      a timer: time base, capture/compare, PWM, dead time, slave modes
 *   adc_f1.c   the F1's ADC: its regular group from the pins' voltages
 *   spi.c      an SPI of the SR/DR layout, master or slave, on its pins
 *   i2c.c      an I2C of the SR1/SR2 layout, master and slave, on its pins
 *   pwr_f4.c   the F4's power control: voltage scaling and over-drive ready
 *   gpio_f4.c  an F4 GPIO port (MODER, AFR, LCKR): pins to the runtime
 *   syscfg_f4.c the F4's system configuration: EXTI line ports, boot remap
 *   dma_f4.c   the F4's DMA controllers: streams, channels, FIFO, double buffer
 *   storage.c  registers that only keep what is written (FLASH's ACR) */
#ifndef ESK_MCU_STM32_H
#define ESK_MCU_STM32_H

#include "mcu/rf.h"

extern const struct esk_dev_ops esk_stm32_rcc_ops, esk_stm32f1_gpio_ops, esk_stm32_usart_ops;
extern const struct esk_dev_ops esk_stm32_tim_ops, esk_stm32f1_afio_ops, esk_stm32_exti_ops;
extern const struct esk_dev_ops esk_stm32_storage_ops, esk_stm32f1_adc_ops;
extern const struct esk_dev_ops esk_stm32_spi_ops, esk_stm32_i2c_ops;
void *esk_stm32f1_adc_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_stm32_spi_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_stm32_i2c_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_stm32_rcc_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_stm32f1_gpio_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_stm32_usart_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_stm32_tim_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_stm32f1_afio_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_stm32_exti_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_stm32_storage_create(struct esk_mach *m, struct esk_rf_periph *p);

/* the F1's clocks in Hz: HCLK (0 without the F1's RCC) and PCLK1 or PCLK2 */
u32 esk_stm32f1_hclk(struct esk_mach *m);
u32 esk_stm32f1_pclk(struct esk_mach *m, int apb);
/* any family's: HCLK (0 without a modelled RCC), PCLK1 or PCLK2, and the
 * clock of the timers on that APB */
u32 esk_stm32_hclk(struct esk_mach *m);
u32 esk_stm32_pclk(struct esk_mach *m, int apb);
u32 esk_stm32_timclk(struct esk_mach *m, int apb);
/* the F4's PWR: VOSRDY, ODRDY and ODSWRDY as the RCC's PLL and PWR's
 * enables have them (pwr_f4.c); nothing without the F4's PWR */
void esk_stm32f4_pwr_settle(struct esk_mach *m);
/* the F4's GPIO ports and SYSCFG (gpio_f4.c, syscfg_f4.c) */
extern const struct esk_dev_ops esk_stm32f4_gpio_ops, esk_stm32f4_syscfg_ops;
void *esk_stm32f4_gpio_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_stm32f4_syscfg_create(struct esk_mach *m, struct esk_rf_periph *p);
/* the pins' outside levels changed: every port's IDR again, and EXTI's
 * edges; one port's pins again (a peripheral's output on them changed) */
void esk_stm32f4_pins_changed(struct esk_mach *m);
void esk_stm32f4_port_changed(struct esk_mach *m, int port);
/* the port (0 for A) SYSCFG's EXTICR gives EXTI line 0-15, or -1 */
int esk_stm32f4_exti_port(struct esk_mach *m, int line);
extern const struct esk_dev_ops esk_stm32f4_pwr_ops;
void *esk_stm32f4_pwr_create(struct esk_mach *m, struct esk_rf_periph *p);
/* AFIO's MAPR (0 without AFIO) and the port an EXTI line listens to */
u32 esk_stm32f1_mapr(struct esk_mach *m);
int esk_stm32f1_exti_port(struct esk_mach *m, int line);
/* the pins' outside levels changed: every port's IDR again, and the EXTI
 * lines' edges */
void esk_stm32f1_pins_changed(struct esk_mach *m);
/* a USART's TX or RX pin as AFIO's MAPR places it: 0 and the port and bit,
 * or -1 */
int esk_stm32f1_usart_pin(struct esk_mach *m, const char *unit, int tx, int *port, int *bit);
/* a timer's alternate-function signal (TIM1_CH1, TIM1_CH1N): the level it
 * drives, or -1; a level a pin gives TIMx_CHy, TIMx_ETR or TIMx_BKIN */
int esk_stm32_tim_signal(struct esk_mach *m, const char *sig);
void esk_stm32_tim_signal_input(struct esk_mach *m, const char *sig, int level);
/* the F4's DMA controllers (dma_f4.c) */
extern const struct esk_dev_ops esk_stm32f4_dma_ops;
void *esk_stm32f4_dma_create(struct esk_mach *m, struct esk_rf_periph *p);
/* a peripheral's DMA request (USART2_TX, TIM1_UP, ADC1) rises (level 1)
 * or falls: a stream that takes it moves the data now, else it waits for
 * one; nothing without a DMA model. `sig` must outlive the request. */
void esk_stm32_dma_request(struct esk_mach *m, const char *sig, int level);
/* a USART's alternate-function signal (USART2_TX, _RTS, _CK): the level it
 * drives, or -1; nCTS's level from its pin; the pin (port, bit) of its TX
 * (tx 1) or RX, as the family maps it (the runtime's uart_pin) */
int esk_stm32_usart_signal(struct esk_mach *m, const char *sig);
void esk_stm32_usart_signal_input(struct esk_mach *m, const char *sig, int level);
int esk_stm32_usart_pin(struct esk_mach *m, const char *unit, int tx, int *port, int *bit);
/* an SPI's alternate-function signal (SPI1_SCK, _MOSI, _MISO, _NSS): the
 * level it drives, or -1; a level its pin gives it (a slave's SCK, MOSI,
 * NSS; a master's NSS); a DMA stream's last item for its request (SPI1_TX,
 * SPI1_RX: the CRC follows) */
int esk_stm32_spi_signal(struct esk_mach *m, const char *sig);
void esk_stm32_spi_signal_input(struct esk_mach *m, const char *sig, int level);
void esk_stm32_spi_dma_end(struct esk_mach *m, const char *sig);
/* an I2C's alternate-function signal (I2C1_SCL, _SDA, _SMBA): the level it
 * drives (1 let go), or -1; a level its pin gives it; a DMA stream's items
 * left after one for its request (I2C1_RX's EOT-1, I2C1_TX's end) */
int esk_stm32_i2c_signal(struct esk_mach *m, const char *sig);
void esk_stm32_i2c_signal_input(struct esk_mach *m, const char *sig, int level);
void esk_stm32_i2c_dma_left(struct esk_mach *m, const char *sig, u32 left);
void esk_stm32f1_i2c_input(struct esk_mach *m, int port, int bit, int level);
/* the F1: the level a pin (port, bit) gives the SPI functions AFIO places
 * there */
void esk_stm32f1_spi_input(struct esk_mach *m, int port, int bit, int level);
/* the F4: the pin (port * 32 + bit) whose GPIO selects `sig` now, or -1 */
int esk_stm32f4_af_pin(struct esk_mach *m, const char *sig);
/* the F1: a USART's CK, CTS or RTS pin ("CK", "CTS", "RTS") as AFIO's MAPR
 * places it: port * 16 + bit, or -1 */
int esk_stm32f1_usart_fn_pin(struct esk_mach *m, const char *unit, const char *fn);
/* the F1: the level a pin (port, bit) gives the timer channels AFIO's MAPR
 * places there */
void esk_stm32f1_tim_input(struct esk_mach *m, int port, int bit, int level);
/* the level a timer's compare channel, an SPI or an I2C master
 * drives on a pin (port, bit) that AFIO's MAPR gives it, or -1 */
int esk_stm32f1_tim_pin(struct esk_mach *m, int port, int bit);
int esk_stm32f1_spi_pin(struct esk_mach *m, int port, int bit);
int esk_stm32f1_i2c_pin(struct esk_mach *m, int port, int bit);
/* one port's pins again (a peripheral's output on them changed) */
void esk_stm32f1_port_changed(struct esk_mach *m, int port);
/* an input line's level, as GPIO's IDR has it: EXTI's edge detector */
void esk_stm32_exti_input(struct esk_mach *m, int line, int level);

#endif
