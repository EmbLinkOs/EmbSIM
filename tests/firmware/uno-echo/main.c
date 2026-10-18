/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- the host serial port's test firmware (tests/protocol/test_host_serial.py): at
 * 9600 baud it prints "boot" and why the part reset (MCUSR: 1 power-on,
 * 2 external, the Uno's DTR auto-reset among them), clears MCUSR, then
 * echoes every byte it receives in upper case. Bare registers, as the
 * uno-blink-serial example. */
#include "atmega328p.h"

#define MCUSR REG8(0x54)

static void put(char c)
{
    while (!(UCSR0A & 0x20)) /* UDRE0 */
        ;
    UDR0 = (uint8_t)c;
}

static void put_hex(uint8_t v)
{
    static const char hex[] = "0123456789abcdef";
    put(hex[v >> 4]);
    put(hex[v & 15]);
}

int main(void)
{
    uint8_t why = MCUSR;
    const char *s = "boot ";
    MCUSR = 0;
    UBRR0H = 0;
    UBRR0L = 103;  /* 9600 baud at 16 MHz */
    UCSR0C = 0x06; /* 8N1 */
    UCSR0B = 0x18; /* RXEN0, TXEN0 */
    while (*s)
        put(*s++);
    put_hex(why);
    put('\r');
    put('\n');
    for (;;) {
        char c;
        while (!(UCSR0A & 0x80)) /* RXC0 */
            ;
        c = (char)UDR0;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        put(c);
    }
}
