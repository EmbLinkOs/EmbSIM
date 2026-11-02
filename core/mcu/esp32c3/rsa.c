/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* rsa.c -- the ESP32-C3's RSA accelerator (TRM v1.4 chapter 20), as
 * ESP-IDF's mpi_ll and mbedtls port (esp_bignum.c, bignum_alt.c) drive it
 * for every big-number product and power: RSA, and the elliptic curves
 * of WPA3's SAE and TLS.
 *
 *   - Four memory blocks of 96 words, M (+0x000), Z (+0x200), Y (+0x400)
 *     and X (+0x600), little-endian words; MODE (+0x804) is the operands'
 *     length in words, minus 1 (N <= 96: 3072 bits); M_PRIME (+0x800) is
 *     M' = -M^-1 mod 2^32;
 *   - SET_START_MODMULT: Z = X * Y mod M, Z holding r = R^2 mod M (R =
 *     2^(32N)) on entry;
 *   - SET_START_MODEXP: Z = X ^ Y mod M, Z holding r; with SEARCH_ENABLE,
 *     from the exponent's bit SEARCH_POS down, otherwise from bit 32N - 1;
 *   - SET_START_MULT: Z = X * Y, X's N words and Y's in Z's upper half
 *     (words N to 2N - 1), MODE then the result's length 2N, minus 1;
 *   - QUERY_CLEAN reads 1 (the memories initialised: they are cleared at
 *     power-up and by the peripheral's reset), QUERY_IDLE reads 1;
 *   - an operation's end raises the interrupt (source 47, while INT_ENA;
 *     INT_CLR clears it).
 * The products and powers go through Montgomery multiplications with the
 * r and M' written (Z = Mont(Mont(X, r), Y) and the left-to-right ladder
 * of HAC 14.94 the hardware follows), so that a wrong r or M' gives the
 * wrong result it gives on the part.
 * Not yet: the time an operation takes (it ends at once, the same with
 * CONSTANT_TIME's mode), the memories' power-down (SYSTEM_RSA_PD_CTRL),
 * the digital signature and HMAC peripherals that share it. */
#include <string.h>

#include "mcu/esp32c3/esp32c3.h"

#define MEM_WORDS    96 /* 3072 bits */
#define BLOCK_STRIDE 0x200u
#define OFF_M_PRIME  0x800u
#define OFF_MODE     0x804u
#define OFF_CLEAN    0x808u
#define OFF_MODEXP   0x80Cu
#define OFF_MODMULT  0x810u
#define OFF_MULT     0x814u
#define OFF_IDLE     0x818u
#define OFF_INT_CLR  0x81Cu
#define OFF_SEARCH   0x824u
#define OFF_POS      0x828u
#define OFF_INT_ENA  0x82Cu
#define RSA_SOURCE   47 /* ETS_RSA_INTR_SOURCE */

enum { BLK_M, BLK_Z, BLK_Y, BLK_X };

struct c3_rsa {
    struct esk_mach *s;
    struct esk_rf_periph *p;
    u32 mem[4][BLOCK_STRIDE / 4u];
    int irq;
};

static u32 reg32(struct c3_rsa *k, u32 off) { return esk_rf_read(k->p, off, 4); }

static void irq(struct c3_rsa *k) { esk_esp32c3_source(k->s, RSA_SOURCE, k->irq && (reg32(k, OFF_INT_ENA) & 1u)); }

/* a >= b, both n words */
static int geq(const u32 *a, const u32 *b, int n)
{
    int i;
    for (i = n - 1; i >= 0; i--)
        if (a[i] != b[i])
            return a[i] > b[i];
    return 1;
}

/* out = a * b * R^-1 mod m (n words, at most 96), m' = -m^-1 mod 2^32: CIOS;
 * also the digital signature's (ds.c) */
void esk_esp32c3_mont(u32 *out, const u32 *a, const u32 *b, const u32 *m, u32 mp, int n)
{
    u32 t[MEM_WORDS + 2];
    int i, j;
    memset(t, 0, sizeof t);
    for (i = 0; i < n; i++) {
        u64 c = 0;
        u32 q;
        for (j = 0; j < n; j++) {
            c += (u64)t[j] + (u64)a[j] * b[i];
            t[j] = (u32)c;
            c >>= 32;
        }
        c += t[n];
        t[n] = (u32)c;
        t[n + 1] = (u32)(c >> 32);
        q = t[0] * mp;
        c = (u64)t[0] + (u64)q * m[0];
        c >>= 32;
        for (j = 1; j < n; j++) {
            c += (u64)t[j] + (u64)q * m[j];
            t[j - 1] = (u32)c;
            c >>= 32;
        }
        c += t[n];
        t[n - 1] = (u32)c;
        t[n] = t[n + 1] + (u32)(c >> 32);
        t[n + 1] = 0;
    }
    if (t[n] || geq(t, m, n)) { /* the final subtraction */
        u64 bw = 0;
        for (j = 0; j < n; j++) {
            u64 d = (u64)t[j] - m[j] - bw;
            t[j] = (u32)d;
            bw = (d >> 32) & 1u;
        }
    }
    memcpy(out, t, (size_t)n * 4u);
}

static void mont(u32 *out, const u32 *a, const u32 *b, const u32 *m, u32 mp, int n)
{
    esk_esp32c3_mont(out, a, b, m, mp, n);
}

static int length(struct c3_rsa *k) { return (int)(reg32(k, OFF_MODE) & 0x7Fu) + 1; }

static void modmult(struct c3_rsa *k)
{
    u32 t[MEM_WORDS], mp = reg32(k, OFF_M_PRIME);
    int n = length(k);
    if (n > MEM_WORDS)
        return;
    mont(t, k->mem[BLK_X], k->mem[BLK_Z], k->mem[BLK_M], mp, n);
    mont(k->mem[BLK_Z], t, k->mem[BLK_Y], k->mem[BLK_M], mp, n);
}

static void modexp(struct c3_rsa *k)
{
    u32 x[MEM_WORDS], z[MEM_WORDS], one[MEM_WORDS], mp = reg32(k, OFF_M_PRIME);
    const u32 *y = k->mem[BLK_Y], *m = k->mem[BLK_M];
    int n = length(k), i, top;
    if (n > MEM_WORDS)
        return;
    memset(one, 0, sizeof one);
    one[0] = 1;
    top = (reg32(k, OFF_SEARCH) & 1u) ? (int)(reg32(k, OFF_POS) & 0xFFFu) : 32 * n - 1;
    if (top > 32 * n - 1)
        top = 32 * n - 1;
    mont(x, k->mem[BLK_X], k->mem[BLK_Z], m, mp, n); /* X R mod M */
    mont(z, k->mem[BLK_Z], one, m, mp, n);           /* R mod M: 1 */
    for (i = top; i >= 0; i--) {
        mont(z, z, z, m, mp, n);
        if ((y[i / 32] >> (i % 32)) & 1u)
            mont(z, z, x, m, mp, n);
    }
    mont(k->mem[BLK_Z], z, one, m, mp, n);
}

static void mult(struct c3_rsa *k)
{
    u32 y[MEM_WORDS], z[2 * MEM_WORDS];
    int len = length(k), n = len / 2, i, j;
    if (n < 1 || n > MEM_WORDS / 2)
        return;
    memcpy(y, k->mem[BLK_Z] + n, (size_t)n * 4u);
    memset(z, 0, sizeof z);
    for (i = 0; i < n; i++) {
        u64 c = 0;
        for (j = 0; j < n; j++) {
            c += (u64)z[i + j] + (u64)k->mem[BLK_X][j] * y[i];
            z[i + j] = (u32)c;
            c >>= 32;
        }
        z[i + n] = (u32)c;
    }
    memcpy(k->mem[BLK_Z], z, (size_t)(2 * n) * 4u);
}

static u32 rsa_read(void *ctx, u32 off, int n)
{
    struct c3_rsa *k = (struct c3_rsa *)ctx;
    if (off < 4u * BLOCK_STRIDE) {
        u32 w = k->mem[off / BLOCK_STRIDE][(off % BLOCK_STRIDE) / 4u];
        (void)n;
        return w >> (8u * (off & 3u));
    }
    if ((off & ~3u) == OFF_CLEAN || (off & ~3u) == OFF_IDLE)
        return 1u;
    return esk_rf_read(k->p, off, n);
}

static void rsa_write(void *ctx, u32 off, int n, u32 v)
{
    struct c3_rsa *k = (struct c3_rsa *)ctx;
    if (off < 4u * BLOCK_STRIDE) {
        u32 *w = &k->mem[off / BLOCK_STRIDE][(off % BLOCK_STRIDE) / 4u];
        if (n == 4)
            *w = v;
        else {
            u32 sh = 8u * (off & 3u), mask = (n == 1 ? 0xFFu : 0xFFFFu) << sh;
            *w = (*w & ~mask) | ((v << sh) & mask);
        }
        return;
    }
    esk_rf_write(k->p, off, n, v);
    if ((off == OFF_MODEXP || off == OFF_MODMULT || off == OFF_MULT) && (v & 1u)) {
        if (off == OFF_MODEXP)
            modexp(k);
        else if (off == OFF_MODMULT)
            modmult(k);
        else
            mult(k);
        k->irq = 1;
        irq(k);
    } else if (off == OFF_INT_CLR && (v & 1u)) {
        k->irq = 0;
        irq(k);
    } else if (off == OFF_INT_ENA)
        irq(k);
}

static void rsa_reset(void *ctx)
{
    struct c3_rsa *k = (struct c3_rsa *)ctx;
    memset(k->mem, 0, sizeof k->mem);
    k->irq = 0;
}

const struct esk_dev_ops esk_esp32c3_rsa_ops = {
    "esp32c3-rsa", rsa_read, rsa_write, rsa_reset, 0, 0,
};

void *esk_esp32c3_rsa_create(struct esk_mach *s, struct esk_rf_periph *p)
{
    struct c3_rsa *k = (struct c3_rsa *)esk_mach_alloc(s, sizeof *k);
    if (!k)
        return 0;
    memset(k, 0, sizeof *k);
    k->s = s;
    k->p = p;
    return k;
}
