/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* atmega328p.h -- the registers this example uses, at their data-space
 * addresses (ATmega328P datasheet, section 36), and the interrupt
 * handler's spelling. */
#ifndef ATMEGA328P_H
#define ATMEGA328P_H

#include <stdint.h>

#define REG8(a) (*(volatile uint8_t *)(a))

#define PINB REG8(0x23)
#define DDRB REG8(0x24)
#define PORTB REG8(0x25)
#define PIND REG8(0x29)
#define DDRD REG8(0x2A)
#define PORTD REG8(0x2B)
#define EIFR REG8(0x3C)
#define EIMSK REG8(0x3D)
#define TCCR0A REG8(0x44)
#define TCCR0B REG8(0x45)
#define OCR0A REG8(0x47)
#define SMCR REG8(0x53)
#define TIMSK0 REG8(0x6E)
#define EICRA REG8(0x69)
#define ADCL REG8(0x78)
#define ADCH REG8(0x79)
#define ADCSRA REG8(0x7A)
#define ADMUX REG8(0x7C)
#define TCCR1A REG8(0x80)
#define TCCR1B REG8(0x81)
#define OCR1AL REG8(0x88)
#define OCR1AH REG8(0x89)
#define UCSR0A REG8(0xC0)
#define UCSR0B REG8(0xC1)
#define UCSR0C REG8(0xC2)
#define UBRR0L REG8(0xC4)
#define UBRR0H REG8(0xC5)
#define UDR0 REG8(0xC6)

/* vector n's handler: the startup's table jumps to __vector_n */
#define ISR(n) __attribute__((signal, used)) void __vector_##n(void)
#define VECT_INT0 1
#define VECT_TIMER0_COMPA 14
#define VECT_USART_RX 18

static inline void sei(void) { __asm__ volatile("sei" ::: "memory"); }
static inline void cli(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void sleep_cpu(void) { __asm__ volatile("sleep" ::: "memory"); }

#endif
