/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* disasm.c -- the AVR5 instruction at an address, as avr-objdump writes
 * it (registers r0-r31, I/O addresses in hex, branch targets as byte
 * addresses), for the Debug perspective's disassembly. An encoding the
 * ATmega328P does not have is ".word 0x....". */
#include <stdarg.h>
#include <stdio.h>

#include "mcu/avr/avr.h"

static const char *const brbs[8] = { "brcs", "breq", "brmi", "brvs", "brlt", "brhs", "brts", "brie" };
static const char *const brbc[8] = { "brcc", "brne", "brpl", "brvc", "brge", "brhc", "brtc", "brid" };
static const char *const bset[8] = { "sec", "sez", "sen", "sev", "ses", "seh", "set", "sei" };
static const char *const bclr[8] = { "clc", "clz", "cln", "clv", "cls", "clh", "clt", "cli" };

static int out(char *t, size_t cap, int size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(t, cap, fmt, ap);
    va_end(ap);
    return size;
}

/* LD and ST's pointer forms: the operand text of mode m on pointer p */
static const char *ptr_text(unsigned low)
{
    switch (low) {
    case 0x1: return "Z+";
    case 0x2: return "-Z";
    case 0x9: return "Y+";
    case 0xa: return "-Y";
    case 0xc: return "X";
    case 0xd: return "X+";
    case 0xe: return "-X";
    }
    return 0;
}

int esk_avr_disasm(u32 pc, unsigned w, unsigned w2, char *t, size_t cap)
{
    unsigned d = (w >> 4) & 31, r = (w & 15) | ((w >> 5) & 16);
    unsigned K = ((w >> 4) & 0xf0) | (w & 15), dh = 16 + ((w >> 4) & 15);
    unsigned io = (w & 15) | ((w >> 5) & 0x30);
    w &= 0xffff;
    w2 &= 0xffff;
    switch (w >> 12) {
    case 0x0:
        if (w == 0)
            return out(t, cap, 2, "nop");
        switch ((w >> 8) & 0xf) {
        case 0x1: return out(t, cap, 2, "movw r%u, r%u", ((w >> 4) & 15) * 2, (w & 15) * 2);
        case 0x2: return out(t, cap, 2, "muls r%u, r%u", 16 + ((w >> 4) & 15), 16 + (w & 15));
        case 0x3: {
            static const char *const m[4] = { "mulsu", "fmul", "fmuls", "fmulsu" };
            return out(t, cap, 2, "%s r%u, r%u", m[((w >> 6) & 2) | ((w >> 3) & 1)], 16 + ((w >> 4) & 7),
                       16 + (w & 7));
        }
        }
        switch ((w >> 10) & 3) {
        case 1: return out(t, cap, 2, "cpc r%u, r%u", d, r);
        case 2: return out(t, cap, 2, "sbc r%u, r%u", d, r);
        case 3: return d == r ? out(t, cap, 2, "lsl r%u", d) : out(t, cap, 2, "add r%u, r%u", d, r);
        }
        break;
    case 0x1: {
        static const char *const m[4] = { "cpse", "cp", "sub", "adc" };
        if (((w >> 10) & 3) == 3 && d == r)
            return out(t, cap, 2, "rol r%u", d);
        return out(t, cap, 2, "%s r%u, r%u", m[(w >> 10) & 3], d, r);
    }
    case 0x2: {
        static const char *const m[4] = { "and", "eor", "or", "mov" };
        if (((w >> 10) & 3) == 1 && d == r)
            return out(t, cap, 2, "clr r%u", d);
        if (((w >> 10) & 3) == 0 && d == r)
            return out(t, cap, 2, "tst r%u", d);
        return out(t, cap, 2, "%s r%u, r%u", m[(w >> 10) & 3], d, r);
    }
    case 0x3: return out(t, cap, 2, "cpi r%u, 0x%02X", dh, K);
    case 0x4: return out(t, cap, 2, "sbci r%u, 0x%02X", dh, K);
    case 0x5: return out(t, cap, 2, "subi r%u, 0x%02X", dh, K);
    case 0x6: return out(t, cap, 2, "ori r%u, 0x%02X", dh, K);
    case 0x7: return out(t, cap, 2, "andi r%u, 0x%02X", dh, K);
    case 0x8: case 0xa: {
        unsigned q = (w & 7) | ((w >> 7) & 0x18) | ((w >> 8) & 0x20);
        char p = (w & 8) ? 'Y' : 'Z';
        if (w & 0x200)
            return q ? out(t, cap, 2, "std %c+%u, r%u", p, q, d) : out(t, cap, 2, "st %c, r%u", p, d);
        return q ? out(t, cap, 2, "ldd r%u, %c+%u", d, p, q) : out(t, cap, 2, "ld r%u, %c", d, p);
    }
    case 0xb:
        if (w & 0x800)
            return out(t, cap, 2, "out 0x%02x, r%u", io, d);
        return out(t, cap, 2, "in r%u, 0x%02x", d, io);
    case 0xc: case 0xd: {
        int k = (int)((w & 0xfff) ^ 0x800) - 0x800;
        u32 to = (u32)((int)pc + 2 + 2 * k) & 0xffff;
        return out(t, cap, 2, "%s .%+d ; 0x%x", (w & 0x1000) ? "rcall" : "rjmp", 2 * k, (unsigned)to);
    }
    case 0xe:
        if (K == 0xff)
            return out(t, cap, 2, "ser r%u", dh);
        return out(t, cap, 2, "ldi r%u, 0x%02X", dh, K);
    case 0xf: {
        unsigned b = w & 7;
        int k = (int)(((w >> 3) & 0x7f) ^ 0x40) - 0x40;
        u32 to = (u32)((int)pc + 2 + 2 * k) & 0xffff;
        switch ((w >> 9) & 7) {
        case 0: case 1: return out(t, cap, 2, "%s .%+d ; 0x%x", brbs[b], 2 * k, (unsigned)to);
        case 2: case 3: return out(t, cap, 2, "%s .%+d ; 0x%x", brbc[b], 2 * k, (unsigned)to);
        case 4: if (!(w & 8)) return out(t, cap, 2, "bld r%u, %u", d, b); break;
        case 5: if (!(w & 8)) return out(t, cap, 2, "bst r%u, %u", d, b); break;
        case 6: if (!(w & 8)) return out(t, cap, 2, "sbrc r%u, %u", d, b); break;
        default: if (!(w & 8)) return out(t, cap, 2, "sbrs r%u, %u", d, b); break;
        }
        break;
    }
    default: /* 0x9 */
        switch ((w >> 8) & 0xf) {
        case 0x0: case 0x1: {
            const char *p = ptr_text(w & 15);
            if ((w & 15) == 0)
                return out(t, cap, 4, "lds r%u, 0x%04X", d, w2);
            if ((w & 15) == 4)
                return out(t, cap, 2, "lpm r%u, Z", d);
            if ((w & 15) == 5)
                return out(t, cap, 2, "lpm r%u, Z+", d);
            if ((w & 15) == 0xf)
                return out(t, cap, 2, "pop r%u", d);
            if (p)
                return out(t, cap, 2, "ld r%u, %s", d, p);
            break;
        }
        case 0x2: case 0x3: {
            const char *p = ptr_text(w & 15);
            if ((w & 15) == 0)
                return out(t, cap, 4, "sts 0x%04X, r%u", w2, d);
            if ((w & 15) == 0xf)
                return out(t, cap, 2, "push r%u", d);
            if (p)
                return out(t, cap, 2, "st %s, r%u", p, d);
            break;
        }
        case 0x4: case 0x5:
            if ((w & 0xfe0c) == 0x940c) {
                u32 k = ((u32)(((w >> 3) & 0x3e) | (w & 1)) << 16 | w2) * 2;
                return out(t, cap, 4, "%s 0x%x", (w & 2) ? "call" : "jmp", (unsigned)k);
            }
            if ((w & 0xff0f) == 0x9408)
                return out(t, cap, 2, "%s", (w & 0x80) ? bclr[(w >> 4) & 7] : bset[(w >> 4) & 7]);
            switch (w) {
            case 0x9508: return out(t, cap, 2, "ret");
            case 0x9518: return out(t, cap, 2, "reti");
            case 0x9588: return out(t, cap, 2, "sleep");
            case 0x9598: return out(t, cap, 2, "break");
            case 0x95a8: return out(t, cap, 2, "wdr");
            case 0x95c8: return out(t, cap, 2, "lpm");
            case 0x95e8: return out(t, cap, 2, "spm");
            case 0x9409: return out(t, cap, 2, "ijmp");
            case 0x9509: return out(t, cap, 2, "icall");
            }
            switch (w & 15) {
            case 0x0: return out(t, cap, 2, "com r%u", d);
            case 0x1: return out(t, cap, 2, "neg r%u", d);
            case 0x2: return out(t, cap, 2, "swap r%u", d);
            case 0x3: return out(t, cap, 2, "inc r%u", d);
            case 0x5: return out(t, cap, 2, "asr r%u", d);
            case 0x6: return out(t, cap, 2, "lsr r%u", d);
            case 0x7: return out(t, cap, 2, "ror r%u", d);
            case 0xa: return out(t, cap, 2, "dec r%u", d);
            }
            break;
        case 0x6: case 0x7:
            return out(t, cap, 2, "%s r%u, 0x%02x", (w & 0x100) ? "sbiw" : "adiw", 24 + 2 * ((w >> 4) & 3),
                       (w & 15) | ((w >> 2) & 0x30));
        case 0x8: return out(t, cap, 2, "cbi 0x%02x, %u", (w >> 3) & 31, w & 7);
        case 0x9: return out(t, cap, 2, "sbic 0x%02x, %u", (w >> 3) & 31, w & 7);
        case 0xa: return out(t, cap, 2, "sbi 0x%02x, %u", (w >> 3) & 31, w & 7);
        case 0xb: return out(t, cap, 2, "sbis 0x%02x, %u", (w >> 3) & 31, w & 7);
        default: return out(t, cap, 2, "mul r%u, r%u", d, r);
        }
        break;
    }
    return out(t, cap, 2, ".word 0x%04x", w);
}
