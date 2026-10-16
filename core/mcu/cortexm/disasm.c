/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* disasm.c -- one Thumb instruction as text, for the debugger (SPEC-008
 * debug.disassemble): ARMv6-M's 16-bit set whole, and the 32-bit forms a
 * compiler emits for loads, stores, multiples, calls and branches, divides
 * and multiplies. What it does not know it prints as the encoding,
 * `.inst 0x...`.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/disasm.c (the Thumb
 * part; the RISC-V, MIPS and PIC24 decoders stay with EmbCC until those
 * cores come over). */
#include <stdio.h>
#include <string.h>

#include "mcu/cortexm/cortexm.h"

static const char *const rn[16] = { "r0", "r1", "r2", "r3", "r4", "r5", "r6",
                                    "r7", "r8", "r9", "r10", "r11", "r12",
                                    "sp", "lr", "pc" };
static const char *const cc[16] = { "eq", "ne", "cs", "cc", "mi", "pl", "vs",
                                    "vc", "hi", "ls", "ge", "lt", "gt", "le",
                                    "", "" };

static void reglist(char *b, size_t n, u32 mask)
{
    size_t k = strlen(b);
    int first = 1;
    snprintf(b + k, n - k, "{");
    for (int r = 0; r < 16; r++)
        if (mask >> r & 1) {
            k = strlen(b);
            snprintf(b + k, n - k, "%s%s", first ? "" : ", ", rn[r]);
            first = 0;
        }
    k = strlen(b);
    snprintf(b + k, n - k, "}");
}

static int thumb16(u32 pc, u32 h, char *b, size_t n)
{
    u32 rd = h & 7, rm = (h >> 3) & 7, rn_ = (h >> 6) & 7, imm5 = (h >> 6) & 31;
    static const char *const dp[16] = { "ands", "eors", "lsls", "lsrs", "asrs",
                                        "adcs", "sbcs", "rors", "tst", "rsbs",
                                        "cmp", "cmn", "orrs", "muls", "bics",
                                        "mvns" };
    static const char *const ls[8] = { "str", "strh", "strb", "ldrsb", "ldr",
                                       "ldrh", "ldrb", "ldrsh" };
    switch (h >> 11) {
    case 0:
        if (imm5)
            snprintf(b, n, "lsls %s, %s, #%u", rn[rd], rn[rm], imm5);
        else
            snprintf(b, n, "movs %s, %s", rn[rd], rn[rm]);
        return 1;
    case 1: snprintf(b, n, "lsrs %s, %s, #%u", rn[rd], rn[rm], imm5 ? imm5 : 32); return 1;
    case 2: snprintf(b, n, "asrs %s, %s, #%u", rn[rd], rn[rm], imm5 ? imm5 : 32); return 1;
    case 3:
        if (h & 0x400)
            snprintf(b, n, "%s %s, %s, #%u", h & 0x200 ? "subs" : "adds", rn[rd],
                     rn[rm], rn_);
        else
            snprintf(b, n, "%s %s, %s, %s", h & 0x200 ? "subs" : "adds", rn[rd],
                     rn[rm], rn[rn_]);
        return 1;
    case 4: snprintf(b, n, "movs %s, #%u", rn[(h >> 8) & 7], h & 255); return 1;
    case 5: snprintf(b, n, "cmp %s, #%u", rn[(h >> 8) & 7], h & 255); return 1;
    case 6: snprintf(b, n, "adds %s, #%u", rn[(h >> 8) & 7], h & 255); return 1;
    case 7: snprintf(b, n, "subs %s, #%u", rn[(h >> 8) & 7], h & 255); return 1;
    case 8:
        if (!(h & 0x400)) {
            snprintf(b, n, "%s %s, %s", dp[(h >> 6) & 15], rn[rd], rn[rm]);
            return 1;
        } else {
            u32 d = (h & 7) | (h >> 4 & 8), m = (h >> 3) & 15;
            switch ((h >> 8) & 3) {
            case 0: snprintf(b, n, "add %s, %s", rn[d], rn[m]); return 1;
            case 1: snprintf(b, n, "cmp %s, %s", rn[d], rn[m]); return 1;
            case 2: snprintf(b, n, "mov %s, %s", rn[d], rn[m]); return 1;
            default: snprintf(b, n, "%s %s", h & 0x80 ? "blx" : "bx", rn[m]); return 1;
            }
        }
    case 9:
        snprintf(b, n, "ldr %s, [pc, #%u]  @ 0x%08x", rn[(h >> 8) & 7], (h & 255) * 4,
                 ((pc + 4) & ~3u) + (h & 255) * 4);
        return 1;
    case 10: case 11:
        snprintf(b, n, "%s %s, [%s, %s]", ls[(h >> 9) & 7], rn[rd], rn[rm], rn[rn_]);
        return 1;
    case 12: case 13: case 14: case 15: {
        int byte = (h >> 12) & 1, ld = (h >> 11) & 1;
        snprintf(b, n, "%s %s, [%s, #%u]", byte ? (ld ? "ldrb" : "strb") : (ld ? "ldr" : "str"),
                 rn[rd], rn[rm], byte ? imm5 : imm5 * 4);
        return 1;
    }
    case 16: case 17:
        snprintf(b, n, "%s %s, [%s, #%u]", h & 0x800 ? "ldrh" : "strh", rn[rd], rn[rm],
                 imm5 * 2);
        return 1;
    case 18: case 19:
        snprintf(b, n, "%s %s, [sp, #%u]", h & 0x800 ? "ldr" : "str", rn[(h >> 8) & 7],
                 (h & 255) * 4);
        return 1;
    case 20: snprintf(b, n, "add %s, pc, #%u", rn[(h >> 8) & 7], (h & 255) * 4); return 1;
    case 21: snprintf(b, n, "add %s, sp, #%u", rn[(h >> 8) & 7], (h & 255) * 4); return 1;
    case 22: case 23:
        if ((h & 0xff00) == 0xb000) {
            snprintf(b, n, "%s sp, #%u", h & 0x80 ? "sub" : "add", (h & 127) * 4);
            return 1;
        }
        if ((h & 0xf500) == 0xb100) {
            u32 off = ((h >> 3) & 0x3e) | ((h >> 4) & 0x40);
            snprintf(b, n, "%s %s, 0x%08x", h & 0x800 ? "cbnz" : "cbz", rn[rd], pc + 4 + off);
            return 1;
        }
        if ((h & 0xff00) == 0xb200) {
            static const char *const x[4] = { "sxth", "sxtb", "uxth", "uxtb" };
            snprintf(b, n, "%s %s, %s", x[(h >> 6) & 3], rn[rd], rn[rm]);
            return 1;
        }
        if ((h & 0xfe00) == 0xb400 || (h & 0xfe00) == 0xbc00) {
            int pop = (h & 0x800) != 0;
            snprintf(b, n, "%s ", pop ? "pop" : "push");
            reglist(b, n, (h & 255) | (h & 0x100 ? (pop ? 0x8000u : 0x4000u) : 0));
            return 1;
        }
        if ((h & 0xffe8) == 0xb660) {
            snprintf(b, n, "cpsi%s %s", h & 0x10 ? "d" : "e", h & 2 ? "i" : "f");
            return 1;
        }
        if ((h & 0xff00) == 0xba00) {
            static const char *const x[4] = { "rev", "rev16", "", "revsh" };
            snprintf(b, n, "%s %s, %s", x[(h >> 6) & 3], rn[rd], rn[rm]);
            return 1;
        }
        if ((h & 0xff00) == 0xbe00) {
            snprintf(b, n, "bkpt 0x%02x", h & 255);
            return 1;
        }
        if ((h & 0xff00) == 0xbf00) {
            static const char *const hint[5] = { "nop", "yield", "wfe", "wfi", "sev" };
            if (h & 15)
                snprintf(b, n, "it 0x%x", h & 255);
            else if (((h >> 4) & 15) < 5)
                snprintf(b, n, "%s", hint[(h >> 4) & 15]);
            else
                return 0;
            return 1;
        }
        return 0;
    case 24: case 25:
        snprintf(b, n, "%s %s!, ", h & 0x800 ? "ldmia" : "stmia", rn[(h >> 8) & 7]);
        reglist(b, n, h & 255);
        return 1;
    case 26: case 27: {
        u32 c = (h >> 8) & 15;
        if (c == 14) {
            snprintf(b, n, "udf #%u", h & 255);
            return 1;
        }
        if (c == 15) {
            snprintf(b, n, "svc #%u", h & 255);
            return 1;
        }
        snprintf(b, n, "b%s 0x%08x", cc[c], pc + 4 + (u32)((s32)(h << 24) >> 23));
        return 1;
    }
    case 28:
        snprintf(b, n, "b 0x%08x", pc + 4 + (u32)((s32)(h << 21) >> 20));
        return 1;
    }
    return 0;
}

static int thumb32(u32 pc, u32 h1, u32 h2, char *b, size_t n)
{
    u32 rn_ = h1 & 15, rt = h2 >> 12;
    if ((h1 & 0xf800) == 0xf000 && (h2 & 0x8000)) {
        u32 s = (h1 >> 10) & 1, j1 = (h2 >> 13) & 1, j2 = (h2 >> 11) & 1;
        if ((h2 & 0x5000) == 0x5000 || (h2 & 0x5000) == 0x1000) {
            u32 i1 = !(j1 ^ s), i2 = !(j2 ^ s);
            s32 off = (s32)(s << 24 | i1 << 23 | i2 << 22 | (h1 & 0x3ff) << 12 |
                            (h2 & 0x7ff) << 1);
            off = (off << 7) >> 7;
            snprintf(b, n, "%s 0x%08x", h2 & 0x4000 ? "bl" : "b.w", pc + 4 + (u32)off);
            return 1;
        }
        if ((h2 & 0x5000) == 0) {
            u32 c = (h1 >> 6) & 15;
            if (c >= 14)
                return 0;
            s32 off = (s32)(s << 20 | j2 << 19 | j1 << 18 | (h1 & 0x3f) << 12 |
                            (h2 & 0x7ff) << 1);
            off = (off << 11) >> 11;
            snprintf(b, n, "b%s.w 0x%08x", cc[c], pc + 4 + (u32)off);
            return 1;
        }
        if (h1 == 0xf7f0 && (h2 & 0xf000) == 0xa000) {
            snprintf(b, n, "udf.w #%u", (h1 & 15) << 12 | (h2 & 0xfff));
            return 1;
        }
        return 0;
    }
    if ((h1 & 0xfe50) == 0xe810 || (h1 & 0xfe50) == 0xe800) {     /* LDM/STM */
        int ld = (h1 >> 4) & 1, db = (h1 >> 8) & 1, wb = (h1 >> 5) & 1;
        if (rn_ == 13 && wb && ld != db)
            snprintf(b, n, "%s ", ld ? "pop.w" : "push.w");
        else
            snprintf(b, n, "%s%s %s%s, ", ld ? "ldm" : "stm", db ? "db" : "ia",
                     rn[rn_], wb ? "!" : "");
        reglist(b, n, h2);
        return 1;
    }
    if ((h1 & 0xfe40) == 0xe840 && (h1 & 0x0120)) {               /* LDRD/STRD */
        u32 imm = (h2 & 255) * 4;
        int u = (h1 >> 7) & 1, p = (h1 >> 8) & 1, w = (h1 >> 5) & 1;
        snprintf(b, n, "%s %s, %s, [%s%s, #%s%u]%s%s", h1 & 0x10 ? "ldrd" : "strd",
                 rn[rt], rn[(h2 >> 8) & 15], rn[rn_], p ? "" : "]", u ? "" : "-", imm,
                 p ? "]" : "", p && w ? "!" : "");
        return 1;
    }
    if ((h1 & 0xfe00) == 0xf800) {                                  /* LDR/STR */
        static const char *const nm[2][4] = { { "strb", "strh", "str", "?" },
                                              { "ldrb", "ldrh", "ldr", "?" } };
        int ld = (h1 >> 4) & 1, sz = (h1 >> 5) & 3, sgn = (h1 >> 8) & 1;
        if (sz == 3)
            return 0;
        const char *op = nm[ld][sz];
        char sop[8];
        if (sgn && ld) {
            snprintf(sop, sizeof sop, "ldrs%s", sz ? "h" : "b");
            op = sop;
        }
        if (rn_ == 15 && ld) {
            u32 imm = h2 & 0xfff;
            snprintf(b, n, "%s.w %s, [pc, #%s%u]", op, rn[rt], h1 & 0x80 ? "" : "-", imm);
        } else if (h1 & 0x80) {
            snprintf(b, n, "%s.w %s, [%s, #%u]", op, rn[rt], rn[rn_], h2 & 0xfff);
        } else if ((h2 & 0x800) == 0x800) {
            int p = (h2 >> 10) & 1, u = (h2 >> 9) & 1, w = (h2 >> 8) & 1;
            snprintf(b, n, "%s %s, [%s%s, #%s%u]%s", op, rn[rt], rn[rn_], p ? "" : "]",
                     u ? "" : "-", h2 & 255, p ? (w ? "]!" : "]") : "");
        } else if (!(h2 & 0xfc0)) {
            snprintf(b, n, "%s.w %s, [%s, %s, lsl #%u]", op, rn[rt], rn[rn_],
                     rn[h2 & 15], (h2 >> 4) & 3);
        } else
            return 0;
        return 1;
    }
    if ((h1 & 0xfa00) == 0xf000 && !(h2 & 0x8000)) {             /* modified imm */
        static const char *const dp[16] = { "and", "bic", "orr", "orn", "eor", 0, 0,
                                            0, "add", 0, "adc", "sbc", 0, "sub",
                                            "rsb", 0 };
        u32 op = (h1 >> 5) & 15, s = (h1 >> 4) & 1, rd = (h2 >> 8) & 15;
        u32 i12 = ((h1 >> 10) & 1) << 11 | ((h2 >> 12) & 7) << 8 | (h2 & 255);
        u32 imm8 = i12 & 255, imm;
        switch (i12 >> 8) {
        case 0: imm = imm8; break;
        case 1: imm = imm8 << 16 | imm8; break;
        case 2: imm = imm8 << 24 | imm8 << 8; break;
        case 3: imm = imm8 * 0x01010101u; break;
        default: {
            u32 v = 0x80 | (i12 & 0x7f), rot = i12 >> 7;
            imm = v >> rot | v << (32 - rot);
        }
        }
        if (!dp[op])
            return 0;
        if (rd == 15 && s && (op == 0 || op == 4 || op == 8 || op == 13))
            snprintf(b, n, "%s %s, #%u", op == 0 ? "tst" : op == 4 ? "teq" :
                     op == 8 ? "cmn" : "cmp", rn[rn_], imm);
        else if (rn_ == 15 && (op == 2 || op == 3))
            snprintf(b, n, "%s%s.w %s, #%u", op == 2 ? "mov" : "mvn", s ? "s" : "",
                     rn[rd], imm);
        else
            snprintf(b, n, "%s%s.w %s, %s, #%u", dp[op], s ? "s" : "", rn[rd], rn[rn_], imm);
        return 1;
    }
    if ((h1 & 0xfa00) == 0xf200 && !(h2 & 0x8000)) {             /* plain imm */
        u32 op = (h1 >> 4) & 31, rd = (h2 >> 8) & 15;
        u32 i12 = ((h1 >> 10) & 1) << 11 | ((h2 >> 12) & 7) << 8 | (h2 & 255);
        if (op == 0 || op == 10)
            snprintf(b, n, "%s %s, %s, #%u", op ? "subw" : "addw", rn[rd], rn[rn_], i12);
        else if (op == 4 || op == 12)
            snprintf(b, n, "%s %s, #%u", op == 4 ? "movw" : "movt", rn[rd],
                     (h1 & 15) << 12 | i12);
        else
            return 0;
        return 1;
    }
    if ((h1 & 0xfe00) == 0xea00) {                                  /* shifted reg */
        static const char *const dp[16] = { "and", "bic", "orr", "orn", "eor", 0, 0,
                                            0, "add", 0, "adc", "sbc", 0, "sub",
                                            "rsb", 0 };
        static const char *const sh[4] = { "lsl", "lsr", "asr", "ror" };
        u32 op = (h1 >> 5) & 15, s = (h1 >> 4) & 1, rd = (h2 >> 8) & 15, rm = h2 & 15;
        u32 amt = ((h2 >> 12) & 7) << 2 | ((h2 >> 6) & 3), ty = (h2 >> 4) & 3;
        char shift[24] = "";
        if (amt || ty)
            snprintf(shift, sizeof shift, ", %s #%u", sh[ty], amt ? amt : 32);
        if (!dp[op])
            return 0;
        if (rd == 15 && s && (op == 0 || op == 4 || op == 8 || op == 13))
            snprintf(b, n, "%s.w %s, %s%s", op == 0 ? "tst" : op == 4 ? "teq" :
                     op == 8 ? "cmn" : "cmp", rn[rn_], rn[rm], shift);
        else if (rn_ == 15 && op == 2 && (amt || ty))
            snprintf(b, n, "%s%s.w %s, %s, #%u", sh[ty], s ? "s" : "", rn[rd], rn[rm],
                     amt ? amt : 32);
        else if (rn_ == 15 && (op == 2 || op == 3))
            snprintf(b, n, "%s%s.w %s, %s%s", op == 2 ? "mov" : "mvn", s ? "s" : "",
                     rn[rd], rn[rm], shift);
        else
            snprintf(b, n, "%s%s.w %s, %s, %s%s", dp[op], s ? "s" : "", rn[rd], rn[rn_],
                     rn[rm], shift);
        return 1;
    }
    if ((h1 & 0xff80) == 0xfa00 && (h2 & 0xf0f0) == 0xf000) {     /* shift by reg */
        static const char *const sh[4] = { "lsl", "lsr", "asr", "ror" };
        snprintf(b, n, "%s%s.w %s, %s, %s", sh[(h1 >> 5) & 3], h1 & 0x10 ? "s" : "",
                 rn[(h2 >> 8) & 15], rn[rn_], rn[h2 & 15]);
        return 1;
    }
    if ((h1 & 0xfff0) == 0xfb90 && (h2 & 0xf0) == 0xf0) {
        snprintf(b, n, "sdiv %s, %s, %s", rn[(h2 >> 8) & 15], rn[rn_], rn[h2 & 15]);
        return 1;
    }
    if ((h1 & 0xfff0) == 0xfbb0 && (h2 & 0xf0) == 0xf0) {
        snprintf(b, n, "udiv %s, %s, %s", rn[(h2 >> 8) & 15], rn[rn_], rn[h2 & 15]);
        return 1;
    }
    if ((h1 & 0xfff0) == 0xfb00 && (h2 & 0xf0) == 0x10) {
        snprintf(b, n, "mls %s, %s, %s, %s", rn[(h2 >> 8) & 15], rn[rn_], rn[h2 & 15], rn[rt]);
        return 1;
    }
    if ((h1 & 0xfff0) == 0xfb00 && (h2 & 0xf0) == 0) {
        if (rt == 15)
            snprintf(b, n, "mul %s, %s, %s", rn[(h2 >> 8) & 15], rn[rn_], rn[h2 & 15]);
        else
            snprintf(b, n, "mla %s, %s, %s, %s", rn[(h2 >> 8) & 15], rn[rn_],
                     rn[h2 & 15], rn[rt]);
        return 1;
    }
    return 0;
}

int esk_dis_thumb(u32 pc, u32 h1, u32 h2, char *b, size_t n)
{
    if ((h1 >> 11) >= 0x1d) {
        if (!thumb32(pc, h1, h2, b, n))
            snprintf(b, n, ".inst.w 0x%04x%04x", h1, h2);
        return 4;
    }
    if (!thumb16(pc, h1, b, n))
        snprintf(b, n, ".inst.n 0x%04x", h1);
    return 2;
}
