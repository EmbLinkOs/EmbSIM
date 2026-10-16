/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* stack-overflow -- the stack analysis's test: a recursion 40 calls deep,
 * each call 64 bytes or more of stack, on a stack the link limits to 2 KiB
 * (stack.ld's __stack_limit), so it goes below the limit once; then a loop
 * of shallow calls, which stays within it. */
#include <stdint.h>

static volatile uint32_t sink;

__attribute__((noinline)) static uint32_t deep(uint32_t n)
{
    volatile uint32_t buf[12];
    uint32_t i;
    for (i = 0; i < 12; i++)
        buf[i] = n + i;
    return n ? deep(n - 1) + buf[n % 12] : buf[0];
}

__attribute__((noinline)) static uint32_t leaf(uint32_t x) { return x * 3u + 1u; }

int main(void)
{
    sink = deep(40);
    for (;;)
        sink = leaf(sink);
    return 0;
}
