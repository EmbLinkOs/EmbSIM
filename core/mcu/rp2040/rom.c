/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* rom.c -- the RP2040's bootrom functions, done by the host (its
 * datasheet, 2.8.3: the bootrom contents).
 *
 * The bootrom's code is not shipped (its float library may only be used on
 * an RP2040), so EmbSIM's ROM holds what a program finds through the
 * documented API instead: the header at 0x10 (magic, version 3: B2), the
 * function and data tables at the half-words 0x14 and 0x16, and
 * rom_table_lookup at 0x18. Each function is `bkpt #n` then a return
 * (mach.h's ROM host functions): the core's bkpt calls rom_call, which
 * does the function here with the core's registers and memory.
 *
 *   - rom_table_lookup(table, code): the table's {code, address} pairs
 *     read from the ROM, as the bootrom reads them;
 *   - popcount32, reverse32, clz32, ctz32 (clz32 and ctz32 of 0 are 32);
 *   - memset, memset4, memcpy, memcpy44, through the core's bus;
 *   - connect_internal_flash, flash_exit_xip, flash_flush_cache,
 *     flash_enter_cmd_xip: the XIP stays as it is, so they do nothing;
 *     flash_range_erase and flash_range_program change the flash's bytes
 *     (programming clears bits, as NOR flash does) and take the W25Q16JV's
 *     typical times: 150 ms for each aligned whole block the block command
 *     erases (the SDK's is 64 KiB, 0xd8), 45 ms for each 4 KiB sector of
 *     the rest, 0.4 ms a page;
 *   - reset_to_usb_boot: the part goes to its USB boot and waits there
 *     (mcu.usb_boot), as with no boot2;
 *   - the soft float and double tables ('SF', 'SD'), version 3's 32
 *     entries each: add, subtract, multiply, divide and square root are
 *     IEEE 754 correctly rounded (to nearest, even) as the bootrom's are;
 *     like the bootrom's, denormal inputs and results are zero and an
 *     infinite or NaN input is an infinity, with no NaN out (a NaN result
 *     is +infinity, the square root of a negative number -infinity);
 *     conversions to integers round towards minus infinity and saturate,
 *     from integers to nearest; cos, sin, tan, atan2, exp and ln are the
 *     host's, rounded once to the result's precision, so they may differ
 *     from the bootrom's in the last place.
 *
 * Timing (estimated, ADR-015): each function takes the cycles below, from
 * the bootrom's published timings where it has them (the arithmetic and
 * conversions), else of their order. */
#include <math.h>
#include <string.h>

#include "mcu/rp2040/rp2040.h"

#define ROM_SIZE 0x4000u
#define FLASH_BASE 0x10000000u
#define FLASH_SIZE 0x200000u

/* the ROM's layout */
#define AT_FUNCS 0x100u /* the function table: {code, address} half-words */
#define AT_DATA 0x200u  /* the data table */
#define AT_TEXT 0x300u  /* the copyright string, the git revision */
#define AT_SF 0x404u    /* the float table (its size in words at -2) */
#define AT_SD 0x504u    /* the double table */
#define AT_STUBS 0x600u /* the functions: `bkpt #n; bx lr`, 4 bytes each */

#define TABLE_WORDS 32 /* SF_TABLE_V2_SIZE / 4 */

/* the host functions' numbers (bkpt's): 0 the lookup, then the named
 * functions, then the float table's entries and the double table's */
enum {
    F_LOOKUP,
    F_POPCOUNT,
    F_REVERSE,
    F_CLZ,
    F_CTZ,
    F_MEMSET,
    F_MEMSET4,
    F_MEMCPY,
    F_MEMCPY44,
    F_CONNECT_FLASH,
    F_EXIT_XIP,
    F_ERASE,
    F_PROGRAM,
    F_FLUSH_CACHE,
    F_ENTER_XIP,
    F_USB_BOOT,
    F_COUNT,
    F_SF = 0x20, /* + the float table's index */
    F_SD = 0x40, /* + the double table's */
    F_END = 0x60
};

static const struct {
    char c1, c2;
    int n;
} funcs[] = {
    { 'P', '3', F_POPCOUNT }, { 'R', '3', F_REVERSE },        { 'L', '3', F_CLZ },        { 'T', '3', F_CTZ },
    { 'M', 'S', F_MEMSET },   { 'S', '4', F_MEMSET4 },        { 'M', 'C', F_MEMCPY },     { 'C', '4', F_MEMCPY44 },
    { 'I', 'F', F_CONNECT_FLASH }, { 'E', 'X', F_EXIT_XIP },  { 'R', 'E', F_ERASE },      { 'R', 'P', F_PROGRAM },
    { 'F', 'C', F_FLUSH_CACHE },   { 'C', 'X', F_ENTER_XIP }, { 'U', 'B', F_USB_BOOT },
};

static const char copyright[] = "EmbSIM's RP2040 bootrom functions (Apache-2.0)";

static u32 stub(int n) { return AT_STUBS + 4u * (u32)n; }

static void put16(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static void put32(u8 *p, u32 v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}

void esk_rp2040_rom(u8 *rom, u32 size)
{
    u32 at;
    size_t i;
    int n;
    if (size < ROM_SIZE)
        return;
    put16(rom + 0x14, AT_FUNCS);
    put16(rom + 0x16, AT_DATA);
    put16(rom + 0x18, stub(F_LOOKUP) | 1u);
    put16(rom + 0x1a, 0);
    at = AT_FUNCS;
    for (i = 0; i < sizeof funcs / sizeof funcs[0]; i++, at += 4) {
        put16(rom + at, (u32)(u8)funcs[i].c1 | (u32)(u8)funcs[i].c2 << 8);
        put16(rom + at + 2, stub(funcs[i].n) | 1u);
    }
    put16(rom + at, 0);
    at = AT_DATA;
    put16(rom + at, 'S' | 'F' << 8);
    put16(rom + at + 2, AT_SF);
    put16(rom + at + 4, 'S' | 'D' << 8);
    put16(rom + at + 6, AT_SD);
    put16(rom + at + 8, 'C' | 'R' << 8);
    put16(rom + at + 10, AT_TEXT);
    put16(rom + at + 12, 'G' | 'R' << 8);
    put16(rom + at + 14, AT_TEXT + 0x80u);
    put16(rom + at + 16, 0);
    memcpy(rom + AT_TEXT, copyright, sizeof copyright);
    put32(rom + AT_TEXT + 0x80u, 0); /* the git revision */
    rom[AT_SF - 2] = TABLE_WORDS;
    rom[AT_SD - 2] = TABLE_WORDS;
    for (n = 0; n < TABLE_WORDS; n++) {
        put32(rom + AT_SF + 4u * (u32)n, stub(F_SF + n) | 1u);
        put32(rom + AT_SD + 4u * (u32)n, stub(F_SD + n) | 1u);
    }
    for (n = 0; n < F_END; n++) {
        u8 *p = rom + stub(n);
        put16(p, 0xbe00u | (u32)n); /* bkpt #n */
        put16(p + 2, 0x4770u);      /* bx lr */
    }
    /* reset_to_usb_boot does not return: cpsid i; b . */
    put16(rom + stub(F_USB_BOOT) + 2, 0xb672u);
    put16(rom + stub(F_COUNT), 0xe7feu);
}

/* ---- float and double, as the bootrom's are -------------------------------- */

static float f_in(u32 v)
{
    float f;
    u32 e = v >> 23 & 0xffu;
    if (e == 0)
        v &= 0x80000000u; /* a denormal is zero */
    else if (e == 0xffu)
        v &= 0xff800000u; /* a NaN is an infinity */
    memcpy(&f, &v, 4);
    return f;
}

static u32 f_out(float f)
{
    u32 v;
    memcpy(&v, &f, 4);
    if ((v >> 23 & 0xffu) == 0)
        v &= 0x80000000u;
    else if ((v & 0x7fffffffu) > 0x7f800000u)
        v = 0x7f800000u; /* no NaN out */
    return v;
}

static double d_in(u32 lo, u32 hi)
{
    double d;
    u64 v = (u64)hi << 32 | lo;
    u32 e = hi >> 20 & 0x7ffu;
    if (e == 0)
        v &= UINT64_C(0x8000000000000000);
    else if (e == 0x7ffu)
        v &= UINT64_C(0xfff0000000000000);
    memcpy(&d, &v, 8);
    return d;
}

static void d_out(double d, u32 *lo, u32 *hi)
{
    u64 v;
    memcpy(&v, &d, 8);
    if ((v >> 52 & 0x7ffu) == 0)
        v &= UINT64_C(0x8000000000000000);
    else if ((v & UINT64_C(0x7fffffffffffffff)) > UINT64_C(0x7ff0000000000000))
        v = UINT64_C(0x7ff0000000000000);
    *lo = (u32)v;
    *hi = (u32)(v >> 32);
}

static int cmp3(double a, double b) { return a < b ? -1 : a > b ? 1 : 0; }

/* flags as `cmp` leaves them for a signed comparison of a and b */
static u32 cmp_flags(int c) { return c < 0 ? 8u : c == 0 ? 6u : 2u; }

static u32 sat32(double d)
{
    d = floor(d);
    if (d <= -2147483648.0)
        return 0x80000000u;
    if (d >= 2147483647.0)
        return 0x7fffffffu;
    return (u32)(s32)d;
}

static u32 satu32(double d)
{
    d = floor(d);
    if (d <= 0)
        return 0;
    if (d >= 4294967295.0)
        return 0xffffffffu;
    return (u32)d;
}

static u64 sat64(double d)
{
    d = floor(d);
    if (d <= -9223372036854775808.0)
        return UINT64_C(0x8000000000000000);
    if (d >= 9223372036854775808.0)
        return UINT64_C(0x7fffffffffffffff);
    return (u64)(int64_t)d;
}

static u64 satu64(double d)
{
    d = floor(d);
    if (d <= 0)
        return 0;
    if (d >= 18446744073709551616.0)
        return UINT64_C(0xffffffffffffffff);
    return (u64)d;
}

static int64_t s64_of(u32 lo, u32 hi) { return (int64_t)((u64)hi << 32 | lo); }
static u64 u64_of(u32 lo, u32 hi) { return (u64)hi << 32 | lo; }

static void ret64(struct esk_rom_call *c, u64 v)
{
    c->r[0] = (u32)v;
    c->r[1] = (u32)(v >> 32);
}

/* the float table's entry i on c's registers; its cycles */
static u32 sf(int i, struct esk_rom_call *c)
{
    float a = f_in(c->r[0]), b = f_in(c->r[1]);
    int e = (int)c->r[1];
    switch (i) {
    case 0: c->r[0] = f_out(a + b); return 60;
    case 1: c->r[0] = f_out(a - b); return 60;
    case 2: c->r[0] = f_out(a * b); return 55;
    case 3: c->r[0] = f_out(a / b); return 75;
    case 4: case 21: c->r[0] = (u32)cmp3(a, b); return 25;
    case 5: c->set_flags = 1; c->nzcv = cmp_flags(cmp3(a, b)); return 20;
    case 6: c->r[0] = a < 0 ? 0xff800000u : f_out(sqrtf(a)); return 65;
    case 7: c->r[0] = sat32(a); return 30;
    case 8: c->r[0] = sat32(ldexp(a, e)); return 30;
    case 9: c->r[0] = satu32(a); return 30;
    case 10: c->r[0] = satu32(ldexp(a, e)); return 30;
    case 11: c->r[0] = f_out((float)(s32)c->r[0]); return 30;
    case 12: c->r[0] = f_out(ldexpf((float)(s32)c->r[0], -e)); return 35;
    case 13: c->r[0] = f_out((float)c->r[0]); return 30;
    case 14: c->r[0] = f_out(ldexpf((float)c->r[0], -e)); return 35;
    case 15: c->r[0] = f_out((float)cos(a)); return 150;
    case 16: c->r[0] = f_out((float)sin(a)); return 150;
    case 17: c->r[0] = f_out((float)tan(a)); return 200;
    case 18: c->r[1] = f_out((float)cos(a)); c->r[0] = f_out((float)sin(a)); return 180;
    case 19: c->r[0] = f_out((float)exp(a)); return 120;
    case 20: c->r[0] = a < 0 ? 0xff800000u : f_out((float)log(a)); return 140;
    case 22: c->r[0] = f_out((float)atan2(a, b)); return 220;
    case 23: c->r[0] = f_out((float)s64_of(c->r[0], c->r[1])); return 45;
    case 24: c->r[0] = f_out(ldexpf((float)s64_of(c->r[0], c->r[1]), -(int)c->r[2])); return 50;
    case 25: c->r[0] = f_out((float)u64_of(c->r[0], c->r[1])); return 45;
    case 26: c->r[0] = f_out(ldexpf((float)u64_of(c->r[0], c->r[1]), -(int)c->r[2])); return 50;
    case 27: ret64(c, sat64(a)); return 45;
    case 28: ret64(c, sat64(ldexp(a, e))); return 50;
    case 29: ret64(c, satu64(a)); return 45;
    case 30: ret64(c, satu64(ldexp(a, e))); return 50;
    case 31: d_out((double)a, &c->r[0], &c->r[1]); return 25;
    }
    return 1;
}

/* the double table's entry i */
static u32 sd(int i, struct esk_rom_call *c)
{
    double a = d_in(c->r[0], c->r[1]), b = d_in(c->r[2], c->r[3]);
    int e = (int)c->r[2];
    switch (i) {
    case 0: d_out(a + b, &c->r[0], &c->r[1]); return 100;
    case 1: d_out(a - b, &c->r[0], &c->r[1]); return 100;
    case 2: d_out(a * b, &c->r[0], &c->r[1]); return 170;
    case 3: d_out(a / b, &c->r[0], &c->r[1]); return 300;
    case 4: case 21: c->r[0] = (u32)cmp3(a, b); return 35;
    case 5: c->set_flags = 1; c->nzcv = cmp_flags(cmp3(a, b)); return 30;
    case 6:
        if (a < 0) {
            c->r[0] = 0;
            c->r[1] = 0xfff00000u;
        } else {
            d_out(sqrt(a), &c->r[0], &c->r[1]);
        }
        return 450;
    case 7: c->r[0] = sat32(a); return 40;
    case 8: c->r[0] = sat32(ldexp(a, e)); return 45;
    case 9: c->r[0] = satu32(a); return 40;
    case 10: c->r[0] = satu32(ldexp(a, e)); return 45;
    case 11: d_out((double)(s32)c->r[0], &c->r[0], &c->r[1]); return 40;
    case 12: d_out(ldexp((double)(s32)c->r[0], -(int)c->r[1]), &c->r[0], &c->r[1]); return 45;
    case 13: d_out((double)c->r[0], &c->r[0], &c->r[1]); return 40;
    case 14: d_out(ldexp((double)c->r[0], -(int)c->r[1]), &c->r[0], &c->r[1]); return 45;
    case 15: d_out(cos(a), &c->r[0], &c->r[1]); return 800;
    case 16: d_out(sin(a), &c->r[0], &c->r[1]); return 800;
    case 17: d_out(tan(a), &c->r[0], &c->r[1]); return 1000;
    case 18: d_out(cos(a), &c->r[2], &c->r[3]); d_out(sin(a), &c->r[0], &c->r[1]); return 1000;
    case 19: d_out(exp(a), &c->r[0], &c->r[1]); return 650;
    case 20:
        if (a < 0) {
            c->r[0] = 0;
            c->r[1] = 0xfff00000u;
        } else {
            d_out(log(a), &c->r[0], &c->r[1]);
        }
        return 700;
    case 22: d_out(atan2(a, b), &c->r[0], &c->r[1]); return 1100;
    case 23: d_out((double)s64_of(c->r[0], c->r[1]), &c->r[0], &c->r[1]); return 60;
    case 24: d_out(ldexp((double)s64_of(c->r[0], c->r[1]), -e), &c->r[0], &c->r[1]); return 65;
    case 25: d_out((double)u64_of(c->r[0], c->r[1]), &c->r[0], &c->r[1]); return 60;
    case 26: d_out(ldexp((double)u64_of(c->r[0], c->r[1]), -e), &c->r[0], &c->r[1]); return 65;
    case 27: ret64(c, sat64(a)); return 60;
    case 28: ret64(c, sat64(ldexp(a, e))); return 65;
    case 29: ret64(c, satu64(a)); return 60;
    case 30: ret64(c, satu64(ldexp(a, e))); return 65;
    case 31: c->r[0] = f_out((float)a); return 40;
    }
    return 1;
}

/* ---- the rest -------------------------------------------------------------- */

static u32 ld(struct esk_rom_call *c, u32 a, int n) { return c->mem->ld(c->mem_ctx, a, n); }
static void st(struct esk_rom_call *c, u32 a, int n, u32 v) { c->mem->st(c->mem_ctx, a, n, v); }
static int bad(struct esk_rom_call *c) { return c->mem->faulted(c->mem_ctx); }

static u32 lookup(struct esk_rom_call *c)
{
    u32 p = c->r[0] & 0xffffu, code = c->r[1] & 0xffffu, k, cyc = 6;
    for (k = 0; k < 64 && p + 4 <= ROM_SIZE; k++, p += 4) {
        u32 have = ld(c, p, 2);
        cyc += 5;
        if (bad(c) || !have)
            break;
        if (have == code) {
            c->r[0] = ld(c, p + 2, 2);
            return cyc;
        }
    }
    c->r[0] = 0;
    return cyc;
}

static u32 rev32(u32 v)
{
    u32 r = 0;
    int i;
    for (i = 0; i < 32; i++, v >>= 1)
        r = r << 1 | (v & 1u);
    return r;
}

static u32 memops(struct esk_mach *s, int f, struct esk_rom_call *c)
{
    u32 d = c->r[0], src = c->r[1], n = c->r[2], i, w;
    (void)s;
    switch (f) {
    case F_MEMSET: /* (ptr, c, n) */
        for (i = 0; i < n && !bad(c); i++)
            st(c, d + i, 1, src & 0xffu);
        return 10 + n;
    case F_MEMSET4:
        w = (src & 0xffu) * 0x01010101u;
        for (i = 0; i + 4 <= n && !bad(c); i += 4)
            st(c, d + i, 4, w);
        return 10 + n / 2;
    case F_MEMCPY: /* (dest, src, n) */
        for (i = 0; i < n && !bad(c); i++)
            st(c, d + i, 1, ld(c, src + i, 1));
        return 10 + 2 * n;
    default: /* F_MEMCPY44 */
        for (i = 0; i + 4 <= n && !bad(c); i += 4)
            st(c, d + i, 4, ld(c, src + i, 4));
        return 10 + n;
    }
}

static u32 cycles_of(struct esk_mach *s, double seconds)
{
    u32 hz = s->core_hz ? s->core_hz(s) : s->reset_hz;
    double c = seconds * (hz ? hz : 6500000u);
    return c > 4e9 ? 0xffffffffu : (u32)c;
}

static u32 flash_op(struct esk_mach *s, int f, struct esk_rom_call *c)
{
    struct esk_region *fl = esk_bus_region(&s->bus, FLASH_BASE, FLASH_SIZE);
    u32 addr = c->r[0], i;
    u8 *mem;
    if (!fl)
        return 10;
    mem = fl->mem + (FLASH_BASE - fl->base);
    if (f == F_ERASE) { /* (addr, count, block_size, block_cmd) */
        u32 count = c->r[1], block = c->r[2], a = addr, left = count;
        double t = 0;
        for (i = 0; i < count && addr + i < FLASH_SIZE; i++)
            mem[addr + i] = 0xff;
        /* as the bootrom does: the block command for each aligned whole
         * block, 4 KiB sectors for the rest */
        while (left) {
            u32 step = 0x1000u;
            if (block >= 0x1000u && (block & (block - 1)) == 0 && !(a & (block - 1)) && left >= block &&
                (c->r[3] & 0xffu) != 0) {
                step = block;
                t += 0.150;
            } else {
                t += 0.045;
            }
            if (step > left)
                step = left;
            a += step;
            left -= step;
        }
        return cycles_of(s, t);
    }
    /* F_PROGRAM: (addr, data, count) */
    for (i = 0; i < c->r[2] && addr + i < FLASH_SIZE; i++) {
        u8 b = (u8)ld(c, c->r[1] + i, 1);
        if (bad(c))
            break;
        mem[addr + i] &= b;
    }
    return cycles_of(s, (c->r[2] + 255u) / 256u * 0.0004);
}

static int rom_call(struct esk_mach *s, u32 pc, unsigned n, struct esk_rom_call *c)
{
    if (pc != stub((int)n) || n >= F_END || (n >= F_COUNT && n < F_SF))
        return 0;
    switch ((int)n) {
    case F_LOOKUP: c->cycles = lookup(c); break;
    case F_POPCOUNT: {
        u32 v = c->r[0], k = 0;
        for (; v; v &= v - 1)
            k++;
        c->r[0] = k;
        c->cycles = 15;
        break;
    }
    case F_REVERSE: c->r[0] = rev32(c->r[0]); c->cycles = 20; break;
    case F_CLZ: {
        u32 v = c->r[0], k = 0;
        while (k < 32 && !(v & 0x80000000u)) {
            v <<= 1;
            k++;
        }
        c->r[0] = k;
        c->cycles = 15;
        break;
    }
    case F_CTZ: {
        u32 v = c->r[0], k = 0;
        while (k < 32 && !(v & 1u)) {
            v >>= 1;
            k++;
        }
        c->r[0] = k;
        c->cycles = 15;
        break;
    }
    case F_MEMSET: case F_MEMSET4: case F_MEMCPY: case F_MEMCPY44:
        c->cycles = memops(s, (int)n, c);
        break;
    case F_CONNECT_FLASH: case F_EXIT_XIP: case F_FLUSH_CACHE: case F_ENTER_XIP:
        c->cycles = 100;
        break;
    case F_ERASE: case F_PROGRAM: c->cycles = flash_op(s, (int)n, c); break;
    case F_USB_BOOT:
        esk_rf_diag(s, ES_SEV_WARNING, "mcu.usb_boot",
                    "the program called the RP2040's reset_to_usb_boot: the part waits in its USB boot");
        c->cycles = 10;
        break;
    default:
        c->cycles = n < F_SD ? sf((int)n - F_SF, c) : sd((int)n - F_SD, c);
        break;
    }
    return 1;
}

void esk_rp2040_rom_install(struct esk_mach *s)
{
    s->rom_call = rom_call;
}
