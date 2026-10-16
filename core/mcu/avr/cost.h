/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* cost.h -- the cycles of an AVR instruction (timing tier "exact",
 * ADR-015): the ATmega328P datasheet's instruction set summary. A taken
 * conditional branch costs 1 more and a skip 1 or 2 more; the core adds
 * those as they happen.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/bench/cost.h (avr_cost), the
 * table EmbCC's QEMU plugin and simulator share. */
#ifndef ESK_AVR_COST_H
#define ESK_AVR_COST_H
#include "embsim/compiler.h"

/* the instruction word's cycles, and whether it is BRBS or BRBC */
ES_INLINE int esk_avr_cost(unsigned w, int *branch)
{
    *branch = 0;
    switch (w >> 12) {
    case 0x0:
        return (w & 0xfe00) == 0x0200 ? 2 : 1;          /* muls, mulsu, fmul* */
    case 0x8: case 0xa:
        return 2;                                       /* ldd, std */
    case 0x9:
        break;
    case 0xc:
        return 2;                                       /* rjmp */
    case 0xd:
        return 3;                                       /* rcall */
    case 0xf:
        if (!(w & 0x0800))
            *branch = 1;                                /* brbs, brbc */
        return 1;
    default:
        return 1;
    }
    switch ((w >> 8) & 0xf) {
    case 0x0: case 0x1:                                 /* lds ld lpm elpm pop */
        return (w & 0xc) == 0x4 ? 3 : 2;
    case 0x2: case 0x3:                                 /* sts st push */
        return 2;
    case 0x4: case 0x5:
        if ((w & 0xfe0e) == 0x940c)
            return 3;                                   /* jmp */
        if ((w & 0xfe0e) == 0x940e)
            return 4;                                   /* call */
        if ((w & 0xf) < 8 || (w & 0xf) == 0xa || (w & 0xff0f) == 0x9408)
            return 1;                                   /* one register, bset, bclr */
        switch (w) {
        case 0x9508: case 0x9518: case 0x9519:          /* ret reti eicall */
            return 4;
        case 0x95c8: case 0x95d8: case 0x9509:          /* lpm elpm icall */
            return 3;
        case 0x9409: case 0x9419:                       /* ijmp eijmp */
            return 2;
        }
        return 1;                                       /* sleep break wdr spm */
    case 0x6: case 0x7: case 0x8: case 0xa:             /* adiw sbiw cbi sbi */
        return 2;
    case 0x9: case 0xb:                                 /* sbic sbis */
        return 1;
    default:                                            /* mul */
        return 2;
    }
}

#endif
