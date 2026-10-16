/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* cost.h -- the cycles the RISC-V core charges per instruction (timing
 * tier "estimated", ADR-015): 1, a load 2, a division or remainder 16; and
 * a taken branch or jump 2 more (the pipeline refill), which the core adds
 * when execution does not continue at the next instruction. The FE310's
 * E31 core takes about these: its manual gives a 2-cycle load-use latency
 * on a hit and a variable-latency divider.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/bench/cost.h (rv_cost). */
#ifndef ESK_RISCV_COST_H
#define ESK_RISCV_COST_H
#include "embsim/compiler.h"

/* an instruction's cost (`size` 2 for a compressed one) and whether it
 * may branch */
ES_INLINE int esk_rv_cost(unsigned w, int size, int *branch)
{
    unsigned op = w & 0x7f;
    *branch = 0;
    if (size == 2) {
        unsigned h = w & 0xffff, q = h & 3, f3 = h >> 13;
        if (q == 0) /* c.lw */
            return f3 >= 1 && f3 <= 3 ? 2 : 1;
        if (q == 1) {
            if (f3 == 1 || f3 == 5 || f3 == 6 || f3 == 7)
                *branch = 1; /* c.jal c.j c.b*z */
            return 1;
        }
        if (f3 >= 1 && f3 <= 3) /* c.lwsp */
            return 2;
        if (f3 == 4 && ((h >> 2) & 0x1f) == 0 && ((h >> 7) & 0x1f))
            *branch = 1; /* c.jr, c.jalr */
        return 1;
    }
    if (op == 0x03 || op == 0x07) /* loads */
        return 2;
    if (op == 0x63 || op == 0x6f || op == 0x67) { /* b*, jal, jalr */
        *branch = 1;
        return 1;
    }
    if ((op == 0x33 || op == 0x3b) && (w >> 25) == 1 && ((w >> 12) & 7) >= 4)
        return 16; /* div, rem */
    return 1;
}

#endif
