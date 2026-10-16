/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- the Uno example: the 'L' LED on D13 blinks at 1 Hz from a
 * 1 ms tick (Timer0 in CTC mode), the potentiometer on A0 is read every
 * 250 ms and printed on the serial line at 9600 baud and sets the
 * brightness of the LED on D9 (Timer1's 8-bit fast PWM on OC1A), and the
 * button on D2 (INT0, falling edge, internal pull-up) is counted and
 * printed. Between events the core sleeps in idle mode. */
#include "atmega328p.h"

static volatile uint32_t ticks;   /* milliseconds since reset */
static volatile uint8_t presses;  /* INT0's count */

ISR(14) /* TIMER0_COMPA */
{
    ticks++;
}

ISR(1) /* INT0 */
{
    presses++;
}

static void put(char c)
{
    while (!(UCSR0A & 0x20)) /* UDRE0 */
        ;
    UDR0 = (uint8_t)c;
}

static void puts_(const char *s)
{
    while (*s)
        put(*s++);
}

/* decimal by subtraction: the part has no divide instruction and this
 * example links no runtime library (the remainder is volatile, or the
 * compiler turns the loop back into a call to __udivmodhi4) */
static void put_num(unsigned v)
{
    static const unsigned tens[5] = { 10000, 1000, 100, 10, 1 };
    volatile unsigned rem = v;
    int i, started = 0;
    for (i = 0; i < 5; i++) {
        char d = '0';
        while (rem >= tens[i]) {
            rem -= tens[i];
            d++;
        }
        if (d != '0' || started || i == 4) {
            put(d);
            started = 1;
        }
    }
}

static uint32_t now(void)
{
    uint32_t t;
    cli();
    t = ticks;
    sei();
    return t;
}

static unsigned adc_read(uint8_t ch)
{
    unsigned lo;
    ADMUX = (uint8_t)(0x40 | ch); /* AVCC reference */
    ADCSRA |= 0x40;               /* ADSC */
    while (ADCSRA & 0x40)
        ;
    lo = ADCL;
    return lo | (unsigned)ADCH << 8;
}

int main(void)
{
    uint32_t next_blink = 500, next_read = 0;
    uint8_t seen = 0;
    DDRB = 0x22;            /* D13 (PB5) and D9 (PB1) outputs */
    PORTD = 0x04;           /* D2's pull-up */
    /* USART0: 9600 baud at 16 MHz (UBRR 103), 8N1 */
    UBRR0H = 0;
    UBRR0L = 103;
    UCSR0C = 0x06;
    UCSR0B = 0x18;          /* RXEN0, TXEN0 */
    /* Timer0: CTC, /64, OCR0A 249: 1 ms */
    TCCR0A = 0x02;
    OCR0A = 249;
    TIMSK0 = 0x02;
    TCCR0B = 0x03;
    /* Timer1: fast PWM 8-bit, non-inverting on OC1A, /64 (976.6 Hz) */
    TCCR1A = 0x81;
    TCCR1B = 0x0B;
    OCR1AH = 0;
    OCR1AL = 0;
    /* ADC on, /128 (125 kHz) */
    ADCSRA = 0x87;
    /* INT0 on the falling edge */
    EICRA = 0x02;
    EIFR = 0x01;
    EIMSK = 0x01;
    SMCR = 0x01;            /* idle sleep */
    sei();
    puts_("EmbSIM Uno\r\n");
    for (;;) {
        uint32_t t = now();
        if (t >= next_blink) {
            next_blink += 500;
            PINB = 0x20;    /* toggle D13 */
        }
        if (t >= next_read) {
            unsigned v = adc_read(0);
            next_read += 250;
            OCR1AH = 0;
            OCR1AL = (uint8_t)(v >> 2);
            puts_("A0=");
            put_num(v);
            puts_("\r\n");
        }
        if (presses != seen) {
            seen = presses;
            puts_("button ");
            put_num(seen);
            puts_("\r\n");
        }
        sleep_cpu();
    }
}
