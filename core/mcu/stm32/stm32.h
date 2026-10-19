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
 *   tim.c      a timer's time base and its output compare and PWM
 *   adc_f1.c   the F1's ADC: its regular group from the pins' voltages
 *   spi_f1.c   the F1's SPI as a master, on its pins
 *   i2c_f1.c   the F1's I2C as a master, on its pins
 *   pwr_f4.c   the F4's power control: voltage scaling and over-drive ready
 *   storage.c  registers that only keep what is written (FLASH's ACR) */
#ifndef ESK_MCU_STM32_H
#define ESK_MCU_STM32_H

#include "mcu/rf.h"

extern const struct esk_dev_ops esk_stm32_rcc_ops, esk_stm32f1_gpio_ops, esk_stm32_usart_ops;
extern const struct esk_dev_ops esk_stm32_tim_ops, esk_stm32f1_afio_ops, esk_stm32_exti_ops;
extern const struct esk_dev_ops esk_stm32_storage_ops, esk_stm32f1_adc_ops;
extern const struct esk_dev_ops esk_stm32f1_spi_ops, esk_stm32f1_i2c_ops;
void *esk_stm32f1_adc_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_stm32f1_spi_create(struct esk_mach *m, struct esk_rf_periph *p);
void *esk_stm32f1_i2c_create(struct esk_mach *m, struct esk_rf_periph *p);
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
/* the level a timer's compare channel, an SPI master or an I2C master
 * drives on a pin (port, bit) that AFIO's MAPR gives it, or -1 */
int esk_stm32f1_tim_pin(struct esk_mach *m, int port, int bit);
int esk_stm32f1_spi_pin(struct esk_mach *m, int port, int bit);
int esk_stm32f1_i2c_pin(struct esk_mach *m, int port, int bit);
/* one port's pins again (a peripheral's output on them changed) */
void esk_stm32f1_port_changed(struct esk_mach *m, int port);
/* an input line's level, as GPIO's IDR has it: EXTI's edge detector */
void esk_stm32_exti_input(struct esk_mach *m, int line, int level);

#endif
