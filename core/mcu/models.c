/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* models.c -- the peripheral models an MCU description can bind by name
 * (SPEC-009 §6): a const table, so no registry state exists (CS-18). */
#include <string.h>

#include "mcu/avr/avr.h"
#include "mcu/nrf52/nrf52.h"
#include "mcu/pic24/pic24.h"
#include "mcu/pic32/pic32.h"
#include "mcu/rf.h"
#include "mcu/riscv/riscv.h"
#include "mcu/rp2040/rp2040.h"
#include "mcu/stm32/stm32.h"

static const struct esk_rf_model_type model_types[] = {
    { "stm32-rcc", &esk_stm32_rcc_ops, esk_stm32_rcc_create },
    { "stm32f1-gpio", &esk_stm32f1_gpio_ops, esk_stm32f1_gpio_create },
    { "stm32f1-afio", &esk_stm32f1_afio_ops, esk_stm32f1_afio_create },
    { "stm32-exti", &esk_stm32_exti_ops, esk_stm32_exti_create },
    { "stm32-usart", &esk_stm32_usart_ops, esk_stm32_usart_create },
    { "stm32-tim", &esk_stm32_tim_ops, esk_stm32_tim_create },
    { "stm32f1-adc", &esk_stm32f1_adc_ops, esk_stm32f1_adc_create },
    { "stm32f1-spi", &esk_stm32f1_spi_ops, esk_stm32f1_spi_create },
    { "stm32f1-i2c", &esk_stm32f1_i2c_ops, esk_stm32f1_i2c_create },
    { "stm32f1-flash", &esk_stm32_storage_ops, esk_stm32_storage_create },
    { "stm32-pwr", &esk_stm32_storage_ops, esk_stm32_storage_create },
    { "storage", &esk_stm32_storage_ops, esk_stm32_storage_create },
    { "avr-port", &esk_avr_port_ops, esk_avr_port_create },
    { "avr-exint", &esk_avr_exint_ops, esk_avr_exint_create },
    { "avr-tc8", &esk_avr_tc8_ops, esk_avr_tc8_create },
    { "avr-tc16", &esk_avr_tc16_ops, esk_avr_tc16_create },
    { "avr-usart", &esk_avr_usart_ops, esk_avr_usart_create },
    { "avr-adc", &esk_avr_adc_ops, esk_avr_adc_create },
    { "avr-cpu", &esk_avr_cpu_ops, esk_avr_cpu_create },
    { "avr-eeprom", &esk_avr_eeprom_ops, esk_avr_eeprom_create },
    { "avr-spi", &esk_avr_spi_ops, esk_avr_spi_create },
    { "avr-twi", &esk_avr_twi_ops, esk_avr_twi_create },
    { "fe310-clint", &esk_fe310_clint_ops, esk_fe310_clint_create },
    { "fe310-plic", &esk_fe310_plic_ops, esk_fe310_plic_create },
    { "fe310-gpio", &esk_fe310_gpio_ops, esk_fe310_gpio_create },
    { "fe310-uart", &esk_fe310_uart_ops, esk_fe310_uart_create },
    { "fe310-prci", &esk_fe310_prci_ops, esk_fe310_prci_create },
    { "fe310-aon", &esk_fe310_aon_ops, esk_fe310_aon_create },
    { "nrf-clock", &esk_nrf_clock_ops, esk_nrf_clock_create },
    { "nrf-gpio", &esk_nrf_gpio_ops, esk_nrf_gpio_create },
    { "nrf-gpiote", &esk_nrf_gpiote_ops, esk_nrf_gpiote_create },
    { "nrf-uart", &esk_nrf_uart_ops, esk_nrf_uart_create },
    { "nrf-timer", &esk_nrf_timer_ops, esk_nrf_timer_create },
    { "nrf-rtc", &esk_nrf_rtc_ops, esk_nrf_rtc_create },
    { "nrf-spim", &esk_nrf_spim_ops, esk_nrf_spim_create },
    { "nrf-pwm", &esk_nrf_pwm_ops, esk_nrf_pwm_create },
    { "nrf-saadc", &esk_nrf_saadc_ops, esk_nrf_saadc_create },
    { "rp2040-resets", &esk_rp2040_resets_ops, esk_rp2040_resets_create },
    { "rp2040-clocks", &esk_rp2040_clocks_ops, esk_rp2040_clocks_create },
    { "rp2040-xosc", &esk_rp2040_xosc_ops, esk_rp2040_xosc_create },
    { "rp2040-rosc", &esk_rp2040_rosc_ops, esk_rp2040_rosc_create },
    { "rp2040-pll", &esk_rp2040_pll_ops, esk_rp2040_pll_create },
    { "rp2040-psm", &esk_rp2040_psm_ops, esk_rp2040_psm_create },
    { "rp2040-watchdog", &esk_rp2040_watchdog_ops, esk_rp2040_watchdog_create },
    { "rp2040-timer", &esk_rp2040_timer_ops, esk_rp2040_timer_create },
    { "rp2040-pads", &esk_rp2040_pads_ops, esk_rp2040_pads_create },
    { "rp2040-io", &esk_rp2040_io_ops, esk_rp2040_io_create },
    { "rp2040-sio", &esk_rp2040_sio_ops, esk_rp2040_sio_create },
    { "rp2040-uart", &esk_rp2040_uart_ops, esk_rp2040_uart_create },
    { "rp2040-ssi", &esk_rp2040_ssi_ops, esk_rp2040_ssi_create },
    { "rp2040-pwm", &esk_rp2040_pwm_ops, esk_rp2040_pwm_create },
    { "rp2040-adc", &esk_rp2040_adc_ops, esk_rp2040_adc_create },
    { "rp2040-spi", &esk_rp2040_spi_ops, esk_rp2040_spi_create },
    { "rp2040-i2c", &esk_rp2040_i2c_ops, esk_rp2040_i2c_create },
    { "pic32-evic", &esk_pic32_evic_ops, esk_pic32_evic_create },
    { "pic32-osc", &esk_pic32_osc_ops, esk_pic32_osc_create },
    { "pic32-cfg", &esk_pic32_cfg_ops, esk_pic32_cfg_create },
    { "pic32-tmr", &esk_pic32_tmr_ops, esk_pic32_tmr_create },
    { "pic32-gpio", &esk_pic32_gpio_ops, esk_pic32_gpio_create },
    { "pic32-uart", &esk_pic32_uart_ops, esk_pic32_uart_create },
    { "pic24-intc", &esk_pic24_intc_ops, esk_pic24_intc_create },
    { "pic24-osc", &esk_pic24_osc_ops, esk_pic24_osc_create },
    { "pic24-pps", &esk_pic24_pps_ops, esk_pic24_pps_create },
    { "pic24-tmr", &esk_pic24_tmr_ops, esk_pic24_tmr_create },
    { "pic24-gpio", &esk_pic24_gpio_ops, esk_pic24_gpio_create },
    { "pic24-uart", &esk_pic24_uart_ops, esk_pic24_uart_create },
};

const struct esk_rf_model_type *esk_rf_model_find(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof model_types / sizeof model_types[0]; i++)
        if (!strcmp(model_types[i].name, name))
            return &model_types[i];
    return NULL;
}
