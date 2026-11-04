/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Semantics: Cadence Xtensa ISA Summary, instruction descriptions. */
#include "mcu/xtensa/execute.h"
#include "mcu/xtensa/integer.h"
#include <string.h>

static uint32_t sx(uint32_t x, unsigned bits)
{
    uint32_t b = UINT32_C(1) << (bits - 1); return (x ^ b) - b;
}
static es_status fail(esk_xtensa_fault *f, unsigned kind, uint32_t pc, uint32_t addr, uint32_t opcode)
{
    if (f) { f->kind = kind; f->pc = pc; f->address = addr; f->opcode = opcode; }
    return kind == ESK_XT_UNSUPPORTED ? ES_EUNSUPPORTED : ES_EINVAL;
}
es_status esk_xtensa_execute(esk_xtensa_state *state, const esk_xtensa_memory *memory,
                             const unsigned char *code, size_t available,
                             size_t *consumed, esk_xtensa_fault *fault)
{
    esk_xtensa_state next;
    uint32_t word, pc, addr = 0, value = 0;
    unsigned op, t, s, r, width, bytes = 0;
    int store = 0, signed16 = 0;
    size_t used = 0;
    es_status st;
    if (consumed) *consumed = 0;
    if (fault) memset(fault, 0, sizeof *fault);
    if (!state || !code || !consumed) return ES_EINVAL;
    pc = state->pc;
    if (available < 2) return fail(fault, ESK_XT_TRUNCATED, pc, pc, 0);
    word = (uint32_t)code[0] | (uint32_t)code[1] << 8;
    op = word & 15u; width = op < 8 ? 3u : 2u;
    if (available < width) return fail(fault, ESK_XT_TRUNCATED, pc, pc, word);
    if (width == 3) word |= (uint32_t)code[2] << 16;
    t = (word >> 4) & 15u; s = (word >> 8) & 15u; r = (word >> 12) & 15u;
    next = *state; next.pc = pc + width;
    st = esk_xtensa_integer_execute(next.a, code, available, &used);
    if (st == ES_OK) { next.pc = pc + (uint32_t)used; width = (unsigned)used; }
    else if (word == 0x0020f0 || word == 0xf03d) { /* NOP / NOP.N */ }
    else if (word == 0x000080 || word == 0xf00d) next.pc = next.a[0]; /* RET/RET.N */
    else if ((word & UINT32_C(0xfff0ff)) == 0x0000a0) next.pc = next.a[s]; /* JX */
    else if ((word & UINT32_C(0xfff0ff)) == 0x0000c0) { /* CALLX0: read before A0 write */
        next.pc = next.a[s]; next.a[0] = pc + 3;
    } else if ((word & 63u) == 5) { /* CALL0 */
        next.pc = (pc & UINT32_C(0xfffffffc)) + 4 + (sx(word >> 6, 18) << 2);
        next.a[0] = pc + 3;
    } else if ((word & 63u) == 6) next.pc = pc + 4 + sx(word >> 6, 18); /* J */
    else if (op == 6 && (t & 3u) == 1) {
        int taken = t == 1 ? next.a[s] == 0 : t == 5 ? next.a[s] != 0 :
                    t == 9 ? (next.a[s] >> 31) != 0 : (next.a[s] >> 31) == 0;
        if (taken) next.pc = pc + 4 + sx(word >> 12, 12);
    } else if (op == 6 && (t == 2 || t == 6 || t == 10 || t == 14 || t == 11 || t == 15)) {
        static const uint32_t constants[16] = {
            UINT32_MAX,1,2,3,4,5,6,7,8,10,12,16,32,64,128,256
        };
        uint32_t left = next.a[s], right = constants[r]; int taken;
        if (t == 11 || t == 15) { if (r < 2) right = r ? 65536u : 32768u; }
        if (t == 10 || t == 14) { left ^= UINT32_C(0x80000000); right ^= UINT32_C(0x80000000); }
        taken = t == 2 ? left == right : t == 6 ? left != right :
                (t == 10 || t == 11) ? left < right : left >= right;
        if (taken) next.pc = pc + 4 + sx(word >> 16, 8);
    } else if (op == 7 && (r == 1 || r == 9 || r == 2 || r == 10 || r == 3 || r == 11)) {
        uint32_t left = next.a[s], right = next.a[t]; int taken;
        if (r == 2 || r == 10) { left ^= UINT32_C(0x80000000); right ^= UINT32_C(0x80000000); }
        taken = r == 1 ? left == right : r == 9 ? left != right :
                (r == 2 || r == 3) ? left < right : left >= right;
        if (taken) next.pc = pc + 4 + sx(word >> 16, 8);
    } else if (op == 12 && t >= 8) {
        int taken = t < 12 ? next.a[s] == 0 : next.a[s] != 0;
        if (taken) next.pc = pc + 4 + ((t & 3u) << 4 | r);
    } else if (op == 8 || op == 9) {
        bytes = 4; addr = next.a[s] + (uint32_t)r * 4; store = op == 9;
    } else if (op == 2 && (r <= 2 || r == 4 || r == 5 || r == 6 || r == 9)) {
        unsigned scale = (r == 0 || r == 4) ? 0u : (r == 1 || r == 5 || r == 9) ? 1u : 2u;
        bytes = 1u << scale; addr = next.a[s] + ((word >> 16) << scale);
        store = r >= 4 && r <= 6; signed16 = r == 9;
    } else if (op == 1) { /* L32R without extended LITBASE option */
        bytes = 4; addr = ((pc + 3) & UINT32_C(0xfffffffc)) + ((word >> 8 | UINT32_C(0xffff0000)) << 2);
    } else return fail(fault, ESK_XT_UNSUPPORTED, pc, pc, word);
    if (bytes) {
        if (addr & (bytes - 1u)) return fail(fault, ESK_XT_ALIGN, pc, addr, word);
        if (store) {
            if (!memory || !memory->write || memory->write(memory->ctx, addr, bytes, next.a[t]) != ES_OK)
                return fail(fault, ESK_XT_STORE, pc, addr, word);
        } else {
            if (!memory || !memory->read || memory->read(memory->ctx, addr, bytes, &value) != ES_OK)
                return fail(fault, ESK_XT_LOAD, pc, addr, word);
            if (bytes == 1) value &= 255u;
            if (bytes == 2) value = signed16 ? sx(value & 65535u, 16) : value & 65535u;
            next.a[t] = value;
        }
    }
    *state = next; *consumed = width; return ES_OK;
}
es_status esk_xtensa_step(esk_xtensa_state *state, const esk_xtensa_memory *memory,
                          esk_xtensa_fault *fault)
{
    unsigned char code[3]; unsigned i, width = 2; uint32_t v; size_t used;
    if (fault) memset(fault, 0, sizeof *fault);
    if (!state) return ES_EINVAL;
    for (i = 0; i < width; i++) {
        uint32_t addr = state->pc + i;
        if (!memory || !memory->read || memory->read(memory->ctx, addr, 1, &v) != ES_OK)
            return fail(fault, ESK_XT_FETCH, state->pc, addr, 0);
        code[i] = (unsigned char)(v & 255u);
        if (i == 0 && (code[0] & 15u) < 8) width = 3;
    }
    return esk_xtensa_execute(state, memory, code, width, &used, fault);
}
