/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* models.c -- the peripheral models an MCU description can bind by name
 * (SPEC-009 §6): a const table, so no registry state exists (CS-18). */
#include <string.h>

#include "mcu/avr/avr.h"
#include "mcu/rf.h"
#include "mcu/riscv/riscv.h"
#include "mcu/stm32/stm32.h"

static const struct esk_rf_model_type model_types[] = {
    { "stm32-rcc", &esk_stm32_rcc_ops, esk_stm32_rcc_create },
    { "stm32f1-gpio", &esk_stm32f1_gpio_ops, esk_stm32f1_gpio_create },
    { "stm32f1-afio", &esk_stm32f1_afio_ops, esk_stm32f1_afio_create },
    { "stm32-exti", &esk_stm32_exti_ops, esk_stm32_exti_create },
    { "stm32-usart", &esk_stm32_usart_ops, esk_stm32_usart_create },
    { "stm32-tim", &esk_stm32_tim_ops, esk_stm32_tim_create },
    { "stm32f1-flash", &esk_stm32_storage_ops, esk_stm32_storage_create },
    { "stm32-pwr", &esk_stm32_storage_ops, esk_stm32_storage_create },
    { "avr-port", &esk_avr_port_ops, esk_avr_port_create },
    { "avr-exint", &esk_avr_exint_ops, esk_avr_exint_create },
    { "avr-tc8", &esk_avr_tc8_ops, esk_avr_tc8_create },
    { "avr-tc16", &esk_avr_tc16_ops, esk_avr_tc16_create },
    { "avr-usart", &esk_avr_usart_ops, esk_avr_usart_create },
    { "avr-adc", &esk_avr_adc_ops, esk_avr_adc_create },
    { "avr-cpu", &esk_avr_cpu_ops, esk_avr_cpu_create },
    { "avr-eeprom", &esk_avr_eeprom_ops, esk_avr_eeprom_create },
    { "fe310-clint", &esk_fe310_clint_ops, esk_fe310_clint_create },
    { "fe310-plic", &esk_fe310_plic_ops, esk_fe310_plic_create },
    { "fe310-gpio", &esk_fe310_gpio_ops, esk_fe310_gpio_create },
    { "fe310-uart", &esk_fe310_uart_ops, esk_fe310_uart_create },
    { "fe310-prci", &esk_fe310_prci_ops, esk_fe310_prci_create },
};

const struct esk_rf_model_type *esk_rf_model_find(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof model_types / sizeof model_types[0]; i++)
        if (!strcmp(model_types[i].name, name))
            return &model_types[i];
    return NULL;
}
