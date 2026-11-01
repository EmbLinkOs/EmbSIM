/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* analysis.c -- the step the analyses watch, and the shadow stacks of the
 * program's calls (analysis.h).
 *
 * A call and a return are recognized by the instruction, as each core
 * executes it:
 *   - Cortex-M: BL and BLX are calls; BX, POP and LDM with the pc, LDR to
 *     the pc and MOV to the pc may be returns;
 *   - RISC-V: JAL and JALR that link (rd ra or t0) are calls, C.JAL and
 *     C.JALR too; JALR and C.JR that do not link may be returns;
 *   - AVR: CALL, RCALL and ICALL are calls; RET is a return;
 *   - PIC24: CALL, RCALL, CALL Wn and RCALL Wn are calls; RETURN and
 *     RETLW returns (RETFIE is the core's esk_mach_exc_return);
 *   - MIPS: JAL, JALR that links, BAL and the other linking branches are
 *     calls; JR may be a return. Each transfers after its delay slot, so
 *     the step that makes the call or the return is the slot's: its
 *     instruction counts to the caller, and the frame is pushed (or popped)
 *     when the next pc is not the slot's fall-through. MIPS16e has JAL,
 *     JALX and JALR, the compact JALRC, and JR and JRC.
 * A call that was taken pushes a frame, which returns to the instruction
 * after it. A transfer that may be a return is one when it lands where a
 * frame returns to: that frame and every frame above it are popped, so a
 * tail call (a branch to another function, which then returns for both)
 * leaves the stack right. One that lands anywhere else is a jump (a
 * switch's table, a call through a pointer that does not link). Exception
 * entry, which no instruction shows, is the core's to tell (esk_an_exc),
 * and its return (esk_an_exc_return) pops back to it, wherever the handler
 * returns to.
 *
 * Each core of the machine has its own stack and its own root path, so a
 * second core's calls are its own (smp.c steps each core through here). */
#include <string.h>

#include "mcu/analysis.h"
#include "mcu/dwarf.h"
#include "mcu/mips/mips.h"
#include "mcu/pic24/pic24.h"
#include "mcu/riscv/riscv.h"

enum { AN_ARM, AN_RV, AN_AVR, AN_MIPS, AN_PIC24 };
enum { K_OTHER, K_CALL, K_RET };

/* ---- the instruction's kind ------------------------------------------- */

static int fetch16(struct esk_mach *m, struct esk_cpu *c, u32 a, u32 *h)
{
    u32 v;
    if (c->ops->phys)
        a = c->ops->phys(c, a);
    if (esk_bus_debug_read(&m->bus, a, 2, &v))
        return 0;
    *h = v & 0xffffu;
    return 1;
}

static int kind_thumb(struct esk_mach *m, struct esk_cpu *c, u32 pc, int *size)
{
    u32 h, h2;
    *size = 2;
    if (!fetch16(m, c, pc, &h))
        return K_OTHER;
    if ((h >> 11) >= 0x1d) {
        *size = 4;
        if (!fetch16(m, c, pc + 2, &h2))
            return K_OTHER;
        if ((h & 0xf800) == 0xf000 && (h2 & 0xd000) == 0xd000)
            return K_CALL;                              /* BL */
        if ((h & 0xffd0) == 0xe890 || (h & 0xffd0) == 0xe910)
            return h2 & 0x8000 ? K_RET : K_OTHER;       /* LDM with the pc */
        if ((h & 0xfff0) == 0xf8d0 || (h & 0xfff0) == 0xf850)
            return (h2 >> 12) == 15 ? K_RET : K_OTHER;  /* LDR pc */
        return K_OTHER;
    }
    if ((h & 0xff87) == 0x4780)
        return K_CALL;                                  /* BLX Rm */
    if ((h & 0xff87) == 0x4700 || (h & 0xff00) == 0xbd00 || (h & 0xff87) == 0x4687)
        return K_RET;                                   /* BX, POP {pc}, MOV pc */
    return K_OTHER;
}

static int kind_riscv(struct esk_mach *m, struct esk_cpu *c, u32 pc, int *size)
{
    u32 h, h2, q, f3, rs1, rs2;
    *size = 2;
    if (!fetch16(m, c, pc, &h))
        return K_OTHER;
    if ((h & 3) == 3) {
        u32 i, op, rd;
        int link;
        *size = 4;
        if (!fetch16(m, c, pc + 2, &h2))
            return K_OTHER;
        i = h | h2 << 16;
        op = i & 0x7f;
        rd = (i >> 7) & 31;
        link = rd == 1 || rd == 5;
        if (op == 0x6f)                                 /* JAL */
            return link ? K_CALL : K_OTHER;
        if (op == 0x67)                                 /* JALR */
            return link ? K_CALL : rd == 0 ? K_RET : K_OTHER;
        return K_OTHER;
    }
    q = h & 3;
    f3 = h >> 13;
    rs1 = (h >> 7) & 31;
    rs2 = (h >> 2) & 31;
    if (q == 1 && f3 == 1 && ((struct rv_state *)c)->xlen == 32)
        return K_CALL;                                  /* C.JAL */
    if (q == 2 && f3 == 4 && rs1 && !rs2)
        return (h >> 12) & 1 ? K_CALL : K_RET;          /* C.JALR, C.JR */
    return K_OTHER;
}

static int kind_avr(struct esk_mach *m, struct esk_cpu *c, u32 pc, int *size)
{
    u32 w;
    *size = 2;
    if (!fetch16(m, c, pc, &w))
        return K_OTHER;
    if ((w & 0xfe0e) == 0x940e) {                       /* CALL */
        *size = 4;
        return K_CALL;
    }
    if ((w & 0xfe0e) == 0x940c)                         /* JMP */
        *size = 4;
    if ((w & 0xf000) == 0xd000 || w == 0x9509)          /* RCALL, ICALL */
        return K_CALL;
    if (w == 0x9508)                                    /* RET */
        return K_RET;
    return K_OTHER;
}

static int kind_mips(struct esk_mach *m, struct esk_cpu *c, u32 pc, int *size)
{
    u32 h, h2, i, op, rt;
    *size = 4;
    if (!fetch16(m, c, pc, &h) || !fetch16(m, c, pc + 2, &h2))
        return K_OTHER;
    i = h | h2 << 16;
    op = i >> 26;
    rt = (i >> 16) & 31;
    if (op == 3)                                        /* JAL */
        return K_CALL;
    if (op == 1 && (rt & 0x1c) == 0x10)                 /* BLTZAL, BGEZAL(L), BAL */
        return K_CALL;
    if (op == 0 && (i & 0x3f) == 9)                     /* JALR */
        return (i >> 11) & 31 ? K_CALL : K_RET;
    if (op == 0 && (i & 0x3f) == 8)                     /* JR */
        return K_RET;
    return K_OTHER;
}

/* a MIPS16e instruction's kind and size; `compact`: it transfers at its
 * own step, having no delay slot */
static int kind_mips16(struct esk_mach *m, struct esk_cpu *c, u32 pc, int *size, int *compact)
{
    u32 h;
    *size = 2;
    *compact = 0;
    if (!fetch16(m, c, pc, &h))
        return K_OTHER;
    *size = mips16_size(h);
    if (((h >> 11) & 0x1f) == 0x03)                     /* JAL, JALX */
        return K_CALL;
    if (((h >> 11) & 0x1f) == 0x1d && (h & 0x1f) == 0) { /* JR, JRC, JALR, JALRC */
        *compact = (h >> 7) & 1;
        return (h >> 6) & 1 ? K_CALL : K_RET;
    }
    return K_OTHER;
}

/* a PIC24 instruction's kind, and its size in program addresses (2 a word) */
static int kind_pic24(struct esk_mach *m, u32 pc, int *size)
{
    u32 i = esk_pic24_prog(m, pc), op = i >> 16;
    *size = op == 0x02 || op == 0x04 ? 4 : 2;
    if (op == 0x02 || op == 0x07)                       /* CALL, RCALL */
        return K_CALL;
    if (op == 0x01 && ((i & 0xfff0) == 0x0000 || (i & 0xfff0) == 0x2000))
        return K_CALL;                                  /* CALL Wn, RCALL Wn */
    if (i == 0x060000 || op == 0x05)                    /* RETURN, RETLW */
        return K_RET;
    return K_OTHER;
}

/* An exception's entry lands on a jump to the handler where the vector
 * table holds instructions (the AVR's; RISC-V's vectored mode; MIPS's, a J
 * and its delay slot): the jump's target, or `pc` itself when it is not a
 * jump. */
static u32 vector_target(esk_an *a, struct esk_cpu *c, u32 pc)
{
    struct esk_mach *m = a->m;
    u32 h, h2;
    if (!fetch16(m, c, pc, &h))
        return pc;
    if (a->arch == AN_AVR) {
        if ((h & 0xfe0e) == 0x940c && fetch16(m, c, pc + 2, &h2))  /* JMP */
            return (((h >> 3 & 0x3e) | (h & 1)) << 16 | h2) * 2;
        if ((h & 0xf000) == 0xc000)                                 /* RJMP */
            return pc + 2 + (u32)((s32)((h & 0xfff) << 20) >> 19);
    } else if (a->arch == AN_MIPS) {
        if (fetch16(m, c, pc + 2, &h2) && (h2 >> 10) == 2)          /* J */
            return ((pc + 4) & 0xf0000000u) | ((h | h2 << 16) & 0x03ffffffu) << 2;
    } else if (a->arch == AN_RV) {
        if ((h & 0xfff) == 0x06f && fetch16(m, c, pc + 2, &h2)) {   /* JAL x0 */
            u32 i = h | h2 << 16;
            u32 off = (i >> 31) << 20 | ((i >> 12) & 0xff) << 12 | ((i >> 20) & 1) << 11 |
                      ((i >> 21) & 0x3ff) << 1;
            return pc + (u32)((s32)(off << 11) >> 11);
        }
        if ((h & 0xe003) == 0xa001) {                               /* C.J */
            u32 off = ((h >> 12) & 1) << 11 | ((h >> 11) & 1) << 4 | ((h >> 9) & 3) << 8 |
                      ((h >> 8) & 1) << 10 | ((h >> 7) & 1) << 6 | ((h >> 6) & 1) << 7 |
                      ((h >> 3) & 7) << 1 | ((h >> 2) & 1) << 5;
            return pc + (u32)((s32)(off << 20) >> 20);
        }
    }
    return pc;
}

/* ---- functions, code and call paths ------------------------------------- */

int esk_an_fn_at(const esk_an *a, u32 pc)
{
    int lo = 0, hi = a->nfn - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (pc < a->fn[mid].lo)
            hi = mid - 1;
        else if (pc >= a->fn[mid].hi)
            lo = mid + 1;
        else
            return mid;
    }
    return -1;
}

int esk_an_in_fn(const esk_an *a, u32 lo, u32 hi)
{
    int f = esk_an_fn_at(a, lo);
    return f >= 0 && hi <= a->fn[f].hi;
}

static int code_at(const esk_an *a, u32 pc)
{
    int lo = 0, hi = a->ncode - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (pc < a->code[mid].lo)
            hi = mid - 1;
        else if (pc >= a->code[mid].hi)
            lo = mid + 1;
        else
            return mid;
    }
    return -1;
}

u64 esk_an_count_at(const esk_an *a, u32 pc)
{
    int i = code_at(a, pc);
    return i < 0 ? 0 : a->code[i].count[(pc - a->code[i].lo) >> 1];
}

static int core_fn(esk_an *a, esk_an_core *k, u32 pc)
{
    if (pc - k->fn_lo < k->fn_hi - k->fn_lo)
        return k->fn_idx;
    k->fn_idx = esk_an_fn_at(a, pc);
    if (k->fn_idx >= 0) {
        k->fn_lo = a->fn[k->fn_idx].lo;
        k->fn_hi = a->fn[k->fn_idx].hi;
    } else {
        k->fn_lo = k->fn_hi = 0;
    }
    return k->fn_idx;
}

static int new_node(esk_an *a, int parent, int fn)
{
    esk_an_node *n;
    if (a->nnode == a->capnode) {
        int cap = a->capnode ? 2 * a->capnode : 256;
        esk_an_node *p = (esk_an_node *)esk_realloc(a->a, a->node, (size_t)a->capnode * sizeof *p,
                                                    (size_t)cap * sizeof *p);
        if (!p) {
            a->oom = 1;
            return parent >= 0 ? parent : 0;
        }
        a->node = p;
        a->capnode = cap;
    }
    n = &a->node[a->nnode];
    memset(n, 0, sizeof *n);
    n->parent = parent;
    n->fn = fn;
    n->child = -1;
    n->sib = -1;
    if (parent >= 0) {
        n->sib = a->node[parent].child;
        a->node[parent].child = a->nnode;
    }
    return a->nnode++;
}

static int child(esk_an *a, int parent, int fn)
{
    int c;
    for (c = a->node[parent].child; c >= 0; c = a->node[c].sib)
        if (a->node[c].fn == fn)
            return c;
    return new_node(a, parent, fn);
}

/* the path the instruction at pc is on: its frame's, or, when the pc has
 * left the frame's function (a tail call, or code no call reached), a path
 * below it */
static int cur_node(esk_an *a, esk_an_core *k, u32 pc)
{
    int base = k->nfr ? k->fr[k->nfr - 1].node : k->root, fn;
    if (k->nfr && k->fr[k->nfr - 1].exc &&
        (k->fr[k->nfr - 1].vec == pc || (a->arch == AN_MIPS && k->fr[k->nfr - 1].vec + 4 == pc)))
        return base; /* the vector's jump (and its slot): the handler's */
    fn = core_fn(a, k, pc);
    if (base == k->cur_base && fn == k->cur_fn)
        return k->cur_node;
    k->cur_base = base;
    k->cur_fn = fn;
    k->cur_node = a->node[base].fn == fn && a->node[base].fn > -2 ? base : child(a, base, fn);
    return k->cur_node;
}

static void push(esk_an *a, esk_an_core *k, struct esk_cpu *c, int parent, u32 ret, u32 target, int exc)
{
    esk_an_frame *f;
    if (k->nfr == ESK_AN_DEPTH)
        return; /* deeper than any stack can be */
    f = &k->fr[k->nfr++];
    f->ret = ret;
    f->sp = esk_an_sp(a, c);
    f->min = f->sp;
    f->exc = exc;
    f->vec = target;
    if (exc)
        target = vector_target(a, c, target);
    f->node = child(a, parent, core_fn(a, k, target));
    a->node[f->node].calls++;
}

/* the frames from i up are over */
static void pop_to(esk_an *a, esk_an_core *k, int i)
{
    int j;
    if (a->what & ESK_AN_STACK)
        for (j = k->nfr - 1; j >= i; j--)
            esk_an_stack_pop(a, k, j);
    k->nfr = i;
}

/* a transfer that may be a return, to `to`: the frame that returns there,
 * above the innermost exception's, is popped with the frames above it */
static void ret_to(esk_an *a, esk_an_core *k, u32 to)
{
    int i;
    for (i = k->nfr - 1; i >= 0 && !k->fr[i].exc; i--)
        if (k->fr[i].ret == to) {
            pop_to(a, k, i);
            return;
        }
}

static void follow(esk_an *a, esk_an_core *k, struct esk_cpu *c, int node, u32 pc1, int ran)
{
    int i;
    if (k->nev) {
        for (i = 0; i < k->nev; i++) {
            if (k->ev[i].entry) {
                push(a, k, c, node, k->ev[i].ret, pc1, k->ev[i].n + 1);
                node = k->nfr ? k->fr[k->nfr - 1].node : k->root;
            } else {
                int j = k->nfr - 1;
                while (j >= 0 && !k->fr[j].exc)
                    j--;
                if (j >= 0)
                    pop_to(a, k, j);
                node = k->nfr ? k->fr[k->nfr - 1].node : k->root;
            }
        }
        return;
    }
    if (!ran || pc1 == k->pc0 + (u32)k->size)
        return;
    if (k->kind == K_CALL) {
        /* a link to inside the function itself, but its entry, is a far
         * branch (GCC's BL in a large Thumb-1 function), not a call */
        int f = esk_an_fn_at(a, k->pc0);
        if (f >= 0 && pc1 != a->fn[f].lo && pc1 - a->fn[f].lo < a->fn[f].hi - a->fn[f].lo)
            return;
        push(a, k, c, node, k->pc0 + (u32)k->size, pc1, 0);
    } else if (k->kind == K_RET)
        ret_to(a, k, pc1);
}

/* ---- the step ------------------------------------------------------------ */

static esk_an_core *core_of(esk_an *a, int core)
{
    return &a->core[core >= 0 && core < a->ncores ? core : 0];
}

void esk_an_exc(esk_an *a, int core, int n, u32 ret)
{
    esk_an_core *k = core_of(a, core);
    if (k->nev < 4) {
        k->ev[k->nev].entry = 1;
        k->ev[k->nev].n = n;
        k->ev[k->nev].ret = ret;
        k->nev++;
    }
}

void esk_an_exc_return(esk_an *a, int core)
{
    esk_an_core *k = core_of(a, core);
    if (k->nev < 4) {
        k->ev[k->nev].entry = 0;
        k->nev++;
    }
}

static int step_kind(esk_an *a, esk_an_core *k, struct esk_cpu *c, u32 pc0)
{
    struct esk_mach *m = a->m;
    switch (a->arch) {
    case AN_ARM:
        return kind_thumb(m, c, pc0, &k->size);
    case AN_RV:
        return kind_riscv(m, c, pc0, &k->size);
    case AN_AVR:
        return kind_avr(m, c, pc0, &k->size);
    case AN_PIC24:
        return kind_pic24(m, pc0, &k->size);
    default: {
        /* the branch's kind is its delay slot's step's: the transfer comes
         * after the slot (but a compact one's, which has none) */
        struct mips_state *ms = (struct mips_state *)c;
        int compact = 0, kind;
        int kk = ms->isa ? kind_mips16(m, c, pc0, &k->size, &compact) : kind_mips(m, c, pc0, &k->size);
        if (compact) {
            k->slot_kind = K_OTHER;
            return kk;
        }
        kind = ms->slot ? k->slot_kind : K_OTHER;
        k->slot_kind = ms->slot ? K_OTHER : kk;
        return kind;
    }
    }
}

void esk_an_step(esk_an *a, int core, struct esk_cpu *c)
{
    struct esk_mach *m = a->m;
    esk_an_core *k = core_of(a, core);
    u32 pc0 = c->ops->pc(c);
    u64 i0 = m->insns, c0 = m->cycles, di, dc;
    int ran;
    k->pc0 = pc0;
    k->nev = 0;
    if (a->what & ESK_AN_CALLS)
        k->kind = step_kind(a, k, c, pc0);
    if ((a->what & ESK_AN_STACK) && k->tail_fn == -2)
        esk_an_stack_first(a, k, c, pc0);
    if (a->what & ESK_AN_MEASURE)
        esk_an_measure_before(a, c0);
    c->ops->step(c);
    di = m->insns - i0;
    dc = m->cycles - c0;
    ran = di != 0;
    a->insns += di;
    a->cycles += dc;
    if (ran && (a->what & ESK_AN_COVERAGE)) {
        int i = a->code_last;
        if (i < 0 || pc0 - a->code[i].lo >= a->code[i].hi - a->code[i].lo)
            i = code_at(a, pc0);
        if (i >= 0) {
            a->code[i].count[(pc0 - a->code[i].lo) >> 1]++;
            a->code_last = i;
        }
    }
    if (a->what & ESK_AN_FAULT) {
        /* a fault the step took, now that its entry is done; the shadow
         * stack is still the faulting code's */
        int i;
        for (i = 0; i < k->nev; i++)
            if (k->ev[i].entry && esk_an_is_fault(a, k->ev[i].n))
                esk_an_fault_taken(a, k, c, k->ev[i].n, k->ev[i].ret);
        esk_an_fault_step(a, k, c);
    }
    if (a->what & ESK_AN_CALLS) {
        int node = cur_node(a, k, pc0);
        /* the step's counts to the path it ran on, before the call or the
         * return it made */
        a->node[node].insns += di;
        a->node[node].cycles += dc;
        if (a->what & ESK_AN_STACK) {
            int entered = 0, i;
            for (i = 0; i < k->nev; i++)
                entered |= k->ev[i].entry;
            esk_an_stack_step(a, k, c, ran, entered, k->kind != K_CALL);
        }
        follow(a, k, c, node, c->ops->pc(c), ran);
    }
    if (a->what & ESK_AN_MEASURE)
        esk_an_measure_after(a, k, ran, c0);
    k->nev = 0;
}

/* ---- the analysis ----------------------------------------------------------- */

typedef struct fn_list {
    esk_an *a;
    esk_an_fn *fn;
    int n, cap;
    size_t names; /* the names' bytes */
    int oom;
} fn_list;

static void add_fn(void *ctx, const char *name, u32 addr, u32 size, int weak)
{
    fn_list *l = (fn_list *)ctx;
    if (!name[0] || l->oom)
        return;
    if (l->n == l->cap) {
        int cap = l->cap ? 2 * l->cap : 64;
        esk_an_fn *p = (esk_an_fn *)esk_realloc(l->a->a, l->fn, (size_t)l->cap * sizeof *p,
                                                (size_t)cap * sizeof *p);
        if (!p) {
            l->oom = 1;
            return;
        }
        l->fn = p;
        l->cap = cap;
    }
    l->fn[l->n].lo = addr & ~1u;
    l->fn[l->n].hi = (addr & ~1u) + size;
    l->fn[l->n].name = name; /* in the image; copied below */
    l->fn[l->n].weak = weak;
    l->n++;
    l->names += strlen(name) + 1;
}

/* by address, then the larger first, then a strong symbol before its weak
 * aliases (startup code's Default_Handler, not the handler it stands in
 * for), then by name */
static int fn_before(const esk_an_fn *x, const esk_an_fn *y)
{
    if (x->lo != y->lo)
        return x->lo < y->lo;
    if (x->hi != y->hi)
        return x->hi > y->hi;
    if (x->weak != y->weak)
        return y->weak;
    return strcmp(x->name, y->name) < 0;
}

static void sort_fns(esk_an_fn *f, int n)
{
    /* an insertion sort over shell gaps: deterministic, no recursion */
    int gap, i, j;
    for (gap = n / 2; gap > 0; gap /= 2)
        for (i = gap; i < n; i++) {
            esk_an_fn t = f[i];
            for (j = i; j >= gap && fn_before(&t, &f[j - gap]); j -= gap)
                f[j] = f[j - gap];
            f[j] = t;
        }
}

es_status esk_an_create(struct esk_mach *m, unsigned what, const u8 *elf, size_t len,
                        const struct esk_dwarf *dw, int cores, esk_an **out)
{
    esk_alloc *al = m->a;
    esk_an *a;
    fn_list l;
    int i, n;
    char *p;
    const char *nm = m->cpu ? m->cpu->ops->name : "";
    *out = NULL;
    a = (esk_an *)esk_calloc(al, 1, sizeof *a);
    if (!a)
        return ES_ENOMEM;
    a->a = al;
    a->m = m;
    a->what = what;
    a->dw = dw;
    a->elf = elf;
    a->elf_len = len;
    a->arch = esk_str_eq(nm, "riscv") ? AN_RV : esk_str_eq(nm, "avr") ? AN_AVR
            : esk_str_eq(nm, "mips") ? AN_MIPS : esk_str_eq(nm, "pic24") ? AN_PIC24 : AN_ARM;
    a->ncores = cores < 1 ? 1 : cores > ESK_AN_CORES ? ESK_AN_CORES : cores;
    a->code_last = -1;
    /* the functions, by address; one of the same address; a function the
     * table gives no size runs to the next one */
    memset(&l, 0, sizeof l);
    l.a = a;
    esk_elf_functions(elf, len, add_fn, &l);
    if (l.oom) {
        esk_free(al, l.fn, (size_t)l.cap * sizeof *l.fn);
        esk_free(al, a, sizeof *a);
        return ES_ENOMEM;
    }
    sort_fns(l.fn, l.n);
    for (i = 0, n = 0; i < l.n; i++) {
        if (n && l.fn[i].lo == l.fn[n - 1].lo)
            continue;
        l.fn[n++] = l.fn[i];
    }
    for (i = 0; i < n; i++) {
        u32 lo = l.fn[i].lo, next = i + 1 < n ? l.fn[i + 1].lo : 0xffffffffu;
        if (l.fn[i].hi <= lo) /* an assembler's label: to the next, at most 4 KiB */
            l.fn[i].hi = next - lo <= 0x1000u ? next : lo + 2;
        else if (l.fn[i].hi > next)
            l.fn[i].hi = next;
    }
    if (!n) {
        esk_free(al, l.fn, (size_t)l.cap * sizeof *l.fn);
        esk_free(al, a, sizeof *a);
        return ES_EINVAL;
    }
    a->fn = l.fn;
    a->nfn = n;
    a->capfn = l.cap;
    a->names_len = l.names + 1;
    a->names = (char *)esk_malloc(al, a->names_len);
    if (!a->names) {
        esk_an_free(a);
        return ES_ENOMEM;
    }
    for (i = 0, p = a->names; i < n; i++) {
        size_t k = strlen(a->fn[i].name) + 1;
        memcpy(p, a->fn[i].name, k);
        a->fn[i].name = p;
        p += k;
    }
    /* the code: the functions' ranges, merged where they meet */
    if (what & ESK_AN_COVERAGE) {
        a->code = (esk_an_code *)esk_calloc(al, (size_t)n, sizeof *a->code);
        if (!a->code) {
            esk_an_free(a);
            return ES_ENOMEM;
        }
        for (i = 0; i < n; i++) {
            if (a->ncode && a->fn[i].lo <= a->code[a->ncode - 1].hi + 16) {
                if (a->fn[i].hi > a->code[a->ncode - 1].hi)
                    a->code[a->ncode - 1].hi = a->fn[i].hi;
                continue;
            }
            a->code[a->ncode].lo = a->fn[i].lo;
            a->code[a->ncode].hi = a->fn[i].hi;
            a->ncode++;
        }
        for (i = 0; i < a->ncode; i++) {
            a->code[i].count = (u64 *)esk_calloc(al, (size_t)(a->code[i].hi - a->code[i].lo) / 2 + 1,
                                                 sizeof(u64));
            if (!a->code[i].count) {
                esk_an_free(a);
                return ES_ENOMEM;
            }
        }
    }
    /* the root, and each core's when there are several */
    new_node(a, -1, -2);
    for (i = 0; i < a->ncores; i++) {
        a->core[i].fr = (esk_an_frame *)esk_calloc(al, ESK_AN_DEPTH, sizeof(esk_an_frame));
        if (!a->core[i].fr || a->oom) {
            esk_an_free(a);
            return ES_ENOMEM;
        }
        a->core[i].root = a->ncores > 1 ? new_node(a, 0, -3 - i) : 0;
    }
    if ((what & ESK_AN_STACK) && esk_an_stack_init(a) != ES_OK) {
        esk_an_free(a);
        return ES_ENOMEM;
    }
    esk_an_restart(a);
    esk_an_clear(a);
    *out = a;
    return ES_OK;
}

void esk_an_restart(esk_an *a)
{
    int i;
    for (i = 0; i < a->ncores; i++) {
        esk_an_core *k = &a->core[i];
        k->nfr = 0;
        k->cur_base = -1;
        k->cur_fn = -1;
        k->cur_node = 0;
        k->fn_lo = k->fn_hi = 0;
        k->fn_idx = -1;
        k->slot_kind = K_OTHER;
        k->nev = 0;
    }
    if (a->what & ESK_AN_STACK)
        esk_an_stack_restart(a);
    if (a->what & ESK_AN_MEASURE)
        esk_an_measure_restart(a);
    a->lockup_seen = 0;
}

void esk_an_clear(esk_an *a)
{
    int i;
    for (i = 0; i < a->ncode; i++)
        memset(a->code[i].count, 0, ((size_t)(a->code[i].hi - a->code[i].lo) / 2 + 1) * sizeof(u64));
    for (i = 0; i < a->nnode; i++) {
        a->node[i].insns = a->node[i].cycles = a->node[i].calls = 0;
    }
    a->insns = a->cycles = 0;
    a->cycles0 = a->m->cycles;
    if (a->what & ESK_AN_STACK)
        esk_an_stack_clear(a);
    if (a->what & ESK_AN_MEASURE)
        esk_an_measure_clear(a);
    esk_an_fault_clear(a);
}

unsigned esk_an_what(const esk_an *a) { return a ? a->what : 0; }

void esk_an_free(esk_an *a)
{
    esk_alloc *al;
    int i;
    if (!a)
        return;
    al = a->a;
    if (a->fn_minsp)
        esk_an_stack_free(a);
    esk_an_fault_clear(a);
    for (i = 0; i < a->ncode; i++)
        esk_free(al, a->code[i].count, ((size_t)(a->code[i].hi - a->code[i].lo) / 2 + 1) * sizeof(u64));
    if (a->code)
        esk_free(al, a->code, (size_t)a->nfn * sizeof *a->code);
    for (i = 0; i < a->ncores; i++)
        if (a->core[i].fr)
            esk_free(al, a->core[i].fr, ESK_AN_DEPTH * sizeof(esk_an_frame));
    esk_free(al, a->node, (size_t)a->capnode * sizeof *a->node);
    esk_free(al, a->names, a->names_len);
    esk_free(al, a->fn, (size_t)a->capfn * sizeof *a->fn);
    esk_free(al, a, sizeof *a);
}
