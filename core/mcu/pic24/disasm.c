/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* disasm.c -- a PIC24F instruction's text (and a dsPIC33E's DSP
 * instructions'), for the Debug perspective, in XC16 as's syntax: the
 * W registers as w0-w15, a branch's target as a program address, a
 * file register as its data address. Anything else is `.word 0x...`.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/disasm.c (dis_pic24). */
#include <stdio.h>
#include <string.h>

#include "mcu/pic24/pic24.h"

/* ---- PIC24F -------------------------------------------------------------- */

/* the source (ppp) and destination (qqq) operand of mode m on Wr, Wb its
 * offset register for [Wr+Wb] */
static void p24_op(char *b, size_t n, int m, u32 r, u32 wb)
{
    switch (m) {
    case 0: snprintf(b, n, "w%u", r); return;
    case 1: snprintf(b, n, "[w%u]", r); return;
    case 2: snprintf(b, n, "[w%u--]", r); return;
    case 3: snprintf(b, n, "[w%u++]", r); return;
    case 4: snprintf(b, n, "[--w%u]", r); return;
    case 5: snprintf(b, n, "[++w%u]", r); return;
    default: snprintf(b, n, "[w%u+w%u]", r, wb); return;
    }
}

/* f's destination: WREG, or f itself */
static const char *p24_wreg(u32 i)
{
    return (i >> 13 & 1) ? "" : ", wreg";
}

/* a dsPIC's MAC class prefetch (iiii for X, jjjj for Y) into Wd: ", [w8]+=2,
 * w4"; nothing for 0100 (none) */
static int p24_prefetch(char *b, size_t n, u32 f, int y, u32 d)
{
    static const char *const step[8] = { "", "+=2", "+=4", "+=6", 0, "-=6", "-=4", "-=2" };
    u32 r = (y ? 10 : 8) + (f >> 3);
    if (f == 4)
        return 0;
    if (f == 12)
        return snprintf(b, n, ", [w%u+w12], w%u", r, d);
    return snprintf(b, n, ", [w%u]%s, w%u", r, step[f & 7], d);
}

/* the DSP instructions: the MAC class (0xC0-0xC7, 0xF0-0xF3) and the
 * accumulator's (0xC8-0xCD) */
static int dis_p24_dsp(u32 i, char *b, size_t n)
{
    static const int wm[8] = { 4, 4, 4, 0, 5, 5, 6, 0 }, wn[8] = { 5, 6, 7, 0, 6, 7, 7, 0 };
    static const char *const shift_dst[2] = { "a", "b" };
    u32 op = i >> 16 & 0xff, a = i >> 15 & 1;
    char x[24] = "", y[24] = "", wb[16] = "", s1[24];
    const char *acc = shift_dst[a];
    if (op <= 0xc7 || op >= 0xf0) {
        u32 aa = i & 3;
        p24_prefetch(x, sizeof x, i >> 6 & 15, 0, 4 + (i >> 12 & 3));
        if (op >= 0xf0) {                       /* the squares, ED, EDAC */
            u32 m = 4 + (op & 3);
            static const char *const sq[4] = { "mac", "mpy", "edac", "ed" };
            if (aa >= 2) {                      /* [Wx], [Wy], Wxd */
                char yy[24] = "";
                p24_prefetch(yy, sizeof yy, i >> 2 & 15, 1, 0);
                char *comma = strrchr(yy, ',');
                if (comma)
                    *comma = 0;
                char *xc = strrchr(x, ',');
                char xd[8] = "";
                if (xc) {
                    snprintf(xd, sizeof xd, "%s", xc);
                    *xc = 0;
                }
                return snprintf(b, n, "%s w%u*w%u, %s%s%s%s", sq[aa], m, m, acc, x, yy, xd);
            }
            p24_prefetch(y, sizeof y, i >> 2 & 15, 1, 4 + (i >> 10 & 3));
            return snprintf(b, n, "%s w%u*w%u, %s%s%s", sq[aa], m, m, acc, x, y);
        }
        p24_prefetch(y, sizeof y, i >> 2 & 15, 1, 4 + (i >> 10 & 3));
        if (aa == 0)
            snprintf(wb, sizeof wb, ", w13");
        else if (aa == 1)
            snprintf(wb, sizeof wb, ", [w13]+=2");
        u32 mmm = op & 7, neg = i >> 14 & 1;
        if (mmm == 3)
            return snprintf(b, n, "clr %s%s%s%s", acc, x, y, wb);
        if (mmm == 7)
            return snprintf(b, n, "movsac %s%s%s%s", acc, x, y, wb);
        snprintf(s1, sizeof s1, "w%d*w%d", wm[mmm], wn[mmm]);
        if (aa == 3)
            return snprintf(b, n, "%s %s, %s%s%s", neg ? "mpy.n" : "mpy", s1, acc, x, y);
        return snprintf(b, n, "%s %s, %s%s%s%s", neg ? "msc" : "mac", s1, acc, x, y, wb);
    }
    int k = (int)(i >> 7 & 15);
    k = (k ^ 8) - 8;
    switch (op) {
    case 0xc8:
        if (i & 0x40) {
            int v = (int)(i & 0x3f);
            return snprintf(b, n, "sftac %s, #%d", acc, (v ^ 0x20) - 0x20);
        }
        return snprintf(b, n, "sftac %s, w%u", acc, (unsigned)(i & 15));
    case 0xc9: case 0xca:
        p24_op(s1, sizeof s1, (int)(i >> 4 & 7), i & 15, i >> 11 & 15);
        if (k)
            return snprintf(b, n, "%s %s, #%d, %s", op == 0xc9 ? "add" : "lac", s1, k, acc);
        return snprintf(b, n, "%s %s, %s", op == 0xc9 ? "add" : "lac", s1, acc);
    case 0xcb: {
        static const char *const ab[8] = { "add", "neg", 0, "sub" };
        const char *nm = ab[i >> 12 & 7];
        if (nm && !(i & 0x0fff))
            return snprintf(b, n, "%s %s", nm, acc);
        break;
    }
    case 0xcc: case 0xcd:
        p24_op(s1, sizeof s1, (int)(i >> 4 & 7), i & 15, i >> 11 & 15);
        if (k)
            return snprintf(b, n, "%s %s, #%d, %s", op == 0xcc ? "sac" : "sac.r", acc, k, s1);
        return snprintf(b, n, "%s %s, %s", op == 0xcc ? "sac" : "sac.r", acc, s1);
    }
    return snprintf(b, n, ".word 0x%06x", (unsigned)i);
}

static int dis_pic24(u32 pc, u32 i, u32 i2, char *b, size_t n)
{
    static const char *const cc[16] = {
        "ov", "c", "z", "n", "le", "lt", "leu", "", "nov", "nc", "nz", "nn",
        "gt", "ge", "gtu", 0 };
    static const char *const alu2[8] = {
        "add", "addc", "sub", "subb", "and", "xor", "ior", 0 };
    static const char *const sh[16] = {
        "sl", 0, "lsr", "asr", "rlnc", "rlc", "rrnc", "rrc",
        "inc", "inc2", "dec", "dec2", "neg", "com", "clr", "setm" };
    u32 op = i >> 16 & 0xff, B = i >> 14 & 1;
    const char *bs = B ? ".b" : "";
    u32 wb = i >> 15 & 15, s = i & 15, d = i >> 7 & 15;
    int pm = (int)(i >> 4 & 7), qm = (int)(i >> 11 & 7);
    u32 f13 = i & 0x1fff;
    s32 rel = (s32)(int16_t)(i & 0xffff);
    char x[24], y[24];
    i &= 0xffffff;
    if (i == 0)
        return snprintf(b, n, "nop"), 1;
    if (i == 0xffffff)
        return snprintf(b, n, "nopr"), 1;
    switch (op) {
    case 0x01: {
        static const char *const w[4] = { "call", "rcall", "goto", "bra" };
        if ((i & 0x9ff0) == 0)
            return snprintf(b, n, "%s w%u", w[i >> 13 & 3], s), 1;
        if ((i & 0xf9f0) == 0)                  /* the PIC24E's */
            return snprintf(b, n, "%s w%u", w[i >> 9 & 3], s), 1;
        if ((i & 0x83f0) == 0x8000)
            return snprintf(b, n, "%s w%u", i & 0x400 ? "goto.l" : "call.l", s), 1;
        break;
    }
    case 0x02: case 0x04:
        return snprintf(b, n, "%s 0x%06x", op == 2 ? "call" : "goto",
                        (unsigned)((i & 0xfffe) | (i2 & 0x7f) << 16)), 2;
    case 0x05:
        return snprintf(b, n, "retlw%s #%u, w%u", bs, (unsigned)(i >> 4 & 0x3ff), s), 1;
    case 0x06:
        if (i == 0x060000)
            return snprintf(b, n, "return"), 1;
        if (i == 0x064000)
            return snprintf(b, n, "retfie"), 1;
        break;
    case 0x07:
        return snprintf(b, n, "rcall 0x%06x", (unsigned)(pc + 2 + 2 * (u32)rel)), 1;
    case 0x08:                                  /* a dsPIC's DO */
        if (i & 0x8000)
            snprintf(x, sizeof x, "w%u", s);
        else
            snprintf(x, sizeof x, "#%u", (unsigned)(i & 0x7fff));
        return snprintf(b, n, "do %s, 0x%06x", x,
                        (unsigned)((pc + 4 + 2 * (u32)(s32)(int16_t)(i2 & 0xffff)) & 0x7ffffe)), 2;
    case 0x09:
        if (i & 0x8000)
            return snprintf(b, n, "repeat w%u", s), 1;
        return snprintf(b, n, "repeat #%u", (unsigned)(i & 0x7fff)), 1;
    case 0x0c: case 0x0d: case 0x0e: case 0x0f: {
        static const char *const dc[4] = { "oa", "ob", "sa", "sb" };
        return snprintf(b, n, "bra %s, 0x%06x", dc[op - 0x0c],
                        (unsigned)(pc + 2 + 2 * (u32)rel)), 1;
    }
    case 0x80: case 0x81: case 0x82: case 0x83:
    case 0x84: case 0x85: case 0x86: case 0x87:
        return snprintf(b, n, "mov 0x%04x, w%u", (unsigned)(i >> 4 & 0x7fff) << 1, s), 1;
    case 0x88: case 0x89: case 0x8a: case 0x8b:
    case 0x8c: case 0x8d: case 0x8e: case 0x8f:
        return snprintf(b, n, "mov w%u, 0x%04x", s, (unsigned)(i >> 4 & 0x7fff) << 1), 1;
    case 0x90: case 0x91: case 0x92: case 0x93:
    case 0x94: case 0x95: case 0x96: case 0x97:
    case 0x98: case 0x99: case 0x9a: case 0x9b:
    case 0x9c: case 0x9d: case 0x9e: case 0x9f: {
        u32 k = wb << 6 | (u32)qm << 3 | (u32)pm;
        s32 off = (s32)((k ^ 0x200) - 0x200) * (B ? 1 : 2);
        if (op < 0x98)
            return snprintf(b, n, "mov%s [w%u%+d], w%u", bs, s, (int)off, d), 1;
        return snprintf(b, n, "mov%s w%u, [w%u%+d]", bs, s, d, (int)off), 1;
    }
    case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4:
    case 0xa6: case 0xa7: {
        static const char *const bw[8] = { "bset", "bclr", "btg", "btst",
                                           "btsts", 0, "btss", "btsc" };
        p24_op(x, sizeof x, pm, s, 0);
        const char *z = (op == 0xa3 || op == 0xa4) ? (i & 0x800 ? ".z" : ".c") : "";
        return snprintf(b, n, "%s%s%s %s, #%u", bw[op - 0xa0],
                        op <= 0xa2 && (i & 0x400) ? ".b" : "", z, x,
                        (unsigned)(i >> 12 & 15)), 1;
    }
    case 0xa5: case 0xad:
        p24_op(x, sizeof x, pm, s, 0);
        return snprintf(b, n, "%s%s %s, w%u", op == 0xa5 ? "btst" : "bsw",
                        i & 0x8000 ? ".z" : ".c", x, (unsigned)(i >> 11 & 15)), 1;
    case 0xa8: case 0xa9: case 0xaa: case 0xab:
    case 0xac: case 0xae: case 0xaf: {
        static const char *const bf[8] = { "bset", "bclr", "btg", "btst",
                                           "btsts", 0, "btss", "btsc" };
        return snprintf(b, n, "%s 0x%04x, #%u", bf[op - 0xa8], (unsigned)(i & 0x1ffe),
                        (unsigned)((i >> 13 & 7) | (i & 1) << 3)), 1;
    }
    case 0xb0: case 0xb1: case 0xb2:
        return snprintf(b, n, "%s%s #%u, w%u", alu2[(op - 0xb0) * 2 + (i >> 15 & 1)], bs,
                        (unsigned)(i >> 4 & 0x3ff), s), 1;
    case 0xb3:
        if (!(i & 0x8000))
            return snprintf(b, n, "ior%s #%u, w%u", bs, (unsigned)(i >> 4 & 0x3ff), s), 1;
        if ((i >> 12 & 15) == 0xc)
            return snprintf(b, n, "mov.b #%u, w%u", (unsigned)(i >> 4 & 0xff), s), 1;
        break;
    case 0xb4: case 0xb5: case 0xb6:
        return snprintf(b, n, "%s%s 0x%04x%s", alu2[(op - 0xb4) * 2 + (i >> 15 & 1)], bs,
                        (unsigned)f13, p24_wreg(i)), 1;
    case 0xb7:
        if (!(i & 0x8000))
            return snprintf(b, n, "ior%s 0x%04x%s", bs, (unsigned)f13, p24_wreg(i)), 1;
        if (i & 0x2000)
            return snprintf(b, n, "mov%s wreg, 0x%04x", bs, (unsigned)f13), 1;
        break;
    case 0xb8: case 0xb9: {
        static const char *const m[4] = { "uu", "us", "su", "ss" };
        const char *v = m[(op - 0xb8) * 2 + (i >> 15 & 1)];
        if ((pm & 6) == 6)
            snprintf(x, sizeof x, "#%u", s | (u32)(pm & 1) << 4);
        else
            p24_op(x, sizeof x, pm, s, 0);
        if (d >= 14)                            /* a dsPIC33E's accumulator */
            return snprintf(b, n, "mul.%s w%u, %s, %s", v, (unsigned)(i >> 11 & 15), x,
                            d == 14 ? "a" : "b"), 1;
        if (d & 1)                              /* the PIC24E's MULW */
            return snprintf(b, n, "mulw.%s w%u, %s, w%u", v, (unsigned)(i >> 11 & 15), x,
                            d - 1), 1;
        return snprintf(b, n, "mul.%s w%u, %s, w%u", v, (unsigned)(i >> 11 & 15), x, d), 1;
    }
    case 0xba: case 0xbb:
        p24_op(x, sizeof x, pm, s, 0);
        p24_op(y, sizeof y, qm, d, 0);
        return snprintf(b, n, "%s%s%s %s, %s", op == 0xba ? "tblrd" : "tblwt",
                        i & 0x8000 ? "h" : "l", bs, x, y), 1;
    case 0xbc:
        return snprintf(b, n, "mul%s 0x%04x", bs, (unsigned)f13), 1;
    case 0xbd:
        return snprintf(b, n, "%s%s 0x%04x%s", i & 0x8000 ? "subbr" : "subr", bs,
                        (unsigned)f13, p24_wreg(i)), 1;
    case 0xbe:
        if (!(i & 0x8000)) {
            if (pm == 4 && s == 15)
                return snprintf(b, n, "pop.d w%u", (unsigned)(i >> 8 & 7) * 2), 1;
            p24_op(x, sizeof x, pm, s, 0);
            return snprintf(b, n, "mov.d %s, w%u", x, (unsigned)(i >> 8 & 7) * 2), 1;
        }
        if (qm == 3 && d == 15)
            return snprintf(b, n, "push.d w%u", (unsigned)(i >> 1 & 7) * 2), 1;
        p24_op(y, sizeof y, qm, d, 0);
        return snprintf(b, n, "mov.d w%u, %s", (unsigned)(i >> 1 & 7) * 2, y), 1;
    case 0xbf:
        return snprintf(b, n, "mov%s 0x%04x%s", bs, (unsigned)f13, p24_wreg(i)), 1;
    case 0xc0: case 0xc1: case 0xc2: case 0xc3: case 0xc4: case 0xc5:
    case 0xc6: case 0xc7: case 0xc8: case 0xc9: case 0xca: case 0xcb:
    case 0xcc: case 0xcd: case 0xf0: case 0xf1: case 0xf2: case 0xf3:
        return dis_p24_dsp(i, b, n), 1;
    case 0xd9:
        if (!(i & 0x8780))
            return snprintf(b, n, "divf w%u, w%u", (unsigned)(i >> 11 & 15), s), 1;
        break;
    case 0xcf:
        p24_op(x, sizeof x, pm, s, 0);
        return snprintf(b, n, "%s %s, w%u", i & 0x8000 ? "ff1l" : "ff1r", x, d), 1;
    case 0xd0: case 0xd1: case 0xd2: case 0xd3:
    case 0xe8: case 0xe9: case 0xea: {
        u32 k = op >= 0xe8 ? 8 + (op - 0xe8) * 2 : (op - 0xd0) * 2;
        const char *nm = sh[k + (i >> 15 & 1)];
        if (!nm)
            break;
        p24_op(x, sizeof x, pm, s, 0);
        p24_op(y, sizeof y, qm, d, 0);
        return snprintf(b, n, "%s%s %s, %s", nm, bs, x, y), 1;
    }
    case 0xd4: case 0xd5: case 0xd6: case 0xd7:
    case 0xec: case 0xed: case 0xee: {
        u32 k = op >= 0xec ? 8 + (op - 0xec) * 2 : (op - 0xd4) * 2;
        const char *nm = sh[k + (i >> 15 & 1)];
        if (!nm)
            break;
        return snprintf(b, n, "%s%s 0x%04x%s", nm, bs, (unsigned)f13, p24_wreg(i)), 1;
    }
    case 0xeb:
        p24_op(y, sizeof y, qm, d, 0);
        return snprintf(b, n, "%s%s %s", i & 0x8000 ? "setm" : "clr", bs, y), 1;
    case 0xef:
        if (!f13 && !(i & 0x2000))
            return snprintf(b, n, "%s%s wreg", i & 0x8000 ? "setm" : "clr", bs), 1;
        return snprintf(b, n, "%s%s 0x%04x%s", i & 0x8000 ? "setm" : "clr", bs,
                        (unsigned)f13, p24_wreg(i)), 1;
    case 0xd8:
        return snprintf(b, n, "div.%s%s w%u, w%u", i & 0x8000 ? "u" : "s",
                        i & 0x40 ? "d" : "", (unsigned)(i >> 7 & 15), s), 1;
    case 0xdd: case 0xde:
        if (i & 0x40)
            snprintf(x, sizeof x, "#%u", s);
        else
            snprintf(x, sizeof x, "w%u", s);
        return snprintf(b, n, "%s w%u, %s, w%u",
                        op == 0xdd ? "sl" : i & 0x8000 ? "asr" : "lsr",
                        (unsigned)(i >> 11 & 15), x, d), 1;
    case 0xdf:
        p24_op(x, sizeof x, pm, s, 0);
        return snprintf(b, n, "fbcl %s, w%u", x, d), 1;
    case 0xe0:
        p24_op(x, sizeof x, pm, s, 0);
        return snprintf(b, n, "cp0%s %s", i & 0x400 ? ".b" : "", x), 1;
    case 0xe1:
        if ((pm & 6) == 6)                      /* the PIC24E's 8 bits: 9:7 */
            snprintf(x, sizeof x, "#%u", (unsigned)((i >> 7 & 7) << 5 | (i & 31)));
        else
            p24_op(x, sizeof x, pm, s, 0);
        return snprintf(b, n, "%s%s w%u, %s", i & 0x8000 ? "cpb" : "cp",
                        i & 0x400 ? ".b" : "", (unsigned)(i >> 11 & 15), x), 1;
    case 0xe2:
        return snprintf(b, n, "cp0%s 0x%04x", bs, (unsigned)f13), 1;
    case 0xe3:
        return snprintf(b, n, "%s%s 0x%04x", i & 0x8000 ? "cpb" : "cp", bs,
                        (unsigned)f13), 1;
    case 0xe6: case 0xe7: {
        static const char *const c[4] = { "cpsgt", "cpslt", "cpsne", "cpseq" };
        static const char *const cb[4] = { "cpbgt", "cpblt", "cpbne", "cpbeq" };
        int k = (int)(op - 0xe6) * 2 + (int)(i >> 15 & 1);
        s32 off = (s32)((i >> 4 & 63) ^ 32) - 32;
        if (off > 1 || off < 0)                 /* the PIC24E's compare and branch */
            return snprintf(b, n, "%s%s w%u, w%u, 0x%06x", cb[k], i & 0x400 ? ".b" : "",
                            (unsigned)(i >> 11 & 15), s, (unsigned)(pc + 2 + 2 * (u32)off)), 1;
        return snprintf(b, n, "%s%s w%u, w%u", c[k],
                        i & 0x400 ? ".b" : "", (unsigned)(i >> 11 & 15), s), 1;
    }
    case 0xf8: return snprintf(b, n, "push 0x%04x", (unsigned)(i & 0xfffe)), 1;
    case 0xf9: return snprintf(b, n, "pop 0x%04x", (unsigned)(i & 0xfffe)), 1;
    case 0xfa:
        if (i == 0xfa8000)
            return snprintf(b, n, "ulnk"), 1;
        return snprintf(b, n, "lnk #%u", (unsigned)(i & 0x3ffe)), 1;
    case 0xfb:
        p24_op(x, sizeof x, pm, s, 0);
        return snprintf(b, n, "%s %s, w%u", i & 0x8000 ? "ze" : "se", x, d), 1;
    case 0xda:
        if (i == 0xda4000)
            return snprintf(b, n, "break"), 1;
        break;
    case 0xfc:
        return snprintf(b, n, "disi #%u", (unsigned)(i & 0x3fff)), 1;
    case 0xfd:
        if ((i & 0xf870) == 0)
            return snprintf(b, n, "exch w%u, w%u", s, d), 1;
        if ((i & 0xfff0) == 0x4000)
            return snprintf(b, n, "daw.b w%u", s), 1;
        if ((i & 0xbff0) == 0x8000)
            return snprintf(b, n, "swap%s w%u", B ? ".b" : "", s), 1;
        break;
    case 0xfe:
        if ((i & 0xe000) == 0xc000 && (i >> 10 & 3) != 3) {    /* MOVPAG */
            static const char *const pg[3] = { "dsrpag", "dswpag", "tblpag" };
            if (i & 0x1000)
                return snprintf(b, n, "movpag w%u, %s", s, pg[i >> 10 & 3]), 1;
            return snprintf(b, n, "movpag #0x%x, %s", (unsigned)(i & 0x3ff), pg[i >> 10 & 3]), 1;
        }
        switch (i & 0xffff) {
        case 0x0000: return snprintf(b, n, "reset"), 1;
        case 0x4000: case 0x4001:
            return snprintf(b, n, "pwrsav #%u", (unsigned)(i & 1)), 1;
        case 0x6000: return snprintf(b, n, "clrwdt"), 1;
        case 0x8000: return snprintf(b, n, "pop.s"), 1;
        case 0xa000: return snprintf(b, n, "push.s"), 1;
        }
        break;
    }
    if (op >= 0x20 && op <= 0x2f)
        return snprintf(b, n, "mov #0x%04x, w%u", (unsigned)(i >> 4 & 0xffff), s), 1;
    if (op >= 0x30 && op <= 0x3e) {
        u32 t = pc + 2 + 2 * (u32)rel;
        if (op == 0x37)
            return snprintf(b, n, "bra 0x%06x", (unsigned)t), 1;
        return snprintf(b, n, "bra %s, 0x%06x", cc[op & 15], (unsigned)t), 1;
    }
    if ((op >= 0x10 && op <= 0x1f) || (op >= 0x40 && op <= 0x77)) {
        static const char *const w3[16] = { 0, 0, "subr", "subbr", 0, 0, 0, 0,
                                            "add", "addc", "sub", "subb", "and",
                                            "xor", "ior", 0 };
        const char *nm = w3[op >> 3];
        if (nm) {
            if ((pm & 6) == 6)
                snprintf(x, sizeof x, "#%u", (unsigned)(i & 31));
            else
                p24_op(x, sizeof x, pm, s, 0);
            p24_op(y, sizeof y, qm, d, 0);
            return snprintf(b, n, "%s%s w%u, %s, %s", nm, bs, wb, x, y), 1;
        }
    }
    if (op >= 0x78 && op <= 0x7f) {
        p24_op(x, sizeof x, pm, s, wb);
        p24_op(y, sizeof y, qm, d, wb);
        if (!B && pm == 4 && s == 15)                   /* pop: mov [--w15] */
            return snprintf(b, n, "pop %s", y), 1;
        if (!B && qm == 3 && d == 15)                   /* push: mov to [w15++] */
            return snprintf(b, n, "push %s", x), 1;
        return snprintf(b, n, "mov%s %s, %s", bs, x, y), 1;
    }
    return snprintf(b, n, ".word 0x%06x", (unsigned)i), 1;
}


int esk_pic24_disasm(u32 pc, u32 i, u32 i2, char *text, size_t cap)
{
    return 2 * dis_pic24(pc, i, i2, text, cap);
}
