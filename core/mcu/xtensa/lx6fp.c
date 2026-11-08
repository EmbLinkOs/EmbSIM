/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* lx6fp.c -- the ESP32's floating-point coprocessor (CP0): the Xtensa
 * Floating-Point Coprocessor Option, single precision, with the
 * division, reciprocal and square root instructions of core-isa.h
 * (XCHAL_HAVE_FP_DIV, _RECIP, _SQRT, _RSQRT).
 *
 * Sixteen 32-bit registers f0-f15; FCR's rounding mode (bits 1:0:
 * nearest even, toward zero, up, down) and FSR's sticky flags (bits 11:7:
 * inexact, underflow, overflow, divide by zero, invalid). Arithmetic is
 * IEEE 754: ADD.S, SUB.S, MUL.S, MADD.S and MSUB.S (fused, one rounding)
 * are rounded as FCR says, with their flags; FLOAT.S and UFLOAT.S scale
 * by 2^-imm, ROUND.S, TRUNC.S, FLOOR.S, CEIL.S and UTRUNC.S by 2^imm, out
 * of range or NaN giving the limits (0x7fffffff, 0x80000000, or for
 * UTRUNC.S 0xffffffff); the comparisons set a boolean register; the
 * moves (MOV.S, ABS.S, NEG.S, the conditional ones, RFR, WFR, CONST.S's
 * 0, 1, 2 and 0.5) change no flag. A NaN result is the first NaN operand
 * made quiet, or 0x7fc00000.
 *
 * Where it differs inside: the division and square root sequences
 * libgcc runs (DIV0.S, NEXP01.S, MADDN.S, MKDADJ.S, ADDEXP.S, ADDEXPM.S,
 * DIVN.S; SQRT0.S, MKSADJ.S, ...) give the quotient or root correctly
 * rounded, as the hardware's do, but the registers in between hold other
 * values: MKDADJ.S fr, fs computes fs / fr, MKSADJ.S the root, ADDEXPM.S
 * moves it to its destination, the others change nothing (Espressif's
 * QEMU does the same). RECIP0.S and RSQRT0.S give the exact reciprocal
 * and reciprocal root rounded, where the hardware gives a seed of fewer
 * bits. The arithmetic is the host's double precision with fma(), which
 * holds every single-precision result exactly or with its residual. */
#include <float.h>
#include <math.h>
#include <string.h>

#include "mcu/xtensa/lx6.h"

#define F_I 0x1u /* the flags, as FSR holds them from bit 7 */
#define F_U 0x2u
#define F_O 0x4u
#define F_Z 0x8u
#define F_V 0x10u
#define QNAN 0x7fc00000u

static float f_of(u32 v)
{
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static u32 bits_of(float f)
{
    u32 v;
    memcpy(&v, &f, 4);
    return v;
}

static int is_nan(u32 v)
{
    return (v & 0x7f800000u) == 0x7f800000u && (v & 0x7fffffu);
}

static int is_snan(u32 v)
{
    return is_nan(v) && !(v & 0x400000u);
}

static void flag(struct lx6 *c, u32 f)
{
    *esk_lx6_fsr(c) |= f << 7;
}

static u32 rmode(struct lx6 *c)
{
    return *esk_lx6_fcr(c) & 3u;
}

/* a NaN result of operands a and b (and c): the first NaN quieted */
static u32 nan_of(struct lx6 *c, u32 a, u32 b, u32 x, int n)
{
    if (is_snan(a) || is_snan(b) || (n > 2 && is_snan(x)))
        flag(c, F_V);
    if (is_nan(a))
        return a | 0x400000u;
    if (is_nan(b))
        return b | 0x400000u;
    if (n > 2 && is_nan(x))
        return x | 0x400000u;
    return QNAN;
}

/* the value hi + lo (lo below hi's last bit, only its sign and whether
 * it is 0 matter) rounded to single precision in mode `m` */
static u32 round_f(struct lx6 *c, double hi, double lo, u32 m)
{
    float f;
    double diff, res;
    int inexact;
    if (hi != hi)
        return QNAN;
    if (isinf(hi))
        return bits_of((float)hi);
    f = (float)hi;
    if (isinf(f)) { /* overflow */
        flag(c, F_O | F_I);
        if ((m == 1) || (m == 2 && hi < 0) || (m == 3 && hi > 0))
            return bits_of(hi < 0 ? -FLT_MAX : FLT_MAX);
        return bits_of(f);
    }
    diff = hi - (double)f;
    res = diff != 0 ? diff : lo;
    inexact = diff != 0 || lo != 0;
    if (m == 0) {
        /* a tie broken the wrong way by the host, lo deciding */
        if (diff != 0 && lo != 0 && (lo > 0) == (diff > 0)) {
            float g = nextafterf(f, diff > 0 ? INFINITY : -INFINITY);
            if (fabs((double)g - (double)f) == 2 * fabs(diff))
                f = g;
        }
    } else if (res != 0) {
        if (m == 1) { /* toward zero */
            if ((f > 0 && res < 0) || (f < 0 && res > 0))
                f = nextafterf(f, 0.0f);
        } else if (m == 2) { /* up */
            if (res > 0)
                f = nextafterf(f, INFINITY);
        } else if (res < 0) { /* down */
            f = nextafterf(f, -INFINITY);
        }
        if (f == 0 && hi != 0)
            f = hi < 0 ? -0.0f : 0.0f;
    }
    if (isinf(f)) {
        flag(c, F_O | F_I);
        return bits_of(f);
    }
    if (inexact) {
        flag(c, F_I);
        if (fabs((double)f) < FLT_MIN || (f == 0 && hi != 0))
            flag(c, F_U);
    }
    return bits_of(f);
}

static int is_inf(u32 v)
{
    return (v & 0x7fffffffu) == 0x7f800000u;
}

static int is_zero(u32 v)
{
    return !(v & 0x7fffffffu);
}

/* a + b * x fused (sub: a - b * x), or b + x (mul 0) / b * x (add 0) */
static u32 add_s(struct lx6 *c, u32 a, u32 b)
{
    double x = f_of(a), y = f_of(b), s, bb, lo;
    if (is_nan(a) || is_nan(b))
        return nan_of(c, a, b, 0, 2);
    if (is_inf(a) && is_inf(b) && ((a ^ b) & 0x80000000u)) {
        flag(c, F_V);
        return QNAN;
    }
    s = x + y;
    if (is_inf(a) || is_inf(b))
        return bits_of((float)s);
    if (s == 0 && x == -y && x != 0)
        return rmode(c) == 3 ? 0x80000000u : 0; /* an exact zero sum: +0 but rounding down */
    if (s == 0)
        return (a & b & 0x80000000u) | (rmode(c) == 3 ? (a | b) & 0x80000000u : 0u);
    bb = s - x;
    lo = (x - (s - bb)) + (y - bb); /* TwoSum: s + lo is x + y exactly */
    return round_f(c, s, lo, rmode(c));
}

static u32 mul_s(struct lx6 *c, u32 a, u32 b)
{
    if (is_nan(a) || is_nan(b))
        return nan_of(c, a, b, 0, 2);
    if ((is_inf(a) && is_zero(b)) || (is_zero(a) && is_inf(b))) {
        flag(c, F_V);
        return QNAN;
    }
    return round_f(c, (double)f_of(a) * (double)f_of(b), 0, rmode(c)); /* exact in double */
}

static u32 madd_s(struct lx6 *c, u32 acc, u32 a, u32 b, int sub)
{
    double p, x, s, bb, lo;
    u32 pb;
    if (is_nan(acc) || is_nan(a) || is_nan(b)) {
        if ((is_inf(a) && is_zero(b)) || (is_zero(a) && is_inf(b)))
            flag(c, F_V);
        return nan_of(c, a, b, acc, 3);
    }
    if ((is_inf(a) && is_zero(b)) || (is_zero(a) && is_inf(b))) {
        flag(c, F_V);
        return QNAN;
    }
    p = (double)f_of(a) * (double)f_of(b);
    if (sub)
        p = -p;
    x = f_of(acc);
    pb = (u32)(p < 0 || (p == 0 && signbit(p))) << 31;
    if ((is_inf(a) || is_inf(b)) && is_inf(acc) && ((pb ^ acc) & 0x80000000u)) {
        flag(c, F_V);
        return QNAN;
    }
    s = x + p;
    if (isinf(s) || is_inf(acc))
        return bits_of((float)s);
    if (s == 0) {
        if (x == 0 && p == 0)
            return (acc & pb) | (rmode(c) == 3 ? (acc | pb) & 0x80000000u : 0u);
        return rmode(c) == 3 ? 0x80000000u : 0;
    }
    bb = s - x;
    lo = (x - (s - bb)) + (p - bb);
    return round_f(c, s, lo, rmode(c));
}

static u32 div_s(struct lx6 *c, u32 a, u32 b)
{
    double q, r;
    if (is_nan(a) || is_nan(b))
        return nan_of(c, a, b, 0, 2);
    if ((is_zero(a) && is_zero(b)) || (is_inf(a) && is_inf(b))) {
        flag(c, F_V);
        return QNAN;
    }
    if (is_zero(b)) {
        if (!is_inf(a))
            flag(c, F_Z);
        return ((a ^ b) & 0x80000000u) | 0x7f800000u;
    }
    if (is_inf(a) || is_zero(a) || is_inf(b))
        return bits_of(f_of(a) / f_of(b));
    q = (double)f_of(a) / (double)f_of(b);
    r = fma(-q, (double)f_of(b), (double)f_of(a)); /* the exact remainder */
    return round_f(c, q, (f_of(b) < 0 ? -r : r), rmode(c));
}

static u32 sqrt_s(struct lx6 *c, u32 a)
{
    double x = f_of(a), r, e;
    if (is_nan(a))
        return nan_of(c, a, 0, 0, 1);
    if (is_zero(a))
        return a;
    if (a & 0x80000000u) {
        flag(c, F_V);
        return QNAN;
    }
    if (is_inf(a))
        return a;
    r = sqrt(x);
    e = fma(-r, r, x);
    return round_f(c, r, e, rmode(c));
}

/* to an integer: v * 2^scale rounded in mode m, signed or not */
static u32 to_int(struct lx6 *c, u32 v, u32 scale, u32 m, int uns)
{
    double x = ldexp((double)f_of(v), (int)scale), y;
    if (is_nan(v)) {
        flag(c, F_V);
        return uns ? 0xffffffffu : 0x7fffffffu;
    }
    y = m == 0 ? nearbyint(x) : m == 1 ? trunc(x) : m == 2 ? ceil(x) : floor(x);
    if (m == 0 && fabs(x - trunc(x)) == 0.5)
        y = 2.0 * nearbyint(x / 2.0); /* ties to even, whatever the host's mode */
    if (uns && !(v & 0x80000000u)) {
        if (y > 4294967295.0) {
            flag(c, F_V);
            return 0xffffffffu;
        }
        if (y != x)
            flag(c, F_I);
        return (u32)y;
    }
    if (y > 2147483647.0) {
        flag(c, F_V);
        return 0x7fffffffu;
    }
    if (y < -2147483648.0) {
        flag(c, F_V);
        return 0x80000000u;
    }
    if (uns && y != 0)
        flag(c, F_V); /* UTRUNC.S of a negative value */
    else if (y != x)
        flag(c, F_I);
    return (u32)(s32)y;
}

static u32 from_int(struct lx6 *c, u32 v, u32 scale, int uns)
{
    double x = uns ? (double)v : (double)(s32)v;
    return round_f(c, ldexp(x, -(int)scale), 0, rmode(c)); /* exact in double */
}

/* the comparisons: 1 UN, 2 OEQ, 3 UEQ, 4 OLT, 5 ULT, 6 OLE, 7 ULE */
static int compare(struct lx6 *c, u32 op, u32 a, u32 b)
{
    int un = is_nan(a) || is_nan(b);
    float x = f_of(a), y = f_of(b);
    if (is_snan(a) || is_snan(b) || (un && op >= 4 && (op & 1u) == 0))
        flag(c, F_V);
    switch (op) {
    case 1: return un;
    case 2: return !un && x == y;
    case 3: return un || x == y;
    case 4: return !un && x < y;
    case 5: return un || x < y;
    case 6: return !un && x <= y;
    case 7: return un || x <= y;
    }
    return 0;
}

int esk_lx6_fp(struct lx6 *c, u32 w)
{
    u32 op1 = (w >> 16) & 15u, op2 = (w >> 20) & 15u, r = (w >> 12) & 15u, s = (w >> 8) & 15u,
        t = (w >> 4) & 15u, *f = esk_lx6_fregs(c), a, v;
    if (op1 == 8) { /* LSX, SSX (LSXU, SSXU: not in this configuration) */
        if (op2 != 0 && op2 != 4)
            return -1;
        if (esk_lx6_win(c, s > t ? s : t))
            return 0;
        a = esk_lx6_ar(c, (int)s) + esk_lx6_ar(c, (int)t);
        if (op2 == 0) {
            if (esk_lx6_load(c, a, 4, &v) == 0)
                f[r] = v;
        } else {
            (void)esk_lx6_store(c, a, 4, f[r]);
        }
        return 0;
    }
    if (op1 == 10) { /* FP0 */
        switch (op2) {
        case 0: f[r] = add_s(c, f[s], f[t]); return 0;
        case 1: f[r] = add_s(c, f[s], f[t] ^ (is_nan(f[t]) ? 0u : 0x80000000u)); return 0;
        case 2: f[r] = mul_s(c, f[s], f[t]); return 0;
        case 4: f[r] = madd_s(c, f[r], f[s], f[t], 0); return 0;
        case 5: f[r] = madd_s(c, f[r], f[s], f[t], 1); return 0;
        case 6: case 7: return 0; /* MADDN.S DIVN.S: in MKDADJ.S's result */
        case 8: case 9: case 10: case 11: case 14: { /* ROUND TRUNC FLOOR CEIL UTRUNC */
            static const u32 mode[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 2, 0, 0, 1, 0 };
            if (esk_lx6_win(c, r))
                return 0;
            esk_lx6_set_ar(c, (int)r, to_int(c, f[s], t, mode[op2], op2 == 14));
            return 0;
        }
        case 12: case 13: /* FLOAT.S UFLOAT.S */
            if (esk_lx6_win(c, s))
                return 0;
            f[r] = from_int(c, esk_lx6_ar(c, (int)s), t, op2 == 13);
            return 0;
        case 15: /* FP1OP */
            switch (t) {
            case 0: f[r] = f[s]; return 0;                      /* MOV.S */
            case 1: f[r] = f[s] & 0x7fffffffu; return 0;        /* ABS.S */
            case 3: {                                           /* CONST.S */
                static const u32 k[4] = { 0x00000000u, 0x3f800000u, 0x40000000u, 0x3f000000u };
                f[r] = k[s & 3u];
                return 0;
            }
            case 4:                                             /* RFR */
                if (esk_lx6_win(c, r))
                    return 0;
                esk_lx6_set_ar(c, (int)r, f[s]);
                return 0;
            case 5:                                             /* WFR */
                if (esk_lx6_win(c, s))
                    return 0;
                f[r] = esk_lx6_ar(c, (int)s);
                return 0;
            case 6: f[r] = f[s] ^ 0x80000000u; return 0;        /* NEG.S */
            case 7: case 9: case 11: return 0;                  /* DIV0.S SQRT0.S NEXP01.S */
            case 8: f[r] = div_s(c, 0x3f800000u, f[s]); return 0; /* RECIP0.S */
            case 10:                                            /* RSQRT0.S */
                f[r] = div_s(c, 0x3f800000u, sqrt_s(c, f[s]));
                return 0;
            case 12: f[r] = sqrt_s(c, f[s]); return 0;          /* MKSADJ.S */
            case 13: f[r] = div_s(c, f[s], f[r]); return 0;     /* MKDADJ.S */
            case 14: return 0;                                  /* ADDEXP.S */
            case 15: f[r] = f[s]; return 0;                     /* ADDEXPM.S */
            }
            return -1;
        }
        return -1;
    }
    if (op1 == 11) { /* FP1 */
        if (op2 >= 1 && op2 <= 7) {
            u32 br = esk_lx6_br(c);
            int b = compare(c, op2, f[s], f[t]);
            esk_lx6_set_br(c, (br & ~(1u << r)) | (u32)b << r);
            return 0;
        }
        if (op2 >= 8 && op2 <= 11) { /* MOVEQZ.S MOVNEZ.S MOVLTZ.S MOVGEZ.S */
            int mv;
            if (esk_lx6_win(c, t))
                return 0;
            v = esk_lx6_ar(c, (int)t);
            mv = op2 == 8 ? v == 0 : op2 == 9 ? v != 0 : op2 == 10 ? (s32)v < 0 : (s32)v >= 0;
            if (mv)
                f[r] = f[s];
            return 0;
        }
        if (op2 == 12 || op2 == 13) { /* MOVF.S MOVT.S */
            if (((esk_lx6_br(c) >> t) & 1u) == (op2 == 13))
                f[r] = f[s];
            return 0;
        }
        return -1;
    }
    return -1;
}
