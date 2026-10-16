/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- the Uno's side of the two-board example: every 100 ms it sends
 * "ping N" on its serial line to the Blue Pill, whose firmware echoes what
 * it receives in upper case; when "PING N" comes back the Uno answers
 * "ok N" and counts the round trip, and from the third on it lights the L
 * LED. The line runs at 117647 baud (U2X, UBRR 16: the nearest the Uno's
 * 16 MHz gets to the Blue Pill's 115200, 2.1 % fast, as on the real
 * boards); bytes are received by the RX-complete interrupt. */
#include "atmega328p.h"

#define UCSR0A_U2X 0x02

static volatile uint32_t ticks;
static volatile char rx[64];
static volatile uint8_t rx_head, rx_tail;

ISR(14) /* TIMER0_COMPA: 1 ms */
{
    ticks++;
}

ISR(18) /* USART_RX */
{
    rx[rx_head & 63] = (char)UDR0;
    rx_head++;
}

static void put(char c)
{
    while (!(UCSR0A & 0x20))
        ;
    UDR0 = (uint8_t)c;
}

static void puts_(const char *s)
{
    while (*s)
        put(*s++);
}

static void put_digit(uint8_t n)
{
    put((char)('0' + n));
}

static uint32_t now(void)
{
    uint32_t t;
    cli();
    t = ticks;
    sei();
    return t;
}

int main(void)
{
    char line[16];
    uint8_t len = 0, n = 1, waiting = 0;
    uint32_t next_ping = 50;
    DDRB = 0x20; /* L */
    UCSR0A = UCSR0A_U2X;
    UBRR0H = 0;
    UBRR0L = 16;
    UCSR0C = 0x06;
    UCSR0B = 0x98; /* RXCIE0, RXEN0, TXEN0 */
    TCCR0A = 0x02;
    OCR0A = 249;
    TIMSK0 = 0x02;
    TCCR0B = 0x03;
    SMCR = 0x01;
    sei();
    for (;;) {
        if (!waiting && now() >= next_ping && n <= 9) {
            puts_("ping ");
            put_digit(n);
            puts_("\r\n");
            waiting = 1;
        }
        while (rx_tail != rx_head) {
            char c = rx[rx_tail & 63];
            rx_tail++;
            if (c == '\n') {
                if (len >= 6 && line[0] == 'P' && line[1] == 'I' && line[2] == 'N' && line[3] == 'G' &&
                    line[5] == (char)('0' + n)) {
                    puts_("ok ");
                    put_digit(n);
                    puts_("\r\n");
                    if (n >= 3)
                        PORTB |= 0x20;
                    n++;
                    waiting = 0;
                    next_ping = now() + 100;
                }
                len = 0;
            } else if (c != '\r' && len < sizeof line) {
                line[len++] = c;
            }
        }
        sleep_cpu();
    }
}
