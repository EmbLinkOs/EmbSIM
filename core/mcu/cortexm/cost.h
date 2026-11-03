/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* cost.h -- the cycles the Cortex-M core charges per instruction
 * (timing tier "estimated", ADR-015): the Cortex-M4 Technical Reference
 * Manual's table, simplified -- 1, a load 2, a load or store multiple
 * 1 + the registers, a load or store pair 3, sdiv/udiv 7, vdiv and vsqrt
 * 14; and a taken branch 2 more (the pipeline refill), which the caller
 * adds when execution does not continue at the next instruction.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/bench/cost.h (arm_cost), the
 * table EmbCC's QEMU plugin and simulator share. */
#ifndef ESK_CORTEXM_COST_H
#define ESK_CORTEXM_COST_H
#include <stddef.h>
#include <stdint.h>
#include "embsim/compiler.h"

ES_INLINE int esk_cm_popcount16(unsigned v)
{
    int n = 0;
    for (v &= 0xffff; v; v &= v - 1)
        n++;
    return n;
}

/* A Thumb instruction's cost and whether it may branch. */
ES_INLINE int esk_cm_cost(const uint8_t *p, size_t n, int *branch)
{
    unsigned h1 = (unsigned)p[0] | (unsigned)p[1] << 8;
    *branch = 0;
    if (n == 2) {
        if ((h1 & 0xff00) == 0x4700) {                  /* bx, blx */
            *branch = 1;
            return 1;
        }
        if ((h1 & 0xf800) == 0x4800)                    /* ldr literal */
            return 2;
        if ((h1 & 0xf000) == 0x5000)                    /* ld/st register */
            return ((h1 >> 9) & 7) >= 3 ? 2 : 1;
        if ((h1 & 0xe000) == 0x6000 || (h1 & 0xf000) == 0x8000 ||
            (h1 & 0xf000) == 0x9000)                    /* ld/st immediate */
            return h1 & 0x0800 ? 2 : 1;
        if ((h1 & 0xfe00) == 0xb400)                    /* push */
            return 1 + esk_cm_popcount16(h1 & 0x1ff);
        if ((h1 & 0xfe00) == 0xbc00) {                  /* pop */
            *branch = (h1 & 0x100) != 0;
            return 1 + esk_cm_popcount16(h1 & 0x1ff);
        }
        if ((h1 & 0xf000) == 0xc000)                    /* stm, ldm */
            return 1 + esk_cm_popcount16(h1 & 0xff);
        if ((h1 & 0xf500) == 0xb100) {                  /* cbz, cbnz */
            *branch = 1;
            return 1;
        }
        if ((h1 & 0xf000) == 0xd000 && (h1 & 0x0e00) != 0x0e00) {
            *branch = 1;                                /* b<cond> */
            return 1;
        }
        if ((h1 & 0xf800) == 0xe000) {                  /* b */
            *branch = 1;
            return 1;
        }
        return 1;
    }
    unsigned h2 = (unsigned)p[2] | (unsigned)p[3] << 8;
    if ((h1 & 0xf800) == 0xf000 && (h2 & 0x8000)) {
        /* b, bl and the miscellaneous control space; only the branches
         * have bit 12 or a condition outside 111x */
        if ((h2 & 0x5000) || (h1 & 0x0380) != 0x0380)
            *branch = 1;
        return 1;
    }
    if ((h1 & 0xfff0) == 0xe8d0 && (h2 & 0xffe0) == 0xf000) {
        *branch = 1;                                    /* tbb, tbh */
        return 2;
    }
    if ((h1 & 0xfe40) == 0xe800) {                      /* ldm, stm */
        if ((h1 & 0x0010) && (h2 & 0x8000))
            *branch = 1;                                /* pop.w {.., pc} */
        return 1 + esk_cm_popcount16(h2);
    }
    if ((h1 & 0xfff0) == 0xe850)                        /* ldrex */
        return 2;
    if ((h1 & 0xfe40) == 0xe840 && (h1 & 0x0120))       /* ldrd, strd */
        return 3;
    if ((h1 & 0xfe00) == 0xf800) {                      /* ld/st single */
        if ((h1 & 0x0010) && (h2 & 0xf000) == 0xf000 &&
            ((h1 >> 5) & 3) == 2)
            *branch = 1;                                /* ldr pc */
        return h1 & 0x0010 ? 2 : 1;
    }
    if ((h1 & 0xffd0) == 0xfb90)                        /* sdiv, udiv */
        return 7;
    if ((h1 & 0xfe10) == 0xec10 && (h1 & 0x0100))       /* vldr, vldm */
        return 2;
    if ((h1 & 0xffb0) == 0xee80 && (h2 & 0x0e50) == 0x0a00)
        return 14;                                      /* vdiv */
    if ((h1 & 0xffbf) == 0xeeb1 && (h2 & 0x0ed0) == 0x0ac0)
        return 14;                                      /* vsqrt */
    return 1;
}

/* A Cortex-M0+ (ARMv6-M) instruction's cycles, from the Cortex-M0+ TRM's
 * Table 3-1 (ARM DDI 0484C, zero wait states), timing tier "exact" for the
 * instructions it lists: `refill` gets the cycles a taken branch adds (1;
 * 2 for POP {.., PC}, whose 3+N the table gives) and `ls` whether it is a
 * single load or store, which takes 2 cycles on the AHB-Lite bus and 1 on
 * the single-cycle I/O port (the core subtracts the 1 when the access went
 * there). MULS is 1, for the fast multiplier the RP2040 configures (its
 * datasheet 2.4); SVC and BKPT are 1, their exception charged apart. */
ES_INLINE int esk_cm0p_cost(const uint8_t *p, size_t n, int *refill, int *ls)
{
    unsigned h1 = (unsigned)p[0] | (unsigned)p[1] << 8;
    *refill = 0;
    *ls = 0;
    if (n == 4) {
        unsigned h2 = (unsigned)p[2] | (unsigned)p[3] << 8;
        if ((h1 & 0xf800) == 0xf000 && (h2 & 0xd000) == 0xd000) {
            *refill = 1;                                /* bl: 3 */
            return 2;
        }
        return 3;                                       /* mrs, msr, isb, dsb, dmb */
    }
    if ((h1 & 0xff00) == 0x4700) {                      /* bx, blx: 2 */
        *refill = 1;
        return 1;
    }
    if ((h1 & 0xfc87) == 0x4487) {                      /* add pc, mov pc: 2 */
        *refill = 1;
        return 1;
    }
    if ((h1 & 0xf800) == 0x4800 || (h1 & 0xf000) == 0x5000 || (h1 & 0xe000) == 0x6000 ||
        (h1 & 0xf000) == 0x8000 || (h1 & 0xf000) == 0x9000) {
        *ls = 1;                                        /* ldr/str: 2 or 1 */
        return 2;
    }
    if ((h1 & 0xfe00) == 0xb400)                        /* push: 1+N */
        return 1 + esk_cm_popcount16(h1 & 0x1ff);
    if ((h1 & 0xfe00) == 0xbc00) {                      /* pop: 1+N, with pc 3+N */
        if (h1 & 0x100)
            *refill = 2;
        return 1 + esk_cm_popcount16(h1 & 0x1ff);
    }
    if ((h1 & 0xf000) == 0xc000)                        /* stm, ldm: 1+N */
        return 1 + esk_cm_popcount16(h1 & 0xff);
    if ((h1 & 0xf000) == 0xd000 && (h1 & 0x0e00) != 0x0e00) {
        *refill = 1;                                    /* b<cond>: 1, 2 taken */
        return 1;
    }
    if ((h1 & 0xf800) == 0xe000) {                      /* b: 2 */
        *refill = 1;
        return 1;
    }
    if (h1 == 0xbf20 || h1 == 0xbf30)                   /* wfe, wfi: 2 (and the wait) */
        return 2;
    return 1;
}

#endif
