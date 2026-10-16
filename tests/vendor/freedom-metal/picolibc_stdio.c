/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* picolibc_stdio.c -- picolibc's stdin, stdout and stderr on freedom-metal's
 * tty (the board's stdout-path UART), as freedom-metal's gloss gives newlib
 * them through write and read. The one file of the build that is not
 * SiFive's: Ubuntu's picolibc has no POSIX console. */
#include <stdio.h>

#include <metal/tty.h>

static int put(char c, FILE *f)
{
    (void)f;
    return metal_tty_putc((unsigned char)c) ? EOF : (unsigned char)c;
}

static int get(FILE *f)
{
    int c = -1;
    (void)f;
    while (metal_tty_getc(&c) || c < 0)
        ;
    return c;
}

static FILE io = FDEV_SETUP_STREAM(put, get, NULL, _FDEV_SETUP_RW);
FILE *const stdin = &io;
FILE *const stdout = &io;
FILE *const stderr = &io;
