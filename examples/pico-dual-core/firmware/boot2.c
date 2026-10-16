/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* boot2.c -- the first 256 bytes of flash: the second-stage boot the
 * RP2040's bootrom checks (CRC-32 of the first 252 bytes in the last word),
 * copies to SRAM at 0x20041f00 and enters. A real boot2 sets the SSI up
 * for fast quad reads; this one leaves the XIP as the bootrom made it and
 * hands over to the program: the vector table after it into VTOR, its
 * stack pointer into MSP, and a jump to its reset handler.
 *
 *     ldr  r0, =0x10000100      @ the vector table
 *     ldr  r1, =0xe000ed08      @ VTOR
 *     str  r0, [r1]
 *     ldm  r0, {r0, r1}         @ the initial SP and the reset vector
 *     msr  msp, r0
 *     bx   r1
 *
 * Written as words, so that every toolchain makes the same bytes and the
 * CRC holds. */
#include <stdint.h>

__attribute__((section(".boot2"), used)) const uint32_t boot2[64] = {
    0x49044803u, /* ldr r0, [pc, #12]; ldr r1, [pc, #16] */
    0xc8036008u, /* str r0, [r1]; ldm r0, {r0, r1} */
    0x8808f380u, /* msr msp, r0 */
    0xbf004708u, /* bx r1; nop */
    0x10000100u, /* the literal pool */
    0xe000ed08u,
    [63] = 0xca04a070u, /* the CRC-32 of the 252 bytes before it */
};
