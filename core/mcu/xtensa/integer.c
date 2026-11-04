/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Original integer semantics; encoding fixtures assembled independently by GNU
 * binutils, used as an external tool only. No decoder tables copied. */
#include "mcu/xtensa/integer.h"

/* Sign-extension as unsigned arithmetic avoids signed overflow/conversions. */
static uint32_t extend(uint32_t x, unsigned bits)
{
    uint32_t sign = UINT32_C(1) << (bits - 1);
    return (x ^ sign) - sign;
}
es_status esk_xtensa_integer_execute(uint32_t a[16], const unsigned char *code,
                                     size_t available, size_t *consumed)
{
    uint32_t insn, result;
    unsigned op, t, s, r, op1, op2, dest;
    if (consumed) *consumed = 0;
    if (!a || !code || !consumed) return ES_EINVAL;
    if (available < 2) return ES_EINVAL;
    insn = (uint32_t)code[0] | (uint32_t)code[1] << 8;
    op = insn & 15u; t = (insn >> 4) & 15u; s = (insn >> 8) & 15u; r = insn >> 12;
    if (op == 10 || op == 11 || (op == 12 && t < 8) || (op == 13 && r == 0)) {
        if (op == 10) { dest = r; result = a[s] + a[t]; }
        else if (op == 11) { dest = r; result = a[s] + (t ? (uint32_t)t : UINT32_MAX); }
        else if (op == 12) {
            uint32_t immediate = (uint32_t)t << 4 | r;
            dest = s; result = immediate >= 96 ? immediate - 128u : immediate;
        } else { dest = t; result = a[s]; }
        a[dest] = result; *consumed = 2; return ES_OK;
    }
    if (op >= 8) return ES_EUNSUPPORTED;
    if (available < 3) return ES_EINVAL;
    insn = (uint32_t)code[0] | (uint32_t)code[1] << 8 | (uint32_t)code[2] << 16;
    op = insn & 15u; t = (insn >> 4) & 15u; s = (insn >> 8) & 15u;
    r = (insn >> 12) & 15u; op1 = (insn >> 16) & 15u; op2 = insn >> 20;
    dest = r;
    if (op == 2 && r == 10) {
        dest = t; result = extend((insn >> 16) | (uint32_t)s << 8, 12);
    } else if (op == 2 && (r == 12 || r == 13)) {
        uint32_t immediate = extend(insn >> 16, 8);
        dest = t; result = a[s] + (r == 13 ? immediate << 8 : immediate);
    } else if (op == 0 && op1 == 0 && op2 >= 8) {
        unsigned shift = op2 & 3u;
        uint32_t left = a[s] << shift;
        result = op2 < 12 ? left + a[t] : left - a[t];
    } else if (op == 0 && op1 == 0 && op2 >= 1 && op2 <= 3) {
        result = op2 == 1 ? a[s] & a[t] : op2 == 2 ? a[s] | a[t] : a[s] ^ a[t];
    } else if (op == 0 && op1 == 0 && op2 == 6 && s <= 1) {
        result = s == 0 || (a[t] & UINT32_C(0x80000000)) ? 0u - a[t] : a[t];
    } else if (op == 0 && op1 == 1 && op2 <= 1) {
        unsigned shift = 32u - (op2 * 16u + t);
        if (shift >= 32) return ES_EUNSUPPORTED;
        result = a[s] << shift;
    } else if (op == 0 && op1 == 1 && op2 == 4) {
        result = a[t] >> s;
    } else if (op == 0 && op1 == 1 && (op2 == 2 || op2 == 3)) {
        unsigned shift = s + (op2 & 1u) * 16u;
        result = a[t] >> shift;
        if (shift && (a[t] & UINT32_C(0x80000000))) result |= UINT32_MAX << (32u - shift);
    } else if (op == 0 && (op1 == 4 || op1 == 5)) {
        unsigned shift = s + (op1 & 1u) * 16u;
        if (shift + op2 + 1 > 32) return ES_EUNSUPPORTED;
        result = (a[t] >> shift) & ((UINT32_C(1) << (op2 + 1)) - 1u);
    } else if (op == 0 && op1 == 2 && op2 == 8) {
        result = a[s] * a[t];
    } else {
        return ES_EUNSUPPORTED;
    }
    a[dest] = result; *consumed = 3;
    return ES_OK;
}
