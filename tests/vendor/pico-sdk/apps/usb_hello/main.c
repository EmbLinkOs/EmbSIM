/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* usb_hello -- the SDK's stdio over USB, as its hello_usb example: once a
 * terminal opens the Pico's serial port (DTR), a greeting, then every 100 ms
 * a count; what the terminal sends comes back in upper case. */
#include <ctype.h>
#include <stdio.h>

#include "pico/stdlib.h"

int main(void)
{
    int n = 0;
    stdio_init_all();
    while (!stdio_usb_connected())
        sleep_ms(10);
    printf("hello over USB\n");
    for (;;) {
        int c = getchar_timeout_us(100000);
        if (c >= 0)
            putchar(toupper(c));
        else
            printf("tick %d\n", n++);
    }
}
