/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* disasm.c -- the RV32 instruction at an address, in the ABI's register
 * names; a compressed one as the instruction it expands to, prefixed c.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/disasm.c (dis_riscv),
 * at 32 bits. */
#include <stdio.h>

#include "mcu/riscv/riscv.h"

static const char *const xn[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1",
    "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6" };

static int rv32(u32 pc, u32 i, int xlen, char *b, size_t n)
{
    u32 op = i & 0x7f, rd = (i >> 7) & 31, f3 = (i >> 12) & 7;
    u32 r1 = (i >> 15) & 31, r2 = (i >> 20) & 31, f7 = i >> 25;
    s32 immi = (s32)i >> 20;
    s32 imms = (s32)((i >> 25) << 5 | ((i >> 7) & 31)) << 20 >> 20;
    switch (op) {
    case 0x03: {
        static const char *const ld[8] = { "lb", "lh", "lw", "ld", "lbu", "lhu", "lwu", 0 };
        if (!ld[f3])
            return 0;
        snprintf(b, n, "%s %s, %d(%s)", ld[f3], xn[rd], immi, xn[r1]);
        return 1;
    }
    case 0x23: {
        static const char *const st[4] = { "sb", "sh", "sw", "sd" };
        if (f3 > 3)
            return 0;
        snprintf(b, n, "%s %s, %d(%s)", st[f3], xn[r2], imms, xn[r1]);
        return 1;
    }
    case 0x13: case 0x1b: {
        static const char *const ar[8] = { "addi", "slli", "slti", "sltiu", "xori",
                                           "srli", "ori", "andi" };
        const char *m = ar[f3];
        int w = op == 0x1b;
        if (f3 == 5 && (i >> 30 & 1))
            m = "srai";
        if (f3 == 1 || f3 == 5)
            snprintf(b, n, "%s%s %s, %s, %u", m, w ? "w" : "", xn[rd], xn[r1],
                     (i >> 20) & (xlen == 64 && !w ? 63u : 31u));
        else
            snprintf(b, n, "%s%s %s, %s, %d", m, w ? "w" : "", xn[rd], xn[r1], immi);
        return 1;
    }
    case 0x33: case 0x3b: {
        static const char *const ar[8] = { "add", "sll", "slt", "sltu", "xor", "srl",
                                           "or", "and" };
        static const char *const md[8] = { "mul", "mulh", "mulhsu", "mulhu", "div",
                                           "divu", "rem", "remu" };
        const char *m = f7 == 1 ? md[f3] : ar[f3];
        if (f7 == 0x20 && f3 == 0)
            m = "sub";
        else if (f7 == 0x20 && f3 == 5)
            m = "sra";
        else if (f7 != 0 && f7 != 1)
            return 0;
        snprintf(b, n, "%s%s %s, %s, %s", m, op == 0x3b ? "w" : "", xn[rd], xn[r1], xn[r2]);
        return 1;
    }
    case 0x37: snprintf(b, n, "lui %s, 0x%x", xn[rd], i >> 12); return 1;
    case 0x17: snprintf(b, n, "auipc %s, 0x%x", xn[rd], i >> 12); return 1;
    case 0x6f: {
        s32 off = (s32)((i >> 31) << 20 | ((i >> 12) & 0xff) << 12 |
                        ((i >> 20) & 1) << 11 | ((i >> 21) & 0x3ff) << 1);
        off = (off << 11) >> 11;
        snprintf(b, n, "jal %s, 0x%08x", xn[rd], pc + (u32)off);
        return 1;
    }
    case 0x67:
        snprintf(b, n, "jalr %s, %d(%s)", xn[rd], immi, xn[r1]);
        return 1;
    case 0x63: {
        static const char *const br[8] = { "beq", "bne", 0, 0, "blt", "bge", "bltu", "bgeu" };
        s32 off = (s32)((i >> 31) << 12 | ((i >> 7) & 1) << 11 | ((i >> 25) & 0x3f) << 5 |
                        ((i >> 8) & 15) << 1);
        off = (off << 19) >> 19;
        if (!br[f3])
            return 0;
        snprintf(b, n, "%s %s, %s, 0x%08x", br[f3], xn[r1], xn[r2], pc + (u32)off);
        return 1;
    }
    case 0x73:
        if (i == 0x00000073) { snprintf(b, n, "ecall"); return 1; }
        if (i == 0x00100073) { snprintf(b, n, "ebreak"); return 1; }
        if (i == 0x30200073) { snprintf(b, n, "mret"); return 1; }
        if (i == 0x10500073) { snprintf(b, n, "wfi"); return 1; }
        if (f3 && f3 != 4) {
            static const char *const cs_[8] = { 0, "csrrw", "csrrs", "csrrc", 0,
                                                "csrrwi", "csrrsi", "csrrci" };
            if (f3 >= 5)
                snprintf(b, n, "%s %s, 0x%03x, %u", cs_[f3], xn[rd], i >> 20, r1);
            else
                snprintf(b, n, "%s %s, 0x%03x, %s", cs_[f3], xn[rd], i >> 20, xn[r1]);
            return 1;
        }
        return 0;
    case 0x2f: {
        static const char *const am[32] = {
            [0] = "amoadd", [1] = "amoswap", [2] = "lr", [3] = "sc", [4] = "amoxor",
            [8] = "amoor", [12] = "amoand", [16] = "amomin", [20] = "amomax",
            [24] = "amominu", [28] = "amomaxu" };
        const char *m = am[i >> 27];
        if (!m || (f3 != 2 && f3 != 3))
            return 0;
        snprintf(b, n, "%s.%c %s, %s, (%s)", m, f3 == 2 ? 'w' : 'd', xn[rd], xn[r2], xn[r1]);
        return 1;
    }
    case 0x0f:
        snprintf(b, n, "%s", f3 == 1 ? "fence.i" : "fence");
        return 1;
    case 0x07: case 0x27:
        snprintf(b, n, "%s f%u, %d(%s)", op == 0x07 ? (f3 == 2 ? "flw" : "fld") :
                 (f3 == 2 ? "fsw" : "fsd"), op == 0x07 ? rd : r2,
                 op == 0x07 ? immi : imms, xn[r1]);
        return 1;
    }
    return 0;
}

int esk_riscv_disasm(u32 pc, u32 insn, char *b, size_t n)
{
    const int xlen = 32;
    if ((insn & 3) != 3) {
        u32 x = esk_rvc_expand(insn & 0xffff, xlen);
        char t[96];
        if (x && rv32(pc, x, xlen, t, sizeof t))
            snprintf(b, n, "c.%s", t);
        else
            snprintf(b, n, ".insn 0x%04x", insn & 0xffff);
        return 2;
    }
    if (!rv32(pc, insn, xlen, b, n))
        snprintf(b, n, ".insn 0x%08x", insn);
    return 4;
}

