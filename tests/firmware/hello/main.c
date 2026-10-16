/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* hello -- the smallest firmware test: arithmetic the core must get right,
 * a line on the semihosting console, and an exit status. */
#include <stdint.h>
#include "semihost.h"

static uint32_t fib(uint32_t n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

int main(void)
{
    static char line[] = "hello from the Cortex-M3: fib(20) = .....\n";
    uint32_t v = fib(20), i;
    for (i = 0; i < 5; i++) {
        line[40 - i] = (char)('0' + v % 10);
        v /= 10;
    }
    sh_puts(line);
    sh_exit(fib(20) == 6765 ? 0 : 1);
    return 0;
}
