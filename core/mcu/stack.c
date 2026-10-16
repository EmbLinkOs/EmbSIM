/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* stack.c -- how deep the stacks went, by whom, and whether one went where
 * it must not (analysis.h).
 *
 * After each step esk_an_stack_step reads the core's stack pointers: the
 * main stack's and, on a Cortex-M, the process stack's (each as the core
 * banks it, so a thread on the PSP is measured on the PSP however often
 * handlers run on the MSP). Each one's high-water mark is its lowest value;
 * its top, the highest (on a Cortex-M the MSP's top is also its reset
 * value). The main stack's region runs down from the top to its limit: the
 * link's __stack_limit (or __StackLimit, _sstack, a PIC24's __SPLIM_init),
 * else the end of .data and .bss (_end, __bss_end, _ebss, end) when it lies
 * below the top. The main stack going below the limit -- into .bss and
 * .data, without one -- is an overflow: `mcu.stack_overflow`, naming the
 * instruction that did it, once; the run goes on, as on a part without a
 * stack guard.
 *
 * By function, from the shadow stack's frames (analysis.c):
 *   - deepest: the lowest sp while the pc was in the function;
 *   - frame: the most it moved sp below its entry, what -fstack-usage's .su
 *     file says it uses. The entry is sp before the call: on the AVR the
 *     call's return address (two bytes) is the callee's, as avr-gcc counts
 *     it, and a PIC24's four;
 *   - incl: the most the stack went below its entry while it was on the
 *     stack, its callees included and its interrupt handlers not.
 *
 * A PIC24's stack grows up: esk_an_sp gives it mirrored, so that deeper is
 * lower here too, and the report turns it back (W15's data addresses, its
 * top the stack's base, XC16's __SP_init). */
#include <string.h>

#include "mcu/analysis.h"
#include "mcu/avr/avr.h"
#include "mcu/cortexm/cortexm.h"
#include "mcu/mips/mips.h"
#include "mcu/pic24/pic24.h"
#include "mcu/riscv/riscv.h"

/* analysis.c's architectures */
enum { AN_ARM, AN_RV, AN_AVR, AN_MIPS, AN_PIC24 };

#define PIC24_MIRROR (PIC24_DATA + 0x10000u)
#define NONE 0xffffffffu

/* what a call pushes besides the callee's frame: the return address */
static u32 ret_bytes(const esk_an *a) { return a->arch == AN_AVR ? 2u : a->arch == AN_PIC24 ? 4u : 0u; }

static const char *const limit_syms[] = { "__stack_limit", "__StackLimit", "_sstack", "__SPLIM_init" };
static const char *const end_syms[] = { "_end", "__bss_end", "__bss_end__", "_ebss", "end", "__end__" };

/* an address as the report prints it: a PIC24's W15 */
static u32 shown(const esk_an *a, u32 v) { return a->arch == AN_PIC24 && v ? PIC24_MIRROR - v : v; }
static u32 view(const esk_an *a, u32 real) { return a->arch == AN_PIC24 && real ? PIC24_MIRROR - real : real; }

static u32 reg16(struct esk_cpu *c, int n)
{
    u8 b[8];
    int len = c->ops->reg_read(c, n, b);
    return len >= 2 ? (u32)b[0] | (u32)b[1] << 8 : 0;
}

u32 esk_an_sp(const esk_an *a, struct esk_cpu *c)
{
    switch (a->arch) {
    case AN_ARM:
        return ((struct esk_cm_state *)c)->R[13];
    case AN_RV:
        return (u32)((struct rv_state *)c)->x[2];
    case AN_MIPS:
        return ((struct mips_state *)c)->r[29];
    case AN_PIC24: {
        u32 w = reg16(c, 15);
        return w < (a->sp0 ? a->sp0 : 1u) ? 0 : PIC24_MIRROR - w;
    }
    default:
        return ESK_AVR_DATA + reg16(c, 33);
    }
}

int esk_an_sps(const esk_an *a, struct esk_cpu *c, u32 *msp, u32 *psp)
{
    if (a->arch == AN_ARM) {
        const struct esk_cm_state *k = (const struct esk_cm_state *)c;
        *msp = k->psp_active ? k->other_sp : k->R[13];
        *psp = k->psp_active ? k->R[13] : k->other_sp;
        return k->psp_active;
    }
    *msp = esk_an_sp(a, c);
    *psp = 0;
    return 0;
}

/* a data symbol of the image, as an address the stack pointer compares
 * with (the AVR's data space at ESK_AVR_DATA) */
static int data_sym(const esk_an *a, const char *name, u32 *v)
{
    if (!esk_elf_label(a->elf, a->elf_len, name, v))
        return 0;
    if (a->arch == AN_AVR && *v < 0x10000u)
        *v += ESK_AVR_DATA;
    return 1;
}

/* the end of .data and .bss, when the image says where it is and it lies
 * below the stack's top: 0, or 1 with its address and name */
static int data_end(const esk_an *a, u32 top, u32 *at, const char **why)
{
    size_t i;
    if (a->arch == AN_PIC24)
        return 0; /* the data is below a stack that grows up */
    for (i = 0; i < ES_COUNTOF(end_syms); i++) {
        u32 v = 0;
        if (data_sym(a, end_syms[i], &v) && v < top && top - v < 0x10000000u) {
            *at = v;
            *why = end_syms[i];
            return 1;
        }
    }
    return 0;
}

/* the limit an overflow is checked against, once the top is known: the
 * linked limit, and the end of the data when that is higher */
static void set_check(const esk_an *a, esk_an_core *k)
{
    u32 de = 0;
    const char *why = NULL;
    k->check = a->stk_limit;
    k->check_why = a->stk_limit_why;
    if (k->msp_top && data_end(a, k->msp_top, &de, &why) && (!a->stk_limit_why || de > a->stk_limit)) {
        k->check = de;
        k->check_why = why;
    }
    k->checked_top = k->msp_top;
}

static int fn_slot(const esk_an *a, int f) { return f >= 0 ? f : a->nfn; }

es_status esk_an_stack_init(esk_an *a)
{
    size_t n = (size_t)a->nfn + 1, i;
    a->fn_minsp = (u32 *)esk_calloc(a->a, n, sizeof(u32));
    a->fn_frame = (u32 *)esk_calloc(a->a, n, sizeof(u32));
    a->fn_incl = (u32 *)esk_calloc(a->a, n, sizeof(u32));
    if (!a->fn_minsp || !a->fn_frame || !a->fn_incl)
        return ES_ENOMEM;
    /* a PIC24's stack starts where XC16's startup puts W15; before it (W15
     * 0x0800 out of reset) it is not one */
    if (a->arch == AN_PIC24 && !esk_elf_label(a->elf, a->elf_len, "__SP_init", &a->sp0))
        a->sp0 = 0;
    for (i = 0; i < ES_COUNTOF(limit_syms) && !a->stk_limit_why; i++)
        if (data_sym(a, limit_syms[i], &a->stk_limit)) {
            a->stk_limit = view(a, a->stk_limit);
            a->stk_limit_why = limit_syms[i];
        }
    return ES_OK;
}

void esk_an_stack_free(esk_an *a)
{
    size_t n = (size_t)a->nfn + 1;
    esk_free(a->a, a->fn_minsp, n * sizeof(u32));
    esk_free(a->a, a->fn_frame, n * sizeof(u32));
    esk_free(a->a, a->fn_incl, n * sizeof(u32));
    a->fn_minsp = a->fn_frame = a->fn_incl = NULL;
}

/* the machine reset: each core's function is found again at its first
 * step; an overflow is reported again. The depths are kept, like the counts */
void esk_an_stack_restart(esk_an *a)
{
    int i;
    for (i = 0; i < a->ncores; i++) {
        a->core[i].tail_fn = -2;
        a->core[i].tail_sp = 0;
    }
    a->ovf = 0;
}

/* the counts cleared: the depths from now on (the stacks' tops kept) */
void esk_an_stack_clear(esk_an *a)
{
    size_t n = (size_t)a->nfn + 1, i;
    int c;
    for (c = 0; c < a->ncores; c++) {
        esk_an_core *k = &a->core[c];
        k->msp_min = 0;
        k->psp_min = NONE;
        k->psp_used = 0;
        for (i = 0; i < (size_t)k->nfr; i++)
            k->fr[i].min = k->fr[i].sp;
    }
    for (i = 0; i < n; i++) {
        a->fn_minsp[i] = NONE;
        a->fn_frame[i] = a->fn_incl[i] = 0;
    }
    a->ovf = 0;
}

/* the first step since the reset: the stack pointer it starts with, the
 * function it starts in */
void esk_an_stack_first(esk_an *a, esk_an_core *k, struct esk_cpu *c, u32 pc0)
{
    u32 psp = 0;
    (void)esk_an_sps(a, c, &k->last_msp, &psp);
    k->tail_fn = -1;
    esk_an_stack_entered(a, k, c, pc0, 0);
}

void esk_an_stack_entered(esk_an *a, esk_an_core *k, struct esk_cpu *c, u32 pc1, int called)
{
    int f = esk_an_fn_at(a, pc1);
    if (!called && f >= 0 && a->fn[f].lo == pc1) {
        k->tail_fn = f;
        k->tail_sp = esk_an_sp(a, c);
    }
}

static void overflow(esk_an *a, esk_an_core *k, u32 msp, int ran)
{
    char msg[200];
    int f = esk_an_fn_at(a, k->pc0);
    a->ovf = 1;
    a->ovf_core = (int)(k - a->core);
    a->ovf_pc = k->pc0;
    a->ovf_sp = msp;
    a->ovf_limit = k->check;
    a->ovf_why = k->check_why;
    a->ovf_entry = !ran;
    if (!a->m->host.diag)
        return;
    esk_snprintf(msg, sizeof msg, "stack overflow: sp 0x%08x %s 0x%08x (%s), %s 0x%08x (%s)", (unsigned)shown(a, msp),
                 a->arch == AN_PIC24 ? "above" : "below", (unsigned)shown(a, k->check), k->check_why,
                 ran ? "at" : "in an exception's entry at", (unsigned)k->pc0, f >= 0 ? a->fn[f].name : "no symbol");
    a->m->host.diag(a->m->host.ctx, ES_SEV_ERROR, "mcu.stack_overflow", msg);
}

/* the function of the step's pc (analysis.c's cache holds it) */
static int step_fn(const esk_an *a, const esk_an_core *k)
{
    return k->pc0 - k->fn_lo < k->fn_hi - k->fn_lo ? k->fn_idx : esk_an_fn_at(a, k->pc0);
}

void esk_an_stack_step(esk_an *a, esk_an_core *k, struct esk_cpu *c, int ran, int entered, int sample_frame)
{
    u32 msp = 0, psp = 0, sp;
    int f = step_fn(a, k);
    if (esk_an_sps(a, c, &msp, &psp))
        k->psp_used = 1;
    /* the top: the highest the main stack was, before and after a step of
     * the image's code (not a boot ROM's, on a stack of its own) */
    if (f >= 0) {
        u32 hi = msp > k->last_msp ? msp : k->last_msp;
        if (hi > k->msp_top)
            k->msp_top = hi;
    }
    k->last_msp = msp;
    if (msp && (!k->msp_min || msp < k->msp_min))
        k->msp_min = msp;
    if (k->psp_used) {
        if (psp > k->psp_top)
            k->psp_top = psp;
        if (psp && psp < k->psp_min)
            k->psp_min = psp;
    }
    sp = esk_an_sp(a, c);
    if (ran && sp) {
        int s = fn_slot(a, f);
        u32 base = 0;
        if (sp < a->fn_minsp[s])
            a->fn_minsp[s] = sp;
        if (!entered && k->nfr && sp < k->fr[k->nfr - 1].min)
            k->fr[k->nfr - 1].min = sp;
        /* the function's own frame: below the sp it was entered with, by a
         * call (its frame's) or by a jump to its first instruction */
        if (k->nfr && a->node[k->fr[k->nfr - 1].node].fn == f && f >= 0)
            base = k->fr[k->nfr - 1].sp + ret_bytes(a);
        else if (f >= 0 && f == k->tail_fn)
            base = k->tail_sp;
        if (!entered && sample_frame && base && base - sp < 0x80000000u && base - sp > a->fn_frame[s])
            a->fn_frame[s] = base - sp;
    }
    if (!entered)
        esk_an_stack_entered(a, k, c, c->ops->pc(c), !sample_frame);
    /* the overflow: the main stack below its limit */
    if (k->msp_top != k->checked_top)
        set_check(a, k);
    if (msp && k->check && msp < k->check && !a->ovf && a->m->state == ESK_MACH_RUN)
        overflow(a, k, msp, ran);
}

/* a frame is over: its depth below its entry, and its lowest sp to the
 * frame that called it (not to the one an exception interrupted) */
void esk_an_stack_pop(esk_an *a, esk_an_core *k, int i)
{
    esk_an_frame *f = &k->fr[i];
    int s = fn_slot(a, a->node[f->node].fn);
    u32 base = f->sp + ret_bytes(a);
    if (s > a->nfn)
        return; /* a core's root */
    if (f->sp && base - f->min < 0x80000000u && base - f->min > a->fn_incl[s])
        a->fn_incl[s] = base - f->min;
    if (i > 0 && !f->exc && f->min < k->fr[i - 1].min)
        k->fr[i - 1].min = f->min;
}

/* ---- the report --------------------------------------------------------- */

/* the frames still open: their depths, innermost first (the report is a
 * snapshot: the frames stay open, their depths so far counted) */
static void close_open(esk_an *a)
{
    int c, i;
    for (c = 0; c < a->ncores; c++) {
        esk_an_core *k = &a->core[c];
        for (i = k->nfr - 1; i >= 0; i--) {
            esk_an_frame *f = &k->fr[i];
            int s = fn_slot(a, a->node[f->node].fn);
            u32 base = f->sp + ret_bytes(a);
            if (s <= a->nfn && f->sp && base - f->min < 0x80000000u && base - f->min > a->fn_incl[s])
                a->fn_incl[s] = base - f->min;
        }
    }
}

/* a core's main stack's limit for the report: the linked one, the end of the
 * data, else the bottom (a PIC24's top) of the RAM it is in */
static u32 report_limit(esk_an *a, const esk_an_core *k, const char **why)
{
    u32 lim = a->stk_limit, de = 0;
    const char *dwhy = NULL;
    struct esk_region *r;
    *why = a->stk_limit_why;
    if (!*why && k->msp_top && data_end(a, k->msp_top, &de, &dwhy)) {
        *why = dwhy;
        return de;
    }
    if (!*why && k->msp_top && a->arch == AN_PIC24) {
        r = esk_bus_region(&a->m->bus, PIC24_DATA + shown(a, k->msp_top), 1);
        if (r) {
            *why = "the top of RAM";
            return view(a, r->base + r->size - PIC24_DATA);
        }
    } else if (!*why && k->msp_top) {
        r = esk_bus_region(&a->m->bus, k->msp_top - 1, 1);
        if (r) {
            *why = "the bottom of RAM";
            return r->base;
        }
    }
    return lim;
}

/* the functions with a depth, deepest first */
static int *by_depth(esk_an *a, int *count)
{
    int n = 0, i, j, gap, nf = a->nfn + 1;
    int *idx = (int *)esk_calloc(a->a, (size_t)nf, sizeof(int));
    if (!idx)
        return NULL;
    for (i = 0; i < nf; i++)
        if (a->fn_minsp[i] != NONE)
            idx[n++] = i;
    for (gap = n / 2; gap > 0; gap /= 2)
        for (i = gap; i < n; i++) {
            int t = idx[i];
            for (j = i; j >= gap && (a->fn_minsp[t] < a->fn_minsp[idx[j - gap]] ||
                                     (a->fn_minsp[t] == a->fn_minsp[idx[j - gap]] && t < idx[j - gap]));
                 j -= gap)
                idx[j] = idx[j - gap];
            idx[j] = t;
        }
    *count = n;
    return idx;
}

static const char *fname(const esk_an *a, int i) { return i < a->nfn ? a->fn[i].name : "[unknown]"; }

int esk_an_stack(esk_an *a, const char *name, esk_strbuf *o)
{
    int c, n = 0, k, up = a->arch == AN_PIC24;
    int *idx;
    if (!(a->what & ESK_AN_STACK))
        return 1;
    close_open(a);
    esk_strbuf_printf(o, "embsim stack: %s\n", name ? name : "");
    for (c = 0; c < a->ncores; c++) {
        const esk_an_core *kc = &a->core[c];
        const char *why;
        u32 lim = report_limit(a, kc, &why);
        char who[24];
        esk_snprintf(who, sizeof who, "%s%s", a->ncores > 1 ? (c ? "core1 " : "core0 ") : "",
                     a->arch == AN_ARM ? "main stack (MSP)" : "stack");
        if (!kc->msp_top || !kc->msp_min || kc->msp_min > kc->msp_top) {
            esk_strbuf_printf(o, "%s: not used\n", who);
        } else {
            esk_strbuf_printf(o, "%s: %s 0x%08x, %s 0x%08x: %u bytes used", who, up ? "base" : "top",
                              (unsigned)shown(a, kc->msp_top), up ? "highest" : "deepest", (unsigned)shown(a, kc->msp_min),
                              (unsigned)(kc->msp_top - kc->msp_min));
            if (why && lim < kc->msp_top)
                esk_strbuf_printf(o, " of %u (%s 0x%08x, %s)", (unsigned)(kc->msp_top - lim), up ? "up to" : "down to",
                                  (unsigned)shown(a, lim), why);
            esk_strbuf_putc(o, '\n');
        }
        if (a->arch == AN_ARM) {
            if (kc->psp_used && kc->psp_min != NONE)
                esk_strbuf_printf(o, "%sprocess stack (PSP): top 0x%08x, deepest 0x%08x: %u bytes used\n",
                                  a->ncores > 1 ? (c ? "core1 " : "core0 ") : "", (unsigned)kc->psp_top,
                                  (unsigned)kc->psp_min, (unsigned)(kc->psp_top - kc->psp_min));
            else
                esk_strbuf_printf(o, "%sprocess stack (PSP): not used\n", a->ncores > 1 ? (c ? "core1 " : "core0 ") : "");
        }
    }
    if (a->ovf) {
        int f = esk_an_fn_at(a, a->ovf_pc);
        esk_strbuf_printf(o, "OVERFLOW: sp 0x%08x went %s 0x%08x (%s), %s 0x%08x (%s)\n", (unsigned)shown(a, a->ovf_sp),
                          up ? "above" : "below", (unsigned)shown(a, a->ovf_limit), a->ovf_why ? a->ovf_why : "",
                          a->ovf_entry ? "in an exception's entry at" : "at", (unsigned)a->ovf_pc,
                          f >= 0 ? a->fn[f].name : "no symbol");
    }
    idx = by_depth(a, &n);
    if (!idx)
        return 0;
    esk_strbuf_printf(o, "by function, deepest first (bytes; frame and incl from the function's entry):\n");
    esk_strbuf_printf(o, "  %-28s %10s %7s %7s\n", "function", up ? "highest" : "deepest", "frame", "incl");
    for (k = 0; k < n; k++) {
        int i = idx[k];
        esk_strbuf_printf(o, "  %-28s 0x%08x %7u %7u\n", fname(a, i), (unsigned)shown(a, a->fn_minsp[i]),
                          (unsigned)a->fn_frame[i], (unsigned)a->fn_incl[i]);
    }
    esk_free(a->a, idx, ((size_t)a->nfn + 1) * sizeof(int));
    return !o->oom;
}

esk_json *esk_an_stack_json(esk_an *a, esk_alloc *al, int top)
{
    esk_json *out, *stacks, *fns;
    int c, n = 0, k, *idx;
    if (!(a->what & ESK_AN_STACK))
        return NULL;
    close_open(a);
    out = esk_json_new_object(al);
    stacks = esk_json_new_array(al);
    fns = esk_json_new_array(al);
    if (!out || !stacks || !fns) {
        esk_json_free(al, stacks);
        esk_json_free(al, fns);
        return out;
    }
    for (c = 0; c < a->ncores; c++) {
        const esk_an_core *kc = &a->core[c];
        const char *why;
        u32 lim = report_limit(a, kc, &why);
        esk_json *s = esk_json_new_object(al);
        if (!s)
            break;
        esk_json_set_integer(al, s, "core", c);
        esk_json_set_string(al, s, "name", a->arch == AN_ARM ? "msp" : "stack");
        if (kc->msp_top && kc->msp_min && kc->msp_min <= kc->msp_top) {
            esk_json_set_integer(al, s, "top", shown(a, kc->msp_top));
            esk_json_set_integer(al, s, "deepest", shown(a, kc->msp_min));
            esk_json_set_integer(al, s, "used", kc->msp_top - kc->msp_min);
            if (why && lim < kc->msp_top) {
                esk_json_set_integer(al, s, "limit", shown(a, lim));
                esk_json_set_string(al, s, "limit_from", why);
                esk_json_set_integer(al, s, "size", kc->msp_top - lim);
            }
        }
        esk_json_array_push(al, stacks, s);
        if (a->arch == AN_ARM && kc->psp_used && kc->psp_min != NONE) {
            esk_json *p = esk_json_new_object(al);
            if (p) {
                esk_json_set_integer(al, p, "core", c);
                esk_json_set_string(al, p, "name", "psp");
                esk_json_set_integer(al, p, "top", kc->psp_top);
                esk_json_set_integer(al, p, "deepest", kc->psp_min);
                esk_json_set_integer(al, p, "used", kc->psp_top - kc->psp_min);
                esk_json_array_push(al, stacks, p);
            }
        }
    }
    esk_json_object_set(al, out, "stacks", stacks);
    if (a->ovf) {
        esk_json *o = esk_json_new_object(al);
        int f = esk_an_fn_at(a, a->ovf_pc);
        if (o) {
            esk_json_set_integer(al, o, "core", a->ovf_core);
            esk_json_set_integer(al, o, "pc", a->ovf_pc);
            esk_json_set_string(al, o, "function", f >= 0 ? a->fn[f].name : "");
            esk_json_set_integer(al, o, "sp", shown(a, a->ovf_sp));
            esk_json_set_integer(al, o, "limit", shown(a, a->ovf_limit));
            esk_json_set_string(al, o, "limit_from", a->ovf_why ? a->ovf_why : "");
            esk_json_object_set(al, out, "overflow", o);
        }
    }
    idx = by_depth(a, &n);
    for (k = 0; idx && k < n && (!top || k < top); k++) {
        int i = idx[k];
        esk_json *x = esk_json_new_object(al);
        if (!x)
            break;
        esk_json_set_string(al, x, "name", fname(a, i));
        esk_json_set_integer(al, x, "deepest", shown(a, a->fn_minsp[i]));
        esk_json_set_integer(al, x, "frame", a->fn_frame[i]);
        esk_json_set_integer(al, x, "incl", a->fn_incl[i]);
        esk_json_array_push(al, fns, x);
    }
    if (idx)
        esk_free(a->a, idx, ((size_t)a->nfn + 1) * sizeof(int));
    esk_json_object_set(al, out, "functions", fns);
    return out;
}
