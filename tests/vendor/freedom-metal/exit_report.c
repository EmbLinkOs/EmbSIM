/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* exit_report.c -- an example's exit status on freedom-metal's tty
 * ("exit 0"), then freedom-metal's own metal_shutdown: the HiFive1 has no
 * shutdown device, so that spins, and examples that report only by their
 * status (example-pmp's main returns the protected word) would say
 * nothing a scenario can see. run.py links it with
 * -Wl,--wrap=metal_shutdown, which sends freedom-metal's _exit here. */
#include <metal/tty.h>

void __real_metal_shutdown(int code);

void __wrap_metal_shutdown(int code)
{
    char d[12];
    int n = 0;
    unsigned u = code < 0 ? 0u - (unsigned)code : (unsigned)code;
    const char *p = "exit ";
    while (*p)
        metal_tty_putc(*p++);
    if (code < 0)
        metal_tty_putc('-');
    do
        d[n++] = (char)('0' + u % 10);
    while (u /= 10);
    while (n)
        metal_tty_putc(d[--n]);
    metal_tty_putc('\n');
    __real_metal_shutdown(code);
}
