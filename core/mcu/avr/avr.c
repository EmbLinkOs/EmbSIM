/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* avr.c -- the AVR core of the ATmega328P: the AVR5 instruction set (the
 * classic core with MUL, MOVW, LPM Rd,Z, JMP and CALL, a 16-bit program
 * counter and no ELPM), SREG's flags exactly, the register file and SP and
 * SREG in the data space, interrupts through the vector table, and SLEEP.
 *
 * Cycles are exact (ADR-015): each instruction's are the datasheet's
 * (cost.h), with the dynamic parts added here -- a taken branch 1 more, a
 * skip 1 more for a one-word instruction skipped and 2 for a two-word one,
 * an interrupt's entry 4 (and 4 more from SLEEP). The part has no wait
 * states, so the count is what the silicon takes.
 *
 * An instruction this part does not have (ELPM, the XMEGA's, SPM, which is
 * not modelled) locks the core up naming it, rather than doing something
 * no part does.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/avr.c, whose
 * instruction set and flags are refereed against QEMU's uno there.
 * Changes for EmbSIM: the core state is a parameter (no file-scope `as`,
 * CS-18); SLEEP and a jump to itself put the machine to sleep
 * (esk_mach.asleep) for the MCU runtime to fast-forward against the
 * session's other events (ADR-019) instead of skipping time or ending the
 * run; RAMEND and the flash size come from the model; the GDB target
 * description is a constant. */
#include <stdio.h>
#include <string.h>

#include "mcu/avr/avr.h"
#include "mcu/avr/cost.h"

typedef int8_t s8;

/* SREG's bits */
enum { SREG_C = 1, SREG_Z = 2, SREG_N = 4, SREG_V = 8, SREG_S = 16, SREG_H = 32, SREG_T = 64,
       SREG_I = 128 };

struct avr_irq {
    int vec;
    int (*pending)(void *ctx, int vec);
    void (*ack)(void *ctx, int vec);
    void *ctx;
};

#define AVR_IRQS 24

struct avr_model {
    const char *name;
    u32 flash_words;
    u16 ramend;
};

static const struct avr_model models[] = {
    { "atmega328p", 0x4000, 0x08ff },
    { "atmega328", 0x4000, 0x08ff },
    { "atmega168", 0x2000, 0x04ff },
    { "atmega88", 0x1000, 0x04ff },
};

typedef struct esk_avr {
    struct esk_cpu cpu; /* the interface; first */
    struct esk_mach *sim;
    const struct avr_model *model;
    u32 flash_words;
    u8 r[32];
    u8 sreg;
    u16 sp;
    u32 pc;  /* in words */
    u32 npc;
    int inhibit;  /* SEI's and RETI's next instruction runs first */
    int sleeping; /* in SLEEP: waking costs 4 cycles more */
    int changed;  /* the instruction changed state */
    struct avr_irq src[AVR_IRQS];
    int nsrc;
    u32 sw_pend; /* vectors pended through the CPU interface */
    int pend;    /* the lowest vector requested, or 0 */
    u64 skipped; /* instructions a skip passed over */
} esk_avr;

static const struct esk_cpu_ops avr_ops;

int esk_avr_is(const struct esk_cpu *c) { return c && c->ops == &avr_ops; }

/* ---- the data space -------------------------------------------------------- */

static struct esk_bus *bus(esk_avr *as) { return &as->sim->bus; }

/* a debugger's watchpoint on a data access: the instruction completes and
 * the run stops after it */
static void watched(esk_avr *as, u16 a, int write)
{
    struct esk_bus *b = bus(as);
    if (b->nwatch)
        (void)esk_bus_watch_check(b, ESK_AVR_DATA + a, 1, write);
}

static u8 dread(esk_avr *as, u16 a)
{
    struct esk_region *r;
    u32 v = 0;
    watched(as, a, 0);
    if (a < 0x20)
        return as->r[a];
    switch (a) {
    case 0x5d: return (u8)as->sp;
    case 0x5e: return (u8)(as->sp >> 8);
    case 0x5f: return as->sreg;
    }
    r = esk_bus_region(bus(as), ESK_AVR_DATA + a, 1);
    if (r)
        return r->mem[ESK_AVR_DATA + a - r->base];
    if (esk_bus_read(bus(as), ESK_AVR_DATA + a, 1, &v)) {
        esk_mach_bus_error(as->sim, ESK_AVR_DATA + a, 1, 0);
        return 0;
    }
    return (u8)v;
}

static void dwrite(esk_avr *as, u16 a, u8 v)
{
    struct esk_region *r;
    watched(as, a, 1);
    as->changed = 1;
    if (a < 0x20) {
        as->r[a] = v;
        return;
    }
    switch (a) {
    case 0x5d: as->sp = (u16)((as->sp & 0xff00) | v); return;
    case 0x5e: as->sp = (u16)((as->sp & 0x00ff) | v << 8); return;
    case 0x5f:
        as->sreg = v;
        esk_avr_irq_changed(as->sim);
        return;
    }
    r = esk_bus_region(bus(as), ESK_AVR_DATA + a, 1);
    if (r) {
        if (!r->rom)
            r->mem[ESK_AVR_DATA + a - r->base] = v;
        return;
    }
    if (esk_bus_write(bus(as), ESK_AVR_DATA + a, 1, v))
        esk_mach_bus_error(as->sim, ESK_AVR_DATA + a, 1, 1);
}

static u8 flash_byte(esk_avr *as, u32 a)
{
    struct esk_region *r = esk_bus_region(bus(as), a, 1);
    u32 v = 0;
    if (r)
        return r->mem[a - r->base];
    return esk_bus_read(bus(as), a, 1, &v) ? 0xff : (u8)v;
}

static u16 flash_word(esk_avr *as, u32 w)
{
    u32 a = (w % as->flash_words) * 2;
    return (u16)(flash_byte(as, a) | flash_byte(as, a + 1) << 8);
}

static void push(esk_avr *as, u8 v)
{
    dwrite(as, as->sp, v);
    as->sp--;
}

static u8 pop(esk_avr *as)
{
    as->sp++;
    return dread(as, as->sp);
}

/* the return address: big-endian on the stack, the low byte pushed first */
static void push_pc(esk_avr *as, u32 pc)
{
    push(as, (u8)pc);
    push(as, (u8)(pc >> 8));
}

static u32 pop_pc(esk_avr *as)
{
    u32 hi = pop(as);
    return (hi << 8 | pop(as)) % as->flash_words;
}

/* ---- interrupts --------------------------------------------------------- */

static esk_avr *core_of(struct esk_mach *m)
{
    return esk_avr_is(m->cpu) ? (esk_avr *)m->cpu : NULL;
}

void esk_avr_irq_source(struct esk_mach *m, int vec, int (*pending)(void *ctx, int vec),
                        void (*ack)(void *ctx, int vec), void *ctx)
{
    esk_avr *k = core_of(m);
    if (!k || k->nsrc == AVR_IRQS || vec <= 0 || vec >= ESK_AVR_VECTORS)
        return;
    k->src[k->nsrc].vec = vec;
    k->src[k->nsrc].pending = pending;
    k->src[k->nsrc].ack = ack;
    k->src[k->nsrc].ctx = ctx;
    k->nsrc++;
}

void esk_avr_irq_changed(struct esk_mach *m)
{
    esk_avr *k = core_of(m);
    int best = 0, v, i;
    if (!k)
        return;
    for (v = 1; v < ESK_AVR_VECTORS && !best; v++)
        if (k->sw_pend >> v & 1)
            best = v;
    for (i = 0; i < k->nsrc; i++) {
        struct avr_irq *q = &k->src[i];
        if ((!best || q->vec < best) && q->pending(q->ctx, q->vec))
            best = q->vec;
    }
    k->pend = best;
    if (m->asleep && best && (k->sreg & SREG_I))
        m->asleep = 0;
}

/* the interrupt's entry: the return address pushed, I cleared, the
 * vector's flag cleared, and 4 cycles (4 more from SLEEP) */
static void take_interrupt(esk_avr *as)
{
    struct esk_mach *s = as->sim;
    int v = as->pend, i;
    u32 c = as->sleeping ? 8 : 4;
    as->sleeping = 0;
    push_pc(as, as->pc);
    as->sreg &= (u8)~SREG_I;
    as->pc = (u32)v * 2;
    as->sw_pend &= ~(1u << v);
    for (i = 0; i < as->nsrc; i++)
        if (as->src[i].vec == v && as->src[i].ack)
            as->src[i].ack(as->src[i].ctx, v);
    esk_avr_irq_changed(s);
    s->cycles += c;
    if (s->counting)
        esk_mach_advance(s, c);
}

/* ---- flags ---------------------------------------------------------------- */

#define B(x, n) (((x) >> (n)) & 1)
#define NB(x, n) (B(x, n) ^ 1)

static void set_flags(esk_avr *as, u8 mask, u8 v)
{
    as->sreg = (u8)((as->sreg & ~mask) | (v & mask));
}

/* N, Z, S from the result; V given */
static u8 nzs(u8 r, int v)
{
    u8 f = 0;
    if (r & 0x80)
        f |= SREG_N;
    if (!r)
        f |= SREG_Z;
    if (v)
        f |= SREG_V;
    if (B(r, 7) ^ (v != 0))
        f |= SREG_S;
    return f;
}

static u8 add8(esk_avr *as, u8 d, u8 s, int c)
{
    u8 r = (u8)(d + s + c);
    int h = (B(d, 3) & B(s, 3)) | (B(s, 3) & NB(r, 3)) | (NB(r, 3) & B(d, 3));
    int cy = (B(d, 7) & B(s, 7)) | (B(s, 7) & NB(r, 7)) | (NB(r, 7) & B(d, 7));
    int v = (B(d, 7) & B(s, 7) & NB(r, 7)) | (NB(d, 7) & NB(s, 7) & B(r, 7));
    set_flags(as, SREG_H | SREG_S | SREG_V | SREG_N | SREG_Z | SREG_C,
              (u8)(nzs(r, v) | (h ? SREG_H : 0) | (cy ? SREG_C : 0)));
    return r;
}

/* SUB, SUBI, CP, CPI (keep_z 0); SBC, SBCI, CPC (keep_z 1: Z only stays) */
static u8 sub8(esk_avr *as, u8 d, u8 s, int c, int keep_z)
{
    u8 r = (u8)(d - s - c);
    int h = (NB(d, 3) & B(s, 3)) | (B(s, 3) & B(r, 3)) | (B(r, 3) & NB(d, 3));
    int cy = (NB(d, 7) & B(s, 7)) | (B(s, 7) & B(r, 7)) | (B(r, 7) & NB(d, 7));
    int v = (B(d, 7) & NB(s, 7) & NB(r, 7)) | (NB(d, 7) & B(s, 7) & B(r, 7));
    u8 f = (u8)(nzs(r, v) | (h ? SREG_H : 0) | (cy ? SREG_C : 0));
    if (keep_z && !(as->sreg & SREG_Z))
        f &= (u8)~SREG_Z;
    set_flags(as, SREG_H | SREG_S | SREG_V | SREG_N | SREG_Z | SREG_C, f);
    return r;
}

static u8 logic(esk_avr *as, u8 r)
{
    set_flags(as, SREG_S | SREG_V | SREG_N | SREG_Z, nzs(r, 0));
    return r;
}

/* ASR, LSR, ROR: C from bit 0, V = N ^ C */
static u8 shift_right(esk_avr *as, u8 d, u8 top)
{
    u8 r = (u8)(d >> 1 | top);
    int c = d & 1, n = B(r, 7);
    set_flags(as, SREG_S | SREG_V | SREG_N | SREG_Z | SREG_C, (u8)(nzs(r, n ^ c) | (c ? SREG_C : 0)));
    return r;
}

/* the multiplies: R1:R0, C from bit 15, Z; FMUL's shifted left once */
static void mul_result(esk_avr *as, u32 p, int fractional)
{
    u16 r = (u16)p;
    int c = B(r, 15);
    if (fractional)
        r = (u16)(r << 1);
    as->r[0] = (u8)r;
    as->r[1] = (u8)(r >> 8);
    set_flags(as, SREG_Z | SREG_C, (u8)((r ? 0 : SREG_Z) | (c ? SREG_C : 0)));
}

static int is_two_word(u16 w)
{
    return (w & 0xfe0e) == 0x940c || (w & 0xfe0e) == 0x940e || /* jmp call */
           (w & 0xfc0f) == 0x9000;                             /* lds sts */
}

/* ---- the instructions -------------------------------------------------------- */

static int unsupported(esk_avr *as, u16 w)
{
    esk_mach_end(as->sim, ESK_MACH_LOCKUP, "0x%04x at 0x%04x is not an %s instruction", w,
                 (unsigned)(as->pc * 2), as->model->name);
    return 0;
}

/* skip the next instruction: its cycles, and the count of the skipped */
static void skip(esk_avr *as)
{
    u16 next = flash_word(as, as->npc);
    int n = is_two_word(next) ? 2 : 1;
    as->npc += (u32)n;
    as->sim->cycles += (u32)n;
    as->skipped++;
}

static u16 rp(const esk_avr *as, int n) { return (u16)(as->r[n] | as->r[n + 1] << 8); }

static void set_rp(esk_avr *as, int n, u16 v)
{
    as->r[n] = (u8)v;
    as->r[n + 1] = (u8)(v >> 8);
}

/* LD and ST through X, Y or Z: mode 0 plain, 1 post-increment, 2
 * pre-decrement */
static void ldst(esk_avr *as, int ptr, int mode, int reg, int store)
{
    u16 a = rp(as, ptr);
    if (mode == 2)
        a--;
    if (store)
        dwrite(as, a, as->r[reg]);
    else
        as->r[reg] = dread(as, a);
    if (mode == 1)
        a++;
    if (mode)
        set_rp(as, ptr, a);
}

static void branch_to(esk_avr *as, s32 k)
{
    as->npc = (u32)((s32)as->pc + 1 + k) % as->flash_words;
}

/* SLEEP with SMCR.SE: the core waits for an interrupt. The runtime
 * fast-forwards to the device that will request one; with I clear nothing
 * wakes it but a reset. */
static void do_sleep(esk_avr *as)
{
    if (!(dread(as, 0x53) & 1))
        return;
    if ((as->sreg & SREG_I) && as->pend)
        return;
    as->sleeping = 1;
    as->sim->asleep = 1;
}

/* 1 when the instruction ran; 0 when it ended the run */
static int exec(esk_avr *as, u16 w, u16 w2)
{
    int d = (w >> 4) & 31, r = (w & 15) | ((w >> 5) & 16);
    u8 K = (u8)(((w >> 4) & 0xf0) | (w & 15));
    int dh = 16 + ((w >> 4) & 15); /* r16..r31 forms */
    switch (w >> 12) {
    case 0x0:
        if (w == 0)
            return 1; /* NOP */
        switch ((w >> 8) & 0xf) {
        case 0x1: /* MOVW */
            set_rp(as, ((w >> 4) & 15) * 2, rp(as, (w & 15) * 2));
            return 1;
        case 0x2: /* MULS */
            mul_result(as, (u32)((s8)as->r[16 + ((w >> 4) & 15)] * (s8)as->r[16 + (w & 15)]), 0);
            return 1;
        case 0x3: { /* MULSU FMUL FMULS FMULSU */
            int a = 16 + ((w >> 4) & 7), b = 16 + (w & 7);
            s32 p;
            switch (w & 0x88) {
            case 0x00: p = (s8)as->r[a] * (s32)as->r[b]; break;
            case 0x08: p = (s32)as->r[a] * (s32)as->r[b]; break;
            case 0x80: p = (s8)as->r[a] * (s8)as->r[b]; break;
            default: p = (s8)as->r[a] * (s32)as->r[b]; break;
            }
            mul_result(as, (u32)p, (w & 0x88) != 0);
            return 1;
        }
        }
        switch ((w >> 10) & 3) {
        case 1: sub8(as, as->r[d], as->r[r], as->sreg & SREG_C, 1); return 1;               /* CPC */
        case 2: as->r[d] = sub8(as, as->r[d], as->r[r], as->sreg & SREG_C, 1); return 1;    /* SBC */
        case 3: as->r[d] = add8(as, as->r[d], as->r[r], 0); return 1;                       /* ADD */
        }
        return unsupported(as, w);
    case 0x1:
        switch ((w >> 10) & 3) {
        case 0: /* CPSE */
            if (as->r[d] == as->r[r])
                skip(as);
            return 1;
        case 1: sub8(as, as->r[d], as->r[r], 0, 0); return 1;                               /* CP */
        case 2: as->r[d] = sub8(as, as->r[d], as->r[r], 0, 0); return 1;                    /* SUB */
        default: as->r[d] = add8(as, as->r[d], as->r[r], as->sreg & SREG_C); return 1;      /* ADC */
        }
    case 0x2:
        switch ((w >> 10) & 3) {
        case 0: as->r[d] = logic(as, as->r[d] & as->r[r]); return 1; /* AND */
        case 1: as->r[d] = logic(as, as->r[d] ^ as->r[r]); return 1; /* EOR */
        case 2: as->r[d] = logic(as, as->r[d] | as->r[r]); return 1; /* OR */
        default: as->r[d] = as->r[r]; return 1;                      /* MOV */
        }
    case 0x3: sub8(as, as->r[dh], K, 0, 0); return 1;                               /* CPI */
    case 0x4: as->r[dh] = sub8(as, as->r[dh], K, as->sreg & SREG_C, 1); return 1;   /* SBCI */
    case 0x5: as->r[dh] = sub8(as, as->r[dh], K, 0, 0); return 1;                   /* SUBI */
    case 0x6: as->r[dh] = logic(as, as->r[dh] | K); return 1;                       /* ORI */
    case 0x7: as->r[dh] = logic(as, as->r[dh] & K); return 1;                       /* ANDI */
    case 0x8: case 0xa: { /* LDD, STD (Y+q, Z+q) */
        int q = (w & 7) | ((w >> 7) & 0x18) | ((w >> 8) & 0x20);
        u16 a = (u16)(rp(as, w & 8 ? 28 : 30) + q);
        if (w & 0x200)
            dwrite(as, a, as->r[d]);
        else
            as->r[d] = dread(as, a);
        return 1;
    }
    case 0xb: /* IN, OUT */
        if (w & 0x800)
            dwrite(as, (u16)(0x20 + ((w & 15) | ((w >> 5) & 0x30))), as->r[d]);
        else
            as->r[d] = dread(as, (u16)(0x20 + ((w & 15) | ((w >> 5) & 0x30))));
        return 1;
    case 0xc: case 0xd: { /* RJMP, RCALL */
        s32 k = (s32)((u32)(w & 0xfff) << 20) >> 20;
        if (w & 0x1000)
            push_pc(as, as->npc);
        branch_to(as, k);
        return 1;
    }
    case 0xe: as->r[dh] = K; return 1; /* LDI */
    case 0xf: {
        int b = w & 7;
        s32 k = (s32)((u32)((w >> 3) & 0x7f) << 25) >> 25;
        switch ((w >> 9) & 7) {
        case 0: case 1: /* BRBS */
            if (as->sreg >> b & 1) {
                branch_to(as, k);
                as->sim->cycles++;
            }
            return 1;
        case 2: case 3: /* BRBC */
            if (!(as->sreg >> b & 1)) {
                branch_to(as, k);
                as->sim->cycles++;
            }
            return 1;
        case 4: /* BLD */
            if (w & 8)
                return unsupported(as, w);
            as->r[d] = (u8)((as->r[d] & ~(1u << b)) | ((as->sreg & SREG_T) ? 1u << b : 0));
            return 1;
        case 5: /* BST */
            if (w & 8)
                return unsupported(as, w);
            set_flags(as, SREG_T, as->r[d] >> b & 1 ? SREG_T : 0);
            return 1;
        default: /* SBRC, SBRS */
            if (w & 8)
                return unsupported(as, w);
            if ((as->r[d] >> b & 1) == ((w >> 9) & 1))
                skip(as);
            return 1;
        }
    }
    }
    /* 1001 */
    switch ((w >> 8) & 0xf) {
    case 0x0: case 0x1: /* loads */
        switch (w & 15) {
        case 0x0: as->r[d] = dread(as, w2); return 1; /* LDS */
        case 0x1: ldst(as, 30, 1, d, 0); return 1;
        case 0x2: ldst(as, 30, 2, d, 0); return 1;
        case 0x4: case 0x5: { /* LPM Rd, Z[+] */
            u16 z = rp(as, 30);
            as->r[d] = flash_byte(as, z);
            if (w & 1)
                set_rp(as, 30, (u16)(z + 1));
            return 1;
        }
        case 0x9: ldst(as, 28, 1, d, 0); return 1;
        case 0xa: ldst(as, 28, 2, d, 0); return 1;
        case 0xc: ldst(as, 26, 0, d, 0); return 1;
        case 0xd: ldst(as, 26, 1, d, 0); return 1;
        case 0xe: ldst(as, 26, 2, d, 0); return 1;
        case 0xf: as->r[d] = pop(as); return 1; /* POP */
        }
        return unsupported(as, w);
    case 0x2: case 0x3: /* stores */
        switch (w & 15) {
        case 0x0: dwrite(as, w2, as->r[d]); return 1; /* STS */
        case 0x1: ldst(as, 30, 1, d, 1); return 1;
        case 0x2: ldst(as, 30, 2, d, 1); return 1;
        case 0x9: ldst(as, 28, 1, d, 1); return 1;
        case 0xa: ldst(as, 28, 2, d, 1); return 1;
        case 0xc: ldst(as, 26, 0, d, 1); return 1;
        case 0xd: ldst(as, 26, 1, d, 1); return 1;
        case 0xe: ldst(as, 26, 2, d, 1); return 1;
        case 0xf: push(as, as->r[d]); return 1; /* PUSH */
        }
        return unsupported(as, w);
    case 0x4: case 0x5:
        if ((w & 0xfe0c) == 0x940c) { /* JMP, CALL */
            u32 k = ((u32)(((w >> 3) & 0x3e) | (w & 1)) << 16 | w2);
            if (w & 2)
                push_pc(as, as->npc);
            as->npc = k % as->flash_words;
            return 1;
        }
        switch (w & 15) {
        case 0x0: /* COM */
            as->r[d] = logic(as, (u8)~as->r[d]);
            set_flags(as, SREG_C, SREG_C);
            return 1;
        case 0x1: { /* NEG */
            u8 x = as->r[d], v = (u8)(0 - x);
            u8 f = nzs(v, v == 0x80);
            if (B(v, 3) | B(x, 3))
                f |= SREG_H;
            if (v)
                f |= SREG_C;
            set_flags(as, SREG_H | SREG_S | SREG_V | SREG_N | SREG_Z | SREG_C, f);
            as->r[d] = v;
            return 1;
        }
        case 0x2: /* SWAP */
            as->r[d] = (u8)(as->r[d] << 4 | as->r[d] >> 4);
            return 1;
        case 0x3: { /* INC */
            u8 v = (u8)(as->r[d] + 1);
            set_flags(as, SREG_S | SREG_V | SREG_N | SREG_Z, nzs(v, v == 0x80));
            as->r[d] = v;
            return 1;
        }
        case 0x5: as->r[d] = shift_right(as, as->r[d], as->r[d] & 0x80); return 1; /* ASR */
        case 0x6: as->r[d] = shift_right(as, as->r[d], 0); return 1;                /* LSR */
        case 0x7: /* ROR */
            as->r[d] = shift_right(as, as->r[d], (as->sreg & SREG_C) ? 0x80 : 0);
            return 1;
        case 0xa: { /* DEC */
            u8 v = (u8)(as->r[d] - 1);
            set_flags(as, SREG_S | SREG_V | SREG_N | SREG_Z, nzs(v, v == 0x7f));
            as->r[d] = v;
            return 1;
        }
        case 0x8:
            if ((w & 0xff0f) == 0x9408) { /* BSET, BCLR */
                u8 m = (u8)(1u << ((w >> 4) & 7));
                set_flags(as, m, (w & 0x80) ? 0 : m);
                if (m == SREG_I && !(w & 0x80))
                    as->inhibit = 1; /* SEI: one more instruction first */
                esk_avr_irq_changed(as->sim);
                return 1;
            }
            switch (w) {
            case 0x9508: /* RET */
                as->npc = pop_pc(as);
                return 1;
            case 0x9518: /* RETI */
                as->npc = pop_pc(as);
                as->sreg |= SREG_I;
                as->inhibit = 1;
                esk_avr_irq_changed(as->sim);
                return 1;
            case 0x9588: /* SLEEP */
                do_sleep(as);
                return 1;
            case 0x9598: /* BREAK: a NOP without on-chip debug */
            case 0x95a8: /* WDR */
                return 1;
            case 0x95c8: /* LPM */
                as->r[0] = flash_byte(as, rp(as, 30));
                return 1;
            }
            return unsupported(as, w);
        case 0x9:
            if (w == 0x9409) { /* IJMP */
                as->npc = rp(as, 30) % as->flash_words;
                return 1;
            }
            if (w == 0x9509) { /* ICALL */
                push_pc(as, as->npc);
                as->npc = rp(as, 30) % as->flash_words;
                return 1;
            }
            return unsupported(as, w);
        }
        return unsupported(as, w);
    case 0x6: case 0x7: { /* ADIW, SBIW */
        int p = 24 + 2 * ((w >> 4) & 3);
        u16 x = rp(as, p), k = (u16)((w & 15) | ((w >> 2) & 0x30)), v;
        int c, vf;
        u8 f;
        if (w & 0x100) {
            v = (u16)(x - k);
            vf = B(x, 15) & NB(v, 15);
            c = B(v, 15) & NB(x, 15);
        } else {
            v = (u16)(x + k);
            vf = NB(x, 15) & B(v, 15);
            c = NB(v, 15) & B(x, 15);
        }
        f = (u8)((B(v, 15) ? SREG_N : 0) | (v ? 0 : SREG_Z) | (vf ? SREG_V : 0) | (c ? SREG_C : 0) |
                 ((B(v, 15) ^ vf) ? SREG_S : 0));
        set_flags(as, SREG_S | SREG_V | SREG_N | SREG_Z | SREG_C, f);
        set_rp(as, p, v);
        return 1;
    }
    case 0x8: case 0xa: { /* CBI, SBI */
        u16 a = (u16)(0x20 + ((w >> 3) & 31));
        u8 m = (u8)(1u << (w & 7)), v = dread(as, a);
        /* the bit alone, as on the part: PINx (a one toggles PORTx's bit)
         * and the flag registers (a one clears the flag) see only it */
        if (a == 0x23 || a == 0x26 || a == 0x29 || (a >= 0x35 && a <= 0x37) || a == 0x3b ||
            a == 0x3c)
            dwrite(as, a, (w & 0x200) ? m : 0);
        else
            dwrite(as, a, (w & 0x200) ? (u8)(v | m) : (u8)(v & ~m));
        return 1;
    }
    case 0x9: case 0xb: { /* SBIC, SBIS */
        u16 a = (u16)(0x20 + ((w >> 3) & 31));
        if ((dread(as, a) >> (w & 7) & 1) == ((w >> 9) & 1))
            skip(as);
        return 1;
    }
    default: /* MUL */
        mul_result(as, (u32)as->r[d] * as->r[r], 0);
        return 1;
    }
}

/* ---- the run --------------------------------------------------------------- */

static int jumps(u16 w)
{
    return (w >> 12) == 0xc || (w & 0xfe0e) == 0x940c || (w >> 11) == 0x1e;
}

static void step(struct esk_cpu *c)
{
    esk_avr *as = (esk_avr *)c;
    struct esk_mach *s = as->sim;
    u16 w, w2 = 0;
    int size = 1, br;
    u64 c0;
    if (as->pend && (as->sreg & SREG_I) && !as->inhibit) {
        take_interrupt(as);
        return;
    }
    as->inhibit = 0;
    w = flash_word(as, as->pc);
    if (is_two_word(w)) {
        w2 = flash_word(as, as->pc + 1);
        size = 2;
    }
    c0 = s->cycles;
    s->insns++;
    s->cycles += (u32)esk_avr_cost(w, &br);
    as->npc = (as->pc + (u32)size) % as->flash_words;
    as->changed = 0;
    if (!exec(as, w, w2))
        return;
    if (s->counting)
        esk_mach_advance(s, (u32)(s->cycles - c0));
    if (as->npc == as->pc && !as->changed && s->state == ESK_MACH_RUN && jumps(w)) {
        /* a jump to itself waits, as SLEEP does, for an interrupt */
        if (!((as->sreg & SREG_I) && as->pend))
            s->asleep = 1;
    }
    as->pc = as->npc;
}

/* ---- the CPU interface ------------------------------------------------------- */

/* Power-on: the registers zero (the part leaves them undefined; QEMU
 * clears them), SREG 0, SP at RAMEND, the program counter at the reset
 * vector. */
static void cpu_reset(struct esk_cpu *c)
{
    esk_avr *k = (esk_avr *)c;
    memset(k->r, 0, sizeof k->r);
    k->sreg = 0;
    k->sp = k->model->ramend;
    k->pc = k->npc = 0;
    k->inhibit = k->sleeping = 0;
    k->sw_pend = 0;
    k->pend = 0;
    k->skipped = 0;
}

static u32 cpu_pc(struct esk_cpu *c) { return ((esk_avr *)c)->pc * 2; }

static void cpu_interrupt(struct esk_cpu *c, int n)
{
    esk_avr *k = (esk_avr *)c;
    /* the register file's numbering is ARM's (16 + k); AVR vectors are k */
    if (n >= 16)
        n -= 16;
    if (n > 0 && n < ESK_AVR_VECTORS) {
        k->sw_pend |= 1u << n;
        esk_avr_irq_changed(k->sim);
    }
}

static int cpu_wake_pending(struct esk_cpu *c)
{
    const esk_avr *k = (const esk_avr *)c;
    return (k->sreg & SREG_I) && k->pend;
}

/* ---- the registers, as GDB numbers them ---------------------------------------
 *
 * QEMU's stub and GDB's AVR target: r0-r31 are 0-31 (a byte each), SREG
 * 32, SP 33 (two bytes) and the PC 34 (four bytes, a byte address), all
 * in the `g` packet. */

static int cpu_reg_read(struct esk_cpu *c, int n, u8 *buf)
{
    esk_avr *k = (esk_avr *)c;
    int i;
    if (n >= 0 && n < 32) {
        buf[0] = k->r[n];
        return 1;
    }
    switch (n) {
    case 32: buf[0] = k->sreg; return 1;
    case 33: buf[0] = (u8)k->sp; buf[1] = (u8)(k->sp >> 8); return 2;
    case 34: {
        u32 pc = k->pc * 2;
        for (i = 0; i < 4; i++)
            buf[i] = (u8)(pc >> (8 * i));
        return 4;
    }
    }
    return 0;
}

static int cpu_reg_write(struct esk_cpu *c, int n, const u8 *buf)
{
    esk_avr *k = (esk_avr *)c;
    if (n >= 0 && n < 32) {
        k->r[n] = buf[0];
        return 1;
    }
    switch (n) {
    case 32:
        k->sreg = buf[0];
        esk_avr_irq_changed(k->sim);
        return 1;
    case 33: k->sp = (u16)(buf[0] | buf[1] << 8); return 2;
    case 34: {
        u32 pc = (u32)buf[0] | (u32)buf[1] << 8 | (u32)buf[2] << 16 | (u32)buf[3] << 24;
        k->pc = (pc / 2) % k->flash_words;
        return 4;
    }
    }
    return 0;
}

static const int g_regs[] = { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
                              18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, -1 };

static const int *cpu_gdb_g_regs(struct esk_cpu *c)
{
    ES_UNUSED(c);
    return g_regs;
}

#define AVR_REG(n) "  <reg name=\"r" #n "\" bitsize=\"8\" type=\"int\"/>\n"

/* QEMU's description: a target.xml that includes avr-cpu.xml, GDB's own
 * file (with its sizes for sreg, sp and pc, which GDB's AVR target
 * overrides) */
static const char avr_cpu_xml[] =
    "<?xml version=\"1.0\"?>\n"
    "<!DOCTYPE feature SYSTEM \"gdb-target.dtd\">\n"
    "<feature name=\"org.gnu.gdb.avr.cpu\">\n"
    "  <reg name=\"r0\" bitsize=\"8\" type=\"int\" regnum=\"0\"/>\n" AVR_REG(1) AVR_REG(2) AVR_REG(3)
        AVR_REG(4) AVR_REG(5) AVR_REG(6) AVR_REG(7) AVR_REG(8) AVR_REG(9) AVR_REG(10) AVR_REG(11)
            AVR_REG(12) AVR_REG(13) AVR_REG(14) AVR_REG(15) AVR_REG(16) AVR_REG(17) AVR_REG(18)
                AVR_REG(19) AVR_REG(20) AVR_REG(21) AVR_REG(22) AVR_REG(23) AVR_REG(24) AVR_REG(25)
                    AVR_REG(26) AVR_REG(27) AVR_REG(28) AVR_REG(29) AVR_REG(30) AVR_REG(31)
    "  <reg name=\"sreg\" bitsize=\"8\" type=\"int\"/>\n"
    "  <reg name=\"sp\" bitsize=\"8\" type=\"int\"/>\n"
    "  <reg name=\"pc\" bitsize=\"8\" type=\"int\"/>\n"
    "</feature>\n";

static const char *cpu_gdb_xml(struct esk_cpu *c, const char *annex)
{
    ES_UNUSED(c);
    if (!strcmp(annex, "target.xml"))
        return "<?xml version=\"1.0\"?><!DOCTYPE target SYSTEM \"gdb-target.dtd\"><target>"
               "<xi:include href=\"avr-cpu.xml\"/></target>";
    return strcmp(annex, "avr-cpu.xml") ? 0 : avr_cpu_xml;
}

static const char *cpu_reg_name(struct esk_cpu *c, int n)
{
    static const char *const names[35] = {
        "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",  "r8",  "r9",  "r10", "r11",
        "r12", "r13", "r14", "r15", "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
        "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31", "sreg", "sp", "pc",
    };
    ES_UNUSED(c);
    return n >= 0 && n < 35 ? names[n] : 0;
}

static int cpu_disasm(struct esk_cpu *c, u32 addr, char *text, size_t cap)
{
    esk_avr *k = (esk_avr *)c;
    u32 w = 0, w2 = 0;
    if (esk_bus_debug_read(&k->sim->bus, addr, 2, &w) != 0)
        return 0;
    (void)esk_bus_debug_read(&k->sim->bus, addr + 2, 2, &w2);
    return esk_avr_disasm(addr, w, w2, text, cap);
}

/* The register file and SP and SREG in the data space, for a debugger
 * (the core's own accesses do not come through the bus for these). */
static u32 regs_read(void *ctx, u32 off, int n)
{
    esk_avr *k = (esk_avr *)ctx;
    u32 v = 0;
    int i;
    for (i = n - 1; i >= 0; i--)
        v = v << 8 | (off + (u32)i < 32 ? k->r[off + (u32)i] : 0);
    return v;
}

static void regs_write(void *ctx, u32 off, int n, u32 v)
{
    esk_avr *k = (esk_avr *)ctx;
    int i;
    for (i = 0; i < n; i++)
        if (off + (u32)i < 32)
            k->r[off + (u32)i] = (u8)(v >> (8 * i));
}

static u32 spsreg_read(void *ctx, u32 off, int n)
{
    esk_avr *k = (esk_avr *)ctx;
    u8 b[3];
    u32 v = 0;
    int i;
    b[0] = (u8)k->sp;
    b[1] = (u8)(k->sp >> 8);
    b[2] = k->sreg;
    for (i = n - 1; i >= 0; i--)
        v = v << 8 | (off + (u32)i < 3 ? b[off + (u32)i] : 0);
    return v;
}

static void spsreg_write(void *ctx, u32 off, int n, u32 v)
{
    esk_avr *k = (esk_avr *)ctx;
    int i;
    for (i = 0; i < n; i++) {
        u8 x = (u8)(v >> (8 * i));
        switch (off + (u32)i) {
        case 0: k->sp = (u16)((k->sp & 0xff00) | x); break;
        case 1: k->sp = (u16)((k->sp & 0x00ff) | x << 8); break;
        case 2: k->sreg = x; esk_avr_irq_changed(k->sim); break;
        }
    }
}

static const struct esk_dev_ops regs_ops = { "avr-regs", regs_read, regs_write, 0, 0, 0 };
static const struct esk_dev_ops spsreg_ops = { "avr-sp-sreg", spsreg_read, spsreg_write, 0, 0, 0 };

static const struct esk_cpu_ops avr_ops = {
    "avr", 83, "an AVR image",
    cpu_reset, step, cpu_pc, cpu_reg_read, cpu_reg_write,
    cpu_gdb_g_regs, cpu_gdb_xml, 2, cpu_interrupt, 34, 1, 0, cpu_reg_name, cpu_wake_pending,
    cpu_disasm,
};

/* the core, with its own devices -- the registers, SP and SREG as the data
 * space has them, ahead of the register file */
es_status esk_avr_create(struct esk_mach *m, const char *model, struct esk_cpu **out)
{
    const struct avr_model *md = 0;
    esk_avr *k;
    size_t i;
    for (i = 0; i < sizeof models / sizeof models[0]; i++)
        if (!strcmp(models[i].name, model))
            md = &models[i];
    if (!md)
        return ES_ENOTFOUND;
    k = (esk_avr *)esk_mach_alloc(m, sizeof *k);
    if (!k)
        return ES_ENOMEM;
    k->cpu.ops = &avr_ops;
    k->sim = m;
    k->model = md;
    k->flash_words = md->flash_words;
    k->sp = md->ramend;
    m->cpu = &k->cpu;
    if (esk_bus_add_device(&m->bus, ESK_AVR_DATA, 0x20, &regs_ops, k) != ES_OK ||
        esk_bus_add_device(&m->bus, ESK_AVR_DATA + 0x5d, 3, &spsreg_ops, k) != ES_OK)
        return ES_ELIMIT;
    *out = &k->cpu;
    return ES_OK;
}
