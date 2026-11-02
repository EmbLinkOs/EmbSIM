/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* latency.c -- measurements between named boundaries in a firmware's run
 * (docs/engineering/timing-fidelity.md, step 3): the cycles and the
 * simulated time from one boundary to the next, each time it happens.
 *
 * A boundary is one of:
 *   SYMBOL or 0xADDR  the instruction at that address starting (the cycle
 *                     before its own cost, after any exception sequence
 *                     the step took first);
 *   exc:N             the core starting to take exception N (the cycle
 *                     before the entry's or tail-chain's own cycles);
 *   pend:N            exception N becoming pending (an interrupt
 *                     asserted, a PendSV or SVC requested): the cycle the
 *                     request was made at, as the device or instruction
 *                     made it.
 *   ret:N             exception N's return done: the first instruction
 *                     after it starting (after the return's cycles), or,
 *                     when another exception tail-chains in its place, that
 *                     tail-chain starting. With pend:PendSV, ret:PendSV
 *                     measures an RTOS context switch from its request to
 *                     the first instruction of the task switched to.
 * N is a number or a name: Reset, NMI, HardFault, MemManage, BusFault,
 * UsageFault, SVCall, DebugMon, PendSV, SysTick, IRQn (16 + n).
 *
 * A measurement FROM..TO starts at the first FROM after the last sample
 * and ends at the next TO; when FROM and TO are the same boundary each
 * occurrence ends one sample and starts the next (a period).
 *
 * The simulator watches the run from outside: a measurement adds no
 * instruction and no cycle to the firmware, unlike a DWT.CYCCNT read in
 * the program, whose own instructions are inside what it measures. Time
 * is the core's cycles converted at the core clock that ran them (exact
 * rational, without drift); host time is not involved. */
#include <stdlib.h>
#include <string.h>

#include "mcu/analysis.h"

#define PS_PER_S 1000000000000ull

static const struct {
    const char *name;
    int n;
} exc_names[] = {
    { "Reset", 1 },     { "NMI", 2 },       { "HardFault", 3 }, { "MemManage", 4 },
    { "BusFault", 5 },  { "UsageFault", 6 }, { "SVCall", 11 },  { "SVC", 11 },
    { "DebugMon", 12 }, { "PendSV", 14 },   { "SysTick", 15 },
};

static int parse_exc(const char *s, int *n)
{
    size_t i;
    char *end;
    long v;
    for (i = 0; i < sizeof exc_names / sizeof exc_names[0]; i++)
        if (!strcmp(s, exc_names[i].name)) {
            *n = exc_names[i].n;
            return 1;
        }
    if (!strncmp(s, "IRQ", 3) && s[3]) {
        v = strtol(s + 3, &end, 10);
        if (*end || v < 0 || v > 495)
            return 0;
        *n = 16 + (int)v;
        return 1;
    }
    v = strtol(s, &end, 0);
    if (!*s || *end || v < 1 || v > 511)
        return 0;
    *n = (int)v;
    return 1;
}

static int parse_bound(esk_an *a, const char *s, esk_an_bound *b, char *why, size_t cap)
{
    int i;
    char *end;
    if (!strncmp(s, "exc:", 4) || !strncmp(s, "pend:", 5) || !strncmp(s, "ret:", 4)) {
        int pend = s[0] == 'p', n = 0;
        if (!parse_exc(strchr(s, ':') + 1, &n)) {
            esk_snprintf(why, cap, "%s: not an exception (a number, IRQn or a name such as PendSV)", s);
            return 0;
        }
        b->kind = pend ? ESK_AN_B_PEND : s[0] == 'r' ? ESK_AN_B_RET : ESK_AN_B_EXC;
        b->v = (u32)n;
        return 1;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        unsigned long v = strtoul(s, &end, 16);
        if (*end || !s[2]) {
            esk_snprintf(why, cap, "%s: not an address", s);
            return 0;
        }
        b->kind = ESK_AN_B_PC;
        b->v = (u32)v & ~1u;
        return 1;
    }
    for (i = 0; i < a->nfn; i++)
        if (a->fn[i].name && !strcmp(a->fn[i].name, s)) {
            b->kind = ESK_AN_B_PC;
            b->v = a->fn[i].lo;
            return 1;
        }
    esk_snprintf(why, cap, "%s: no such function in the image's symbols", s);
    return 0;
}

es_status esk_an_measure_add(esk_an *a, const char *spec, char *why, size_t cap)
{
    const char *eq = strchr(spec, '='), *body = eq ? eq + 1 : spec, *dots = strstr(body, "..");
    esk_an_meas *m;
    char from[96], to[96];
    size_t lf, lt;
    if (!(a->what & ESK_AN_MEASURE)) {
        esk_snprintf(why, cap, "measurements are not on");
        return ES_ESTATE;
    }
    if (!dots || dots == body || !dots[2]) {
        esk_snprintf(why, cap, "%s: a measurement is [NAME=]FROM..TO", spec);
        return ES_EINVAL;
    }
    if (a->nmeas == ESK_AN_MEASURES) {
        esk_snprintf(why, cap, "at most %d measurements", ESK_AN_MEASURES);
        return ES_EINVAL;
    }
    lf = (size_t)(dots - body);
    lt = strlen(dots + 2);
    if (lf >= sizeof from || lt >= sizeof to || (eq && (size_t)(eq - spec) >= sizeof m->name)) {
        esk_snprintf(why, cap, "%s: too long", spec);
        return ES_EINVAL;
    }
    m = &a->meas[a->nmeas];
    memset(m, 0, sizeof *m);
    memcpy(from, body, lf);
    from[lf] = 0;
    memcpy(to, dots + 2, lt + 1);
    if (!parse_bound(a, from, &m->from, why, cap) || !parse_bound(a, to, &m->to, why, cap))
        return ES_EINVAL;
    if (eq) {
        memcpy(m->name, spec, (size_t)(eq - spec));
        m->name[eq - spec] = 0;
    } else {
        esk_snprintf(m->name, sizeof m->name, "m%d", a->nmeas + 1);
    }
    esk_snprintf(m->from_s, sizeof m->from_s, "%s", from);
    esk_snprintf(m->to_s, sizeof m->to_s, "%s", to);
    a->nmeas++;
    return ES_OK;
}

/* the simulated time brought up to machine cycle `c`, at the core clock now */
static void t_sync(esk_an *a, u64 c)
{
    u64 d, per, rem;
    u32 hz;
    if (!a->t_init) {
        a->t_c = c;
        a->t_init = 1;
        return;
    }
    if (c <= a->t_c)
        return;
    d = c - a->t_c;
    hz = esk_mach_core_hz(a->m);
    if (!hz)
        hz = 1;
    per = PS_PER_S / hz;
    rem = PS_PER_S % hz;
    a->t_ps += d * per;
    a->t_frac += d * rem;
    a->t_ps += a->t_frac / hz;
    a->t_frac %= hz;
    a->t_c = c;
    if (!a->hz_lo || hz < a->hz_lo)
        a->hz_lo = hz;
    if (hz > a->hz_hi)
        a->hz_hi = hz;
}

static int same(const esk_an_bound *x, int kind, u32 v)
{
    return x->kind == kind && x->v == v;
}

static void trace_add(esk_an *a, int kind, u32 v, u64 c, u64 ps)
{
    if (!a->tracing)
        return;
    if (!a->tstarted) {
        if (a->tstart.kind && !(a->tstart.kind == kind && a->tstart.v == v))
            return;
        a->tstarted = 1;
    }
    if (a->ntev >= a->tlimit) {
        a->tdropped++;
        return;
    }
    if (a->ntev == a->captev) {
        u64 cap = a->captev ? 2 * a->captev : 4096;
        struct esk_an_tev *p;
        if (cap > a->tlimit)
            cap = a->tlimit;
        p = (struct esk_an_tev *)esk_realloc(a->a, a->tev, (size_t)a->captev * sizeof *p, (size_t)cap * sizeof *p);
        if (!p) {
            a->tdropped++;
            return;
        }
        a->tev = p;
        a->captev = cap;
    }
    a->tev[a->ntev].c = c - a->cycles0;
    a->tev[a->ntev].ps = ps;
    a->tev[a->ntev].v = v;
    a->tev[a->ntev].kind = (u8)kind;
    a->tev[a->ntev].core = 0;
    a->ntev++;
}

/* boundary (kind, v) at machine cycle c */
static void stamp(esk_an *a, int kind, u32 v, u64 c)
{
    int i;
    u64 ps;
    t_sync(a, c);
    ps = a->t_ps;
    trace_add(a, kind, v, c, ps);
    for (i = 0; i < a->nmeas; i++) {
        esk_an_meas *m = &a->meas[i];
        if (m->armed && same(&m->to, kind, v)) {
            u64 dc = c - m->c0, dps = ps - m->ps0;
            if (!m->n || dc < m->cmin)
                m->cmin = dc;
            if (dc > m->cmax)
                m->cmax = dc;
            if (!m->n || dps < m->psmin)
                m->psmin = dps;
            if (dps > m->psmax)
                m->psmax = dps;
            m->csum += dc;
            m->pssum += dps;
            if (m->ns < ESK_AN_MSAMPLES) {
                m->s[m->ns].c0 = m->c0 - a->cycles0;
                m->s[m->ns].cycles = dc;
                m->s[m->ns].ps = dps;
                m->ns++;
            }
            m->n++;
            m->armed = 0;
        }
        if (!m->armed && same(&m->from, kind, v)) {
            m->armed = 1;
            m->c0 = c;
            m->ps0 = ps;
        }
    }
}

void esk_an_measure_before(esk_an *a, u64 c0)
{
    t_sync(a, c0);
    a->in_step = 1;
    a->npend = 0;
}

void esk_an_measure_after(esk_an *a, esk_an_core *k, int ran, u64 c0)
{
    int i, core = (int)(k - a->core);
    a->in_step = 0;
    for (i = 0; i < k->nev; i++) {
        if (k->ev[i].entry) {
            /* a return just done, and this exception tail-chained in */
            if (a->ret_n[core]) {
                stamp(a, ESK_AN_B_RET, (u32)a->ret_n[core], c0);
                a->ret_n[core] = 0;
            }
            stamp(a, ESK_AN_B_EXC, (u32)k->ev[i].n, c0);
            if (a->nexc[core] < 16)
                a->exc_stack[core][a->nexc[core]] = k->ev[i].n;
            a->nexc[core]++;
        } else if (a->nexc[core] > 0) {
            a->nexc[core]--;
            if (a->nexc[core] < 16)
                a->ret_n[core] = a->exc_stack[core][a->nexc[core]];
        }
    }
    if (ran) {
        /* a return's cycles were a step of their own: this instruction is
         * the first after it, unless the return came in this very step */
        int returned_now = 0;
        for (i = 0; i < k->nev; i++)
            returned_now |= !k->ev[i].entry;
        if (a->ret_n[core] && !returned_now) {
            stamp(a, ESK_AN_B_RET, (u32)a->ret_n[core], c0);
            a->ret_n[core] = 0;
        }
        stamp(a, ESK_AN_B_PC, k->pc0 & ~1u, c0);
    }
    for (i = 0; i < a->npend; i++)
        stamp(a, ESK_AN_B_PEND, (u32)a->pend[i].n, a->pend[i].c);
    a->npend = 0;
}

void esk_an_pend(esk_an *a, int core, int n)
{
    (void)core;
    if (!(a->what & ESK_AN_MEASURE) || (!a->nmeas && !a->tracing))
        return;
    if (a->in_step) {
        if (a->npend < (int)(sizeof a->pend / sizeof a->pend[0])) {
            a->pend[a->npend].n = n;
            a->pend[a->npend].c = a->m->cycles;
            a->npend++;
        }
        return;
    }
    stamp(a, ESK_AN_B_PEND, (u32)n, a->m->cycles);
}

void esk_an_measure_restart(esk_an *a)
{
    int i;
    for (i = 0; i < a->nmeas; i++)
        a->meas[i].armed = 0;
    for (i = 0; i < ESK_AN_CORES; i++)
        a->nexc[i] = a->ret_n[i] = 0;
    a->npend = 0;
    a->in_step = 0;
}

void esk_an_measure_clear(esk_an *a)
{
    int i;
    for (i = 0; i < a->nmeas; i++) {
        esk_an_meas *m = &a->meas[i];
        m->armed = 0;
        m->n = m->cmin = m->cmax = m->csum = m->psmin = m->psmax = m->pssum = 0;
        m->ns = 0;
    }
    a->t_init = 0;
    a->t_ps = a->t_frac = 0;
    a->hz_lo = a->hz_hi = 0;
}

static void ns_text(esk_strbuf *out, u64 ps)
{
    esk_strbuf_printf(out, "%llu.%03llu ns", (unsigned long long)(ps / 1000u), (unsigned long long)(ps % 1000u));
}

int esk_an_measure_text(esk_an *a, const char *name, esk_strbuf *out)
{
    int i;
    t_sync(a, a->m->cycles);
    esk_strbuf_printf(out, "measurements: %s, core clock ", name ? name : "the MCU");
    if (a->hz_lo == a->hz_hi)
        esk_strbuf_printf(out, "%u Hz", (unsigned)a->hz_hi);
    else
        esk_strbuf_printf(out, "%u to %u Hz (it changed in the run)", (unsigned)a->hz_lo, (unsigned)a->hz_hi);
    esk_strbuf_printf(out, "; simulated time, independent of the host\n");
    for (i = 0; i < a->nmeas; i++) {
        esk_an_meas *m = &a->meas[i];
        esk_strbuf_printf(out, "  %s: %s .. %s: %llu sample%s", m->name, m->from_s, m->to_s, (unsigned long long)m->n,
                          m->n == 1 ? "" : "s");
        if (m->n) {
            esk_strbuf_printf(out, "; cycles min %llu, mean %.2f, max %llu; time min ", (unsigned long long)m->cmin,
                              (double)m->csum / (double)m->n, (unsigned long long)m->cmax);
            ns_text(out, m->psmin);
            esk_strbuf_printf(out, ", mean ");
            ns_text(out, m->pssum / m->n);
            esk_strbuf_printf(out, ", max ");
            ns_text(out, m->psmax);
        }
        if (m->armed)
            esk_strbuf_printf(out, " (one started, not ended)");
        esk_strbuf_printf(out, "\n");
    }
    return !out->oom;
}

esk_json *esk_an_measure_json(esk_an *a, esk_alloc *al)
{
    esk_json *o = esk_json_new_object(al), *list = esk_json_new_array(al);
    int i, j;
    if (!o || !list) {
        esk_json_free(al, o);
        esk_json_free(al, list);
        return NULL;
    }
    t_sync(a, a->m->cycles);
    esk_json_object_set(al, o, "time_base", esk_json_new_string(al, "simulated"));
    esk_json_object_set(al, o, "core_hz_min", esk_json_new_integer(al, (int64_t)a->hz_lo));
    esk_json_object_set(al, o, "core_hz_max", esk_json_new_integer(al, (int64_t)a->hz_hi));
    for (i = 0; i < a->nmeas; i++) {
        esk_an_meas *m = &a->meas[i];
        esk_json *e = esk_json_new_object(al), *ss = esk_json_new_array(al);
        if (!e || !ss) {
            esk_json_free(al, e);
            esk_json_free(al, ss);
            break;
        }
        esk_json_object_set(al, e, "name", esk_json_new_string(al, m->name));
        esk_json_object_set(al, e, "from", esk_json_new_string(al, m->from_s));
        esk_json_object_set(al, e, "to", esk_json_new_string(al, m->to_s));
        esk_json_object_set(al, e, "count", esk_json_new_integer(al, (int64_t)m->n));
        if (m->n) {
            esk_json_object_set(al, e, "cycles_min", esk_json_new_integer(al, (int64_t)m->cmin));
            esk_json_object_set(al, e, "cycles_max", esk_json_new_integer(al, (int64_t)m->cmax));
            esk_json_object_set(al, e, "cycles_mean", esk_json_new_number(al, (double)m->csum / (double)m->n));
            esk_json_object_set(al, e, "ps_min", esk_json_new_integer(al, (int64_t)m->psmin));
            esk_json_object_set(al, e, "ps_max", esk_json_new_integer(al, (int64_t)m->psmax));
            esk_json_object_set(al, e, "ps_mean", esk_json_new_number(al, (double)m->pssum / (double)m->n));
        }
        for (j = 0; j < m->ns; j++) {
            esk_json *s = esk_json_new_object(al);
            if (!s)
                break;
            esk_json_object_set(al, s, "start_cycle", esk_json_new_integer(al, (int64_t)m->s[j].c0));
            esk_json_object_set(al, s, "cycles", esk_json_new_integer(al, (int64_t)m->s[j].cycles));
            esk_json_object_set(al, s, "ps", esk_json_new_integer(al, (int64_t)m->s[j].ps));
            esk_json_array_push(al, ss, s);
        }
        esk_json_object_set(al, e, "samples", ss);
        esk_json_array_push(al, list, e);
    }
    esk_json_object_set(al, o, "measurements", list);
    return o;
}

/* ---- the trace ---- */

es_status esk_an_trace(esk_an *a, const char *start, u64 limit, char *why, size_t cap)
{
    if (!(a->what & ESK_AN_MEASURE)) {
        esk_snprintf(why, cap, "the trace is not on");
        return ES_ESTATE;
    }
    memset(&a->tstart, 0, sizeof a->tstart);
    if (start && !parse_bound(a, start, &a->tstart, why, cap))
        return ES_EINVAL;
    a->tracing = 1;
    a->tstarted = 0;
    a->tlimit = limit ? limit : 100000;
    return ES_OK;
}

static const char *exc_name(u32 n)
{
    size_t i;
    for (i = 0; i < sizeof exc_names / sizeof exc_names[0]; i++)
        if ((u32)exc_names[i].n == n && strcmp(exc_names[i].name, "SVC"))
            return exc_names[i].name;
    return NULL;
}

static void ev_name(esk_strbuf *out, const struct esk_an_tev *e)
{
    const char *nm = exc_name(e->v);
    if (nm)
        esk_strbuf_printf(out, "%s", nm);
    else if (e->v >= 16)
        esk_strbuf_printf(out, "IRQ%u", (unsigned)(e->v - 16));
    else
        esk_strbuf_printf(out, "%u", (unsigned)e->v);
}

static void vcd_bits(esk_strbuf *out, u32 v, int n, const char *id)
{
    int i;
    esk_strbuf_putc(out, 'b');
    for (i = n - 1; i >= 0; i--)
        esk_strbuf_putc(out, (v >> i) & 1 ? '1' : '0');
    esk_strbuf_printf(out, " %s\n", id);
}

int esk_an_trace_write(esk_an *a, int vcd, esk_strbuf *out)
{
    u64 i;
    if (!vcd) {
        esk_strbuf_printf(out, "# simulated time; cycle\ttime_ns\tevent\tvalue\twhere\n");
        for (i = 0; i < a->ntev; i++) {
            const struct esk_an_tev *e = &a->tev[i];
            esk_strbuf_printf(out, "%llu\t%llu.%03llu\t", (unsigned long long)e->c, (unsigned long long)(e->ps / 1000u),
                              (unsigned long long)(e->ps % 1000u));
            if (e->kind == ESK_AN_B_PC) {
                int f = esk_an_fn_at(a, e->v);
                esk_strbuf_printf(out, "insn\t0x%08x\t%s\n", (unsigned)e->v, f >= 0 && a->fn[f].name ? a->fn[f].name : "?");
            } else {
                esk_strbuf_printf(out, "%s\t", e->kind == ESK_AN_B_EXC ? "exc" : e->kind == ESK_AN_B_PEND ? "pend" : "ret");
                ev_name(out, e);
                esk_strbuf_printf(out, "\t\n");
            }
        }
        if (a->tdropped)
            esk_strbuf_printf(out, "# %llu events past the limit not recorded\n", (unsigned long long)a->tdropped);
        return !out->oom;
    }
    /* a value change dump at 1 ps: pc (32 bits), the exception active (9),
     * an event pulse per pending exception */
    esk_strbuf_printf(out, "$comment EmbSIM trace: simulated time, the core's own cycles at its clock $end\n"
                           "$timescale 1ps $end\n$scope module core $end\n"
                           "$var wire 32 p pc $end\n$var wire 9 x exception $end\n$var wire 9 q pending $end\n"
                           "$var wire 64 c cycle $end\n$upscope $end\n$enddefinitions $end\n");
    {
        u32 active[17];
        int na = 0;
        u64 last = (u64)-1;
        for (i = 0; i < a->ntev; i++) {
            const struct esk_an_tev *e = &a->tev[i];
            if (e->ps != last) {
                esk_strbuf_printf(out, "#%llu\n", (unsigned long long)e->ps);
                last = e->ps;
                {
                    u64 cy = e->c;
                    int b;
                    esk_strbuf_putc(out, 'b');
                    for (b = 63; b >= 0; b--)
                        esk_strbuf_putc(out, (cy >> b) & 1 ? '1' : '0');
                    esk_strbuf_printf(out, " c\n");
                }
            }
            if (e->kind == ESK_AN_B_PC) {
                vcd_bits(out, e->v, 32, "p");
            } else if (e->kind == ESK_AN_B_EXC) {
                if (na < 17)
                    active[na++] = e->v;
                vcd_bits(out, e->v, 9, "x");
            } else if (e->kind == ESK_AN_B_RET) {
                if (na > 0)
                    na--;
                vcd_bits(out, na ? active[na - 1] : 0, 9, "x");
            } else {
                vcd_bits(out, e->v, 9, "q");
            }
        }
    }
    return !out->oom;
}

