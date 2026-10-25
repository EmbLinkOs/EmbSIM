/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* cortexm-fpu.c -- the Cortex-M's floating-point unit: FPv4-SP (the
 * M4) and FPv5's double precision (the M7). Arithmetic follows the
 * architecture's NaN rules and FPSCR's DN and FZ bits, not the host's.
 * The registers are cs->S (cortexm.h).
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/cortexm-fpu.c. */
#include <math.h>
#include <string.h>

#include "mcu/cortexm/cortexm.h"

static int fp_enabled(struct esk_cm_state *cs)
{
    if (!cs->model->fpu)
        return 0;
    u32 a = esk_cm_privileged(cs) ? 1 : 2;
    return ((cs->cpacr >> 20) & 3) >= a && ((cs->cpacr >> 22) & 3) >= a;
}

static float f_of(u32 b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

static u32 b_of(float f)
{
    u32 b;
    memcpy(&b, &f, 4);
    return b;
}

static double d_of(u64 b)
{
    double d;
    memcpy(&d, &b, 8);
    return d;
}

static u64 bd_of(double d)
{
    u64 b;
    memcpy(&b, &d, 8);
    return b;
}

static u64 getd(struct esk_cm_state *cs, int n)
{
    return (u64)cs->S[2 * n + 1] << 32 | cs->S[2 * n];
}

static void setd(struct esk_cm_state *cs, int n, u64 v)
{
    cs->S[2 * n] = (u32)v;
    cs->S[2 * n + 1] = (u32)(v >> 32);
}

/* The architecture's NaN rules, which the host's arithmetic does not
 * promise: a signalling NaN operand wins over a quiet one, the first
 * operand over the second, and the result is the operand quietened (or
 * the default NaN with FPSCR.DN). */
static int nan_s(u32 a)
{
    return (a & 0x7f800000u) == 0x7f800000u && (a & 0x7fffffu);
}

static int snan_s(u32 a)
{
    return nan_s(a) && !(a & 0x400000u);
}

static int nan_d(u64 a)
{
    return (a & 0x7ff0000000000000ull) == 0x7ff0000000000000ull &&
           (a & 0xfffffffffffffull);
}

static int snan_d(u64 a)
{
    return nan_d(a) && !(a & 0x8000000000000ull);
}

static u32 qnan_s(struct esk_cm_state *cs, u32 a)
{
    return (cs->fpscr & (1u << 25)) ? 0x7fc00000u : a | 0x400000u;
}

static u64 qnan_d(struct esk_cm_state *cs, u64 a)
{
    return (cs->fpscr & (1u << 25)) ? 0x7ff8000000000000ull
                                : a | 0x8000000000000ull;
}

static int pick_nan_s(struct esk_cm_state *cs, const u32 *ops, int n, u32 *r)
{
    for (int i = 0; i < n; i++)
        if (snan_s(ops[i])) {
            *r = qnan_s(cs, ops[i]);
            return 1;
        }
    for (int i = 0; i < n; i++)
        if (nan_s(ops[i])) {
            *r = qnan_s(cs, ops[i]);
            return 1;
        }
    return 0;
}

static int pick_nan_d(struct esk_cm_state *cs, const u64 *ops, int n, u64 *r)
{
    for (int i = 0; i < n; i++)
        if (snan_d(ops[i])) {
            *r = qnan_d(cs, ops[i]);
            return 1;
        }
    for (int i = 0; i < n; i++)
        if (nan_d(ops[i])) {
            *r = qnan_d(cs, ops[i]);
            return 1;
        }
    return 0;
}

/* FPSCR.FZ: denormal inputs and outputs are zero */
static u32 fz_s(struct esk_cm_state *cs, u32 a)
{
    if ((cs->fpscr & (1u << 24)) && (a & 0x7f800000u) == 0 && (a & 0x7fffffu))
        return a & 0x80000000u;
    return a;
}

static u64 fz_d(struct esk_cm_state *cs, u64 a)
{
    if ((cs->fpscr & (1u << 24)) && (a & 0x7ff0000000000000ull) == 0 &&
        (a & 0xfffffffffffffull))
        return a & 0x8000000000000000ull;
    return a;
}

/* a result that is NaN from valid operands is the default NaN */
static u32 res_s(struct esk_cm_state *cs, float f)
{
    u32 r = b_of(f);
    if (nan_s(r))
        return 0x7fc00000u;
    return fz_s(cs, r);
}

static u64 res_d(struct esk_cm_state *cs, double d)
{
    u64 r = bd_of(d);
    if (nan_d(r))
        return 0x7ff8000000000000ull;
    return fz_d(cs, r);
}

enum { FOP_ADD, FOP_SUB, FOP_MUL, FOP_DIV, FOP_FMA };

static u32 fop_s(struct esk_cm_state *cs, int op, u32 a, u32 b, u32 c)
{
    u32 ops[3], r;
    a = fz_s(cs, a);
    b = fz_s(cs, b);
    c = fz_s(cs, c);
    if (op == FOP_FMA) {
        ops[0] = c;             /* the addend first: FPMulAdd's order */
        ops[1] = a;
        ops[2] = b;
        if (pick_nan_s(cs, ops, 3, &r))
            return r;
        return res_s(cs, fmaf(f_of(a), f_of(b), f_of(c)));
    }
    ops[0] = a;
    ops[1] = b;
    if (pick_nan_s(cs, ops, 2, &r))
        return r;
    float x = f_of(a), y = f_of(b);
    switch (op) {
    case FOP_ADD: return res_s(cs, x + y);
    case FOP_SUB: return res_s(cs, x - y);
    case FOP_MUL: return res_s(cs, x * y);
    default: return res_s(cs, x / y);
    }
}

static u64 fop_d(struct esk_cm_state *cs, int op, u64 a, u64 b, u64 c)
{
    u64 ops[3], r;
    a = fz_d(cs, a);
    b = fz_d(cs, b);
    c = fz_d(cs, c);
    if (op == FOP_FMA) {
        ops[0] = c;
        ops[1] = a;
        ops[2] = b;
        if (pick_nan_d(cs, ops, 3, &r))
            return r;
        return res_d(cs, fma(d_of(a), d_of(b), d_of(c)));
    }
    ops[0] = a;
    ops[1] = b;
    if (pick_nan_d(cs, ops, 2, &r))
        return r;
    double x = d_of(a), y = d_of(b);
    switch (op) {
    case FOP_ADD: return res_d(cs, x + y);
    case FOP_SUB: return res_d(cs, x - y);
    case FOP_MUL: return res_d(cs, x * y);
    default: return res_d(cs, x / y);
    }
}

static void fcmp_flags(struct esk_cm_state *cs, int unord, int eq, int lt)
{
    u32 f = unord ? 0x3u : eq ? 0x6u : lt ? 0x8u : 0x2u;
    cs->fpscr = (cs->fpscr & 0x0fffffffu) | f << 28;
}

/* round a double to an integer: toward zero, or as FPSCR.RMode says */
static double round_by(double x, int mode)
{
    double t = floor(x), d = x - t;
    switch (mode) {
    case 0:                             /* nearest, ties to even */
        if (d > 0.5 || (d == 0.5 && fmod(t, 2.0) != 0.0))
            t += 1.0;
        return t;
    case 1: return ceil(x);             /* toward +inf */
    case 2: return floor(x);            /* toward -inf */
    default: return x < 0 ? ceil(x) : floor(x);
    }
}

/* VCVT to a 32-bit integer: NaN is 0, out of range saturates */
static u32 to_int(double x, int is_signed, int mode)
{
    if (x != x)
        return 0;
    double r = round_by(x, mode);
    if (is_signed) {
        if (r >= 2147483648.0)
            return 0x7fffffffu;
        if (r < -2147483648.0)
            return 0x80000000u;
        return (u32)(s32)r;
    }
    if (r >= 4294967296.0)
        return 0xffffffffu;
    if (r < 0)
        return 0;
    return (u32)r;
}

static u32 vfp_imm_s(u32 imm8)
{
    return (imm8 & 0x80) << 24 | ((imm8 & 0x40) ? 0x3E000000u : 0x40000000u) |
           (imm8 & 0x3f) << 19;
}

static u64 vfp_imm_d(u32 imm8)
{
    u32 hi = (imm8 & 0x80) << 24 |
             ((imm8 & 0x40) ? 0x3FC00000u : 0x40000000u) | (imm8 & 0x3f) << 16;
    return (u64)hi << 32;
}

/* PreserveFPState: the floating-point registers an exception frame
 * reserved room for, written there now, with the privilege and the
 * readiness to take a fault recorded when the frame was stacked. A fault
 * of the MemManage or BusFault that could not have been taken then
 * escalates to HardFault. */
static int fp_preserve(struct esk_cm_state *cs)
{
    u32 a = cs->fpcar;
    cs->access = ACC_LAZYFP;
    cs->force_unpriv = (cs->fpccr >> 1) & 1;    /* USER */
    for (int i = 0; i < 16; i++)
        esk_cm_st(cs, a + 4 * (u32)i, 4, cs->S[i]);
    esk_cm_st(cs, a + 64, 4, cs->fpscr);
    cs->access = ACC_NORMAL;
    cs->force_unpriv = 0;
    if (cs->fault_exc) {
        int ready = cs->fault_exc == EXC_MEM ? (int)(cs->fpccr >> 5 & 1)
                  : cs->fault_exc == EXC_BUS ? (int)(cs->fpccr >> 6 & 1) : 1;
        if (!ready) {
            cs->cfsr |= cs->fault_bits;
            cs->hfsr |= 1u << 30;               /* FORCED */
            cs->fault_exc = EXC_HARD;
            cs->fault_bits = 0;
        }
        return 0;
    }
    cs->fpccr &= ~1u;                           /* LSPACT */
    return 1;
}

/* ExecuteFPCheck, before each floating-point instruction: the coprocessor
 * enabled for this privilege, the state an exception left to preserve
 * lazily written first, and a new floating-point context begun with
 * FPDSCR's defaults when ASPEN tracks it */
int esk_cm_fp_begin(struct esk_cm_state *cs)
{
    if (!fp_enabled(cs)) {
        esk_cm_fault_raise(cs, EXC_USAGE, UFSR_NOCP);
        return 0;
    }
    if ((cs->fpccr & 1u) && !fp_preserve(cs))   /* LSPACT */
        return 0;
    if ((cs->fpccr & 0x80000000u) && !(cs->control & 4)) {   /* ASPEN, no FPCA */
        cs->fpscr = (cs->fpscr & ~0x07C00000u) | (cs->fpdscr & 0x07C00000u);
        cs->control |= 4;
    }
    return 1;
}

/* Extension register load and store, and the 64-bit moves. */
void esk_cm_vfp_ldst(struct esk_cm_state *cs, u32 h1, u32 h2)
{
    int P = (int)(h1 >> 8 & 1), U = (int)(h1 >> 7 & 1);
    int D = (int)(h1 >> 6 & 1), W = (int)(h1 >> 5 & 1);
    int L = (int)(h1 >> 4 & 1), rn = (int)(h1 & 15);
    u32 vd = h2 >> 12 & 15, imm8 = h2 & 0xff;
    int dbl = (int)(h2 >> 8 & 1);
    if (!esk_cm_fp_begin(cs))
        return;
    if (!P && !U && D && !W) {          /* VMOV two core registers */
        int rt = (int)(h2 >> 12 & 15), rt2 = rn;
        u32 m = (h2 & 15), M = h2 >> 5 & 1;
        if (dbl) {
            int dm = (int)(M << 4 | m);
            if (dm >= 16 && cs->model->fpu != FPU_DP) {
                esk_cm_undef(cs);
                return;
            }
            if (L) {
                esk_cm_set_reg(cs, rt, cs->S[2 * dm]);
                esk_cm_set_reg(cs, rt2, cs->S[2 * dm + 1]);
            } else {
                cs->S[2 * dm] = cs->R[rt];
                cs->S[2 * dm + 1] = cs->R[rt2];
            }
        } else {
            int sm = (int)(m << 1 | M);
            if (L) {
                esk_cm_set_reg(cs, rt, cs->S[sm]);
                esk_cm_set_reg(cs, rt2, cs->S[(sm + 1) & 31]);
            } else {
                cs->S[sm] = cs->R[rt];
                cs->S[(sm + 1) & 31] = cs->R[rt2];
            }
        }
        return;
    }
    if (!P && !U) {
        esk_cm_undef(cs);
        return;
    }
    u32 base = rn == 15 ? (cs->pc + 4) & ~3u : cs->R[rn];
    if (P && !W) {                      /* VLDR, VSTR */
        u32 a = U ? base + imm8 * 4 : base - imm8 * 4;
        if (dbl) {
            int d = D << 4 | (int)vd;
            if (L) {
                u32 lo = esk_cm_mem_ld(cs, a, 4, 1), hi = esk_cm_mem_ld(cs, a + 4, 4, 1);
                cs->S[2 * d] = lo;
                cs->S[2 * d + 1] = hi;
            } else {
                esk_cm_mem_st(cs, a, 4, cs->S[2 * d], 1);
                esk_cm_mem_st(cs, a + 4, 4, cs->S[2 * d + 1], 1);
            }
        } else {
            int d = (int)(vd << 1) | D;
            if (L)
                cs->S[d] = esk_cm_mem_ld(cs, a, 4, 1);
            else
                esk_cm_mem_st(cs, a, 4, cs->S[d], 1);
        }
        return;
    }
    if (P == U && W) {                  /* PU=11 with W: undefined */
        esk_cm_undef(cs);
        return;
    }
    /* VLDM, VSTM, VPUSH, VPOP */
    u32 words = dbl ? (imm8 & ~1u) : imm8;
    u32 a = U ? base : base - imm8 * 4;
    int first = dbl ? (D << 4 | (int)vd) * 2 : (int)(vd << 1) | D;
    for (u32 i = 0; i < words; i++) {
        int s = first + (int)i;
        if (s >= 64)
            break;
        if (L)
            cs->S[s] = esk_cm_mem_ld(cs, a + 4 * i, 4, 1);
        else
            esk_cm_mem_st(cs, a + 4 * i, 4, cs->S[s], 1);
    }
    if (W && !cs->fault_exc)
        esk_cm_set_reg(cs, rn, U ? base + imm8 * 4 : base - imm8 * 4);
}

/* 8, 16 and 32-bit transfers between the core and the FPU */
void esk_cm_vfp_xfer(struct esk_cm_state *cs, u32 h1, u32 h2)
{
    u32 A = h1 >> 5 & 7, L = h1 >> 4 & 1, Cb = h2 >> 8 & 1;
    int rt = (int)(h2 >> 12 & 15);
    if (!esk_cm_fp_begin(cs))
        return;
    if (!Cb && A == 0) {                /* VMOV core <-> single */
        int n = (int)((h1 & 15) << 1 | (h2 >> 7 & 1));
        if (L)
            esk_cm_set_reg(cs, rt, cs->S[n]);
        else
            cs->S[n] = cs->R[rt];
        return;
    }
    if (!Cb && A == 7) {                /* VMRS, VMSR */
        if ((h1 & 15) != 1) {
            esk_cm_undef(cs);
            return;
        }
        if (L) {
            if (rt == 15) {
                cs->N = (int)(cs->fpscr >> 31);
                cs->Z = (int)(cs->fpscr >> 30 & 1);
                cs->C = (int)(cs->fpscr >> 29 & 1);
                cs->V = (int)(cs->fpscr >> 28 & 1);
            } else
                esk_cm_set_reg(cs, rt, cs->fpscr);
        } else
            cs->fpscr = cs->R[rt] & 0xF7C0009Fu;
        return;
    }
    if (Cb && (h1 & 0xc0) == 0 && (h2 >> 5 & 3) == 0) {
        /* VMOV between a core register and one half of a D register:
         * the register is D:Vd (or N:Vn), D in the second halfword */
        int d = (int)((h2 >> 7 & 1) << 4 | (h1 & 15));
        int x = (int)(h1 >> 5 & 1);
        if (d >= 16) {
            esk_cm_undef(cs);
            return;
        }
        if (L)
            esk_cm_set_reg(cs, rt, cs->S[2 * d + x]);
        else
            cs->S[2 * d + x] = cs->R[rt];
        return;
    }
    esk_cm_undef(cs);
}

void esk_cm_vfp_dp(struct esk_cm_state *cs, u32 h1, u32 h2)
{
    u32 opc1 = (h1 >> 4 & 0xb);
    int D = (int)(h1 >> 6 & 1), Nb = (int)(h2 >> 7 & 1);
    int M = (int)(h2 >> 5 & 1), op = (int)(h2 >> 6 & 1);
    u32 vn = h1 & 15, vd = h2 >> 12 & 15, vm = h2 & 15;
    int dbl = (int)(h2 >> 8 & 1);
    if (!esk_cm_fp_begin(cs))
        return;
    if (dbl && cs->model->fpu != FPU_DP) {
        esk_cm_undef(cs);
        return;
    }
    int d = dbl ? D << 4 | (int)vd : (int)(vd << 1) | D;
    int n = dbl ? Nb << 4 | (int)vn : (int)(vn << 1) | Nb;
    int m = dbl ? M << 4 | (int)vm : (int)(vm << 1) | M;
    if (opc1 != 0xb) {
        if (dbl && (d >= 16 || n >= 16 || m >= 16)) {
            esk_cm_undef(cs);
            return;
        }
        /* the arithmetic: dest, n and m */
        if (dbl) {
            u64 a = getd(cs, n), b = getd(cs, m), acc = getd(cs, d), r;
            switch (opc1) {
            case 0x0: case 0x1: {           /* VMLA VMLS, VNMLS VNMLA */
                u64 p = fop_d(cs, FOP_MUL, a, b, 0);
                if (op)
                    p ^= 0x8000000000000000ull;
                if (opc1 == 1)
                    acc ^= 0x8000000000000000ull;
                r = fop_d(cs, FOP_ADD, acc, p, 0);
                break;
            }
            case 0x2:                       /* VMUL, VNMUL */
                r = fop_d(cs, FOP_MUL, a, b, 0);
                if (op)
                    r ^= 0x8000000000000000ull;
                break;
            case 0x3:
                r = fop_d(cs, op ? FOP_SUB : FOP_ADD, a, b, 0);
                break;
            case 0x8:
                if (op) { esk_cm_undef(cs); return; }
                r = fop_d(cs, FOP_DIV, a, b, 0);
                break;
            case 0x9: case 0xa:             /* VFNMA VFNMS, VFMA VFMS */
                if (op)
                    a ^= 0x8000000000000000ull;
                if (opc1 == 9)
                    acc ^= 0x8000000000000000ull;
                r = fop_d(cs, FOP_FMA, a, b, acc);
                break;
            default:
                esk_cm_undef(cs);
                return;
            }
            setd(cs, d, r);
        } else {
            u32 a = cs->S[n], b = cs->S[m], acc = cs->S[d], r;
            switch (opc1) {
            case 0x0: case 0x1: {
                u32 p = fop_s(cs, FOP_MUL, a, b, 0);
                if (op)
                    p ^= 0x80000000u;
                if (opc1 == 1)
                    acc ^= 0x80000000u;
                r = fop_s(cs, FOP_ADD, acc, p, 0);
                break;
            }
            case 0x2:
                r = fop_s(cs, FOP_MUL, a, b, 0);
                if (op)
                    r ^= 0x80000000u;
                break;
            case 0x3:
                r = fop_s(cs, op ? FOP_SUB : FOP_ADD, a, b, 0);
                break;
            case 0x8:
                if (op) { esk_cm_undef(cs); return; }
                r = fop_s(cs, FOP_DIV, a, b, 0);
                break;
            case 0x9: case 0xa:
                if (op)
                    a ^= 0x80000000u;
                if (opc1 == 9)
                    acc ^= 0x80000000u;
                r = fop_s(cs, FOP_FMA, a, b, acc);
                break;
            default:
                esk_cm_undef(cs);
                return;
            }
            cs->S[d] = r;
        }
        return;
    }
    /* the other operations: opc2 is the Vn field */
    u32 opc2 = vn;
    int opc3 = (int)(h2 >> 6 & 3);
    /* which operands are double: the destination, except where a
     * conversion writes a single; the source, except where it reads one */
    int dd = dbl && opc2 != 0x7 && opc2 != 0xc && opc2 != 0xd;
    int dm = dbl && opc2 != 0x8;
    if ((dd && d >= 16) || (dm && m >= 16)) {
        esk_cm_undef(cs);
        return;
    }
    if (!(opc3 & 1)) {                      /* VMOV immediate */
        u32 imm8 = (h1 & 15) << 4 | (h2 & 15);
        if (dbl)
            setd(cs, d, vfp_imm_d(imm8));
        else
            cs->S[d] = vfp_imm_s(imm8);
        return;
    }
    switch (opc2) {
    case 0x0:                               /* VMOV reg, VABS */
        if (dbl)
            setd(cs, d, opc3 == 1 ? getd(cs, m) : getd(cs, m) & ~0x8000000000000000ull);
        else
            cs->S[d] = opc3 == 1 ? cs->S[m] : cs->S[m] & ~0x80000000u;
        return;
    case 0x1:                               /* VNEG, VSQRT */
        if (opc3 == 1) {
            if (dbl)
                setd(cs, d, getd(cs, m) ^ 0x8000000000000000ull);
            else
                cs->S[d] = cs->S[m] ^ 0x80000000u;
            return;
        }
        if (dbl) {
            u64 a = fz_d(cs, getd(cs, m)), r;
            if (pick_nan_d(cs, &a, 1, &r))
                setd(cs, d, r);
            else
                setd(cs, d, res_d(cs, sqrt(d_of(a))));
        } else {
            u32 a = fz_s(cs, cs->S[m]), r;
            if (pick_nan_s(cs, &a, 1, &r))
                cs->S[d] = r;
            else
                cs->S[d] = res_s(cs, sqrtf(f_of(a)));
        }
        return;
    case 0x4: case 0x5: {                   /* VCMP, VCMPE */
        if (dbl) {
            u64 a = fz_d(cs, getd(cs, d)), b = opc2 == 5 ? 0 : fz_d(cs, getd(cs, m));
            double x = d_of(a), y = d_of(b);
            fcmp_flags(cs, x != x || y != y, x == y, x < y);
        } else {
            u32 a = fz_s(cs, cs->S[d]), b = opc2 == 5 ? 0 : fz_s(cs, cs->S[m]);
            float x = f_of(a), y = f_of(b);
            fcmp_flags(cs, x != x || y != y, x == y, x < y);
        }
        return;
    }
    case 0x7:                               /* VCVT between precisions */
        if (opc3 != 3 || cs->model->fpu != FPU_DP) {
            esk_cm_undef(cs);
            return;
        }
        if (dbl) {                          /* double -> single */
            int sd = (int)(vd << 1) | D;
            (void)dd;
            u64 a = fz_d(cs, getd(cs, m)), r;
            if (pick_nan_d(cs, &a, 1, &r)) {
                u32 hi = (u32)(r >> 32);
                cs->S[sd] = (cs->fpscr & (1u << 25)) ? 0x7fc00000u
                        : (hi & 0x80000000u) | 0x7fc00000u |
                          (u32)(r >> 29 & 0x3fffff);
            } else
                cs->S[sd] = res_s(cs, (float)d_of(a));
        } else {                            /* single -> double */
            int dd2 = D << 4 | (int)vd;
            int sm = (int)(vm << 1) | M;
            if (dd2 >= 16) {
                esk_cm_undef(cs);
                return;
            }
            u32 a = fz_s(cs, cs->S[sm]);
            if (nan_s(a)) {
                u32 q = qnan_s(cs, a);
                setd(cs, dd2, (cs->fpscr & (1u << 25)) ? 0x7ff8000000000000ull
                         : (u64)(q & 0x80000000u) << 32 |
                           0x7ff8000000000000ull |
                           (u64)(q & 0x3fffffu) << 29);
            } else
                setd(cs, dd2, res_d(cs, (double)f_of(a)));
        }
        return;
    case 0x8: {                             /* VCVT integer -> float */
        int sm = (int)(vm << 1) | M;
        int is_signed = opc3 >> 1;
        double x = is_signed ? (double)(s32)cs->S[sm] : (double)cs->S[sm];
        if (dbl)
            setd(cs, d, res_d(cs, x));
        else
            cs->S[d] = res_s(cs, (float)x);
        return;
    }
    case 0xc: case 0xd: {                   /* VCVT, VCVTR float -> int */
        int sd = (int)(vd << 1) | D;
        int mode = (opc3 & 2) ? 3 : (int)(cs->fpscr >> 22 & 3);
        double x = dbl ? d_of(fz_d(cs, getd(cs, m))) : (double)f_of(fz_s(cs, cs->S[m]));
        cs->S[sd] = to_int(x, opc2 == 0xd, mode);
        return;
    }
    }
    esk_cm_undef(cs);
}
