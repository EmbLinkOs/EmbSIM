/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* disasm.c -- a MIPS32r2 instruction's text, for the Debug perspective:
 * the integer set, coprocessor 0's instructions, ll and sc, the cache
 * and pref hints, in GNU as's syntax with the ABI's register names; a
 * branch's target as an address. Anything else is `.inst 0x...`.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/disasm.c (dis_mips). */
#include <stdio.h>

#include "mcu/mips/mips.h"

static const char *const mr[32] = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2",
    "t3", "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5", "s6",
    "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra" };

int esk_mips_disasm(u32 pc, u32 i, char *b, size_t n)
{
    u32 op = i >> 26, rs = (i >> 21) & 31, rt = (i >> 16) & 31;
    u32 rd = (i >> 11) & 31, sa = (i >> 6) & 31, fn = i & 0x3f;
    s32 imm = (s32)(signed short)(i & 0xffff);
    u32 t = pc + 4 + ((u32)imm << 2);
    static const char *const ld[16] = {
        "lb", "lh", "lwl", "lw", "lbu", "lhu", "lwr", 0,
        "sb", "sh", "swl", "sw", 0, 0, "swr", "cache" };
    static const char *const alu[8] = {
        0, 0, "slti", "sltiu", "andi", "ori", "xori", "lui" };
    static const char *const rr[64] = {
        [0x20] = "add", [0x21] = "addu", [0x22] = "sub", [0x23] = "subu",
        [0x24] = "and", [0x25] = "or", [0x26] = "xor", [0x27] = "nor",
        [0x2a] = "slt", [0x2b] = "sltu", [0x0a] = "movz", [0x0b] = "movn" };
    static const char *const tr[8] = {
        "tge", "tgeu", "tlt", "tltu", "teq", 0, "tne", 0 };
    if (i == 0)
        return snprintf(b, n, "nop"), 4;
    switch (op) {
    case 0:
        if (rr[fn])
            return snprintf(b, n, "%s %s, %s, %s", rr[fn], mr[rd], mr[rs], mr[rt]), 4;
        if (fn >= 0x30 && fn <= 0x36 && tr[fn - 0x30])
            return snprintf(b, n, "%s %s, %s", tr[fn - 0x30], mr[rs], mr[rt]), 4;
        switch (fn) {
        case 0x00:
            if (!rd && sa == 3)
                return snprintf(b, n, "ehb"), 4;
            return snprintf(b, n, "sll %s, %s, %u", mr[rd], mr[rt], sa), 4;
        case 0x02: return snprintf(b, n, "%s %s, %s, %u", rs & 1 ? "rotr" : "srl",
                                   mr[rd], mr[rt], sa), 4;
        case 0x03: return snprintf(b, n, "sra %s, %s, %u", mr[rd], mr[rt], sa), 4;
        case 0x04: return snprintf(b, n, "sllv %s, %s, %s", mr[rd], mr[rt], mr[rs]), 4;
        case 0x06: return snprintf(b, n, "%s %s, %s, %s", sa & 1 ? "rotrv" : "srlv",
                                   mr[rd], mr[rt], mr[rs]), 4;
        case 0x07: return snprintf(b, n, "srav %s, %s, %s", mr[rd], mr[rt], mr[rs]), 4;
        case 0x08: return snprintf(b, n, "jr %s", mr[rs]), 4;
        case 0x09: return snprintf(b, n, "jalr %s, %s", mr[rd], mr[rs]), 4;
        case 0x0c: return snprintf(b, n, "syscall"), 4;
        case 0x0d: return snprintf(b, n, "break %u", (i >> 16) & 0x3ff), 4;
        case 0x0f: return snprintf(b, n, "sync"), 4;
        case 0x10: case 0x12:
            return snprintf(b, n, "%s %s, $ac%u", fn == 0x10 ? "mfhi" : "mflo",
                            mr[rd], (i >> 21) & 3), 4;
        case 0x11: case 0x13:
            return snprintf(b, n, "%s %s, $ac%u", fn == 0x11 ? "mthi" : "mtlo",
                            mr[rs], (i >> 11) & 3), 4;
        case 0x18: case 0x19: case 0x1a: case 0x1b: {
            static const char *const md[4] = { "mult", "multu", "div", "divu" };
            return snprintf(b, n, "%s %s, %s", md[fn - 0x18], mr[rs], mr[rt]), 4;
        }
        }
        break;
    case 1: {
        static const char *const ri[32] = {
            "bltz", "bgez", "bltzl", "bgezl", 0, 0, 0, 0, "tgei", "tgeiu",
            "tlti", "tltiu", "teqi", 0, "tnei", 0, "bltzal", "bgezal",
            "bltzall", "bgezall" };
        if (rt < 32 && ri[rt]) {
            if (rt >= 8 && rt <= 14)
                return snprintf(b, n, "%s %s, %d", ri[rt], mr[rs], imm), 4;
            return snprintf(b, n, "%s %s, 0x%08x", ri[rt], mr[rs], t), 4;
        }
        break;
    }
    case 2: case 3:
        return snprintf(b, n, "%s 0x%08x", op == 2 ? "j" : "jal",
                        ((pc + 4) & 0xf0000000u) | (i & 0x03ffffffu) << 2), 4;
    case 4: case 5: case 20: case 21: {
        static const char *const be[4] = { "beq", "bne", "beql", "bnel" };
        return snprintf(b, n, "%s %s, %s, 0x%08x", be[(op & 1) | (op >> 3 & 2)],
                        mr[rs], mr[rt], t), 4;
    }
    case 6: case 7: case 22: case 23: {
        static const char *const bz[4] = { "blez", "bgtz", "blezl", "bgtzl" };
        return snprintf(b, n, "%s %s, 0x%08x", bz[(op & 1) | (op >> 3 & 2)], mr[rs], t), 4;
    }
    case 8: case 9:
        return snprintf(b, n, "%s %s, %s, %d", op == 8 ? "addi" : "addiu",
                        mr[rt], mr[rs], imm), 4;
    case 16:
        if (rs == 0 || rs == 4)
            return snprintf(b, n, "%s %s, $%u, %u", rs ? "mtc0" : "mfc0", mr[rt],
                            rd, i & 7), 4;
        if (rs == 11)
            return snprintf(b, n, "%s %s", i & 0x20 ? "ei" : "di", mr[rt]), 4;
        if (rs == 10 || rs == 14)
            return snprintf(b, n, "%s %s, %s", rs == 10 ? "rdpgpr" : "wrpgpr",
                            mr[rd], mr[rt]), 4;
        switch (i) {
        case 0x42000018: return snprintf(b, n, "eret"), 4;
        case 0x4200001f: return snprintf(b, n, "deret"), 4;
        case 0x42000020: return snprintf(b, n, "wait"), 4;
        case 0x42000001: return snprintf(b, n, "tlbr"), 4;
        case 0x42000002: return snprintf(b, n, "tlbwi"), 4;
        case 0x42000006: return snprintf(b, n, "tlbwr"), 4;
        case 0x42000008: return snprintf(b, n, "tlbp"), 4;
        }
        break;
    case 28:
        switch (fn) {
        case 0x00: case 0x01: case 0x04: case 0x05: {
            static const char *const ma[6] = { "madd", "maddu", 0, 0, "msub", "msubu" };
            return snprintf(b, n, "%s $ac%u, %s, %s", ma[fn], (i >> 11) & 3,
                            mr[rs], mr[rt]), 4;
        }
        case 0x02: return snprintf(b, n, "mul %s, %s, %s", mr[rd], mr[rs], mr[rt]), 4;
        case 0x20: case 0x21:
            return snprintf(b, n, "%s %s, %s", fn == 0x20 ? "clz" : "clo", mr[rd], mr[rs]), 4;
        case 0x3f: return snprintf(b, n, "sdbbp %u", (i >> 6) & 0xfffff), 4;
        }
        break;
    case 31:
        switch (fn) {
        case 0x00: return snprintf(b, n, "ext %s, %s, %u, %u", mr[rt], mr[rs], sa, rd + 1), 4;
        case 0x04: return snprintf(b, n, "ins %s, %s, %u, %u", mr[rt], mr[rs], sa,
                                   rd - sa + 1), 4;
        case 0x20:
            if (sa == 0x02 || sa == 0x10 || sa == 0x18)
                return snprintf(b, n, "%s %s, %s", sa == 0x02 ? "wsbh" : sa == 0x10
                                ? "seb" : "seh", mr[rd], mr[rt]), 4;
            break;
        case 0x38:
            if (sa == 0x12)
                return snprintf(b, n, "rddsp %s, 0x%x", mr[rd], (i >> 16) & 0x3ff), 4;
            if (sa == 0x13)
                return snprintf(b, n, "wrdsp %s, 0x%x", mr[rs], (i >> 11) & 0x3ff), 4;
            break;
        case 0x3b: return snprintf(b, n, "rdhwr %s, $%u", mr[rt], rd), 4;
        }
        break;
    case 48: case 56:
        return snprintf(b, n, "%s %s, %d(%s)", op == 48 ? "ll" : "sc", mr[rt], imm,
                        mr[rs]), 4;
    case 51:
        return snprintf(b, n, "pref %u, %d(%s)", rt, imm, mr[rs]), 4;
    default:
        if (op == 15)
            return snprintf(b, n, "lui %s, 0x%x", mr[rt], i & 0xffff), 4;
        if (op >= 10 && op <= 14)
            return snprintf(b, n, "%s %s, %s, %d", alu[op - 8], mr[rt], mr[rs],
                            op >= 12 ? (int)(i & 0xffff) : imm), 4;
        if (op >= 32 && op <= 47 && ld[op - 32]) {
            if (op == 47)
                return snprintf(b, n, "cache 0x%x, %d(%s)", rt, imm, mr[rs]), 4;
            return snprintf(b, n, "%s %s, %d(%s)", ld[op - 32], mr[rt], imm, mr[rs]), 4;
        }
        break;
    }
    return snprintf(b, n, ".inst 0x%08x", i), 4;
}

