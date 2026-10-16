/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* semihost.h -- ARM semihosting for the test firmware: SYS_WRITE0 and
 * SYS_EXIT_EXTENDED through `bkpt 0xab`. */
#ifndef TEST_FW_SEMIHOST_H
#define TEST_FW_SEMIHOST_H
#include <stdint.h>

static inline uint32_t semihost_call(uint32_t op, const void *arg)
{
    register uint32_t r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

static inline void sh_puts(const char *s) { semihost_call(0x04, s); }

static inline void sh_exit(uint32_t status)
{
    uint32_t block[2] = { 0x20026u, status };
    semihost_call(0x20, block);
}
#endif
