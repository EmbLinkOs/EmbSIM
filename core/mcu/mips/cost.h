/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* cost.h -- the cycles the MIPS32 core charges per instruction (timing
 * tier "estimated", ADR-015): 1, a load 2 (the M4K's load-use latency
 * from its SRAM interface), mult and multu 2, madd, msub and mul 2, div
 * and divu 18 (the M4K's iterative divider takes up to 35; 18 is EmbCC's
 * estimate across operands). The branch delay slot hides the M4K's branch
 * penalty, so a branch costs 1. Wait states on flash are not modelled:
 * the PIC32MX's prefetch cache hides most of them once it is on.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/bench/cost.h (mips_cost). */
#ifndef ESK_MIPS_COST_H
#define ESK_MIPS_COST_H
#include "embsim/compiler.h"

ES_INLINE int esk_mips_cost(unsigned w)
{
    unsigned op = w >> 26, fn = w & 0x3f;
    if ((op >= 0x20 && op <= 0x26) || op == 0x30) /* loads, ll */
        return 2;
    if (op == 0 && (fn == 0x18 || fn == 0x19)) /* mult, multu */
        return 2;
    if (op == 0 && (fn == 0x1a || fn == 0x1b)) /* div, divu */
        return 18;
    if (op == 0x1c && (fn <= 0x02 || fn == 0x04 || fn == 0x05))
        return 2; /* madd mul msub */
    return 1;
}

#endif
