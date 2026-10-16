/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* profile.c -- where the run's instructions and cycles went, by function
 * (analysis.h).
 *
 * esk_an_step gives each step's instructions and cycles -- the change in the
 * machine's counts, so whatever a step costs beyond its instruction (a
 * taken branch, an exception's entry) is in it -- to the call path the pc
 * is on (esk_an_node). Every step goes to exactly one path, so the counts
 * add up to the steps' totals, to the instruction and the cycle. The time a
 * core sleeps (WFI, an idle loop) is no step's: the report gives it as the
 * machine's elapsed cycles beside the cycles run. From the paths:
 *   - a function's self counts are those of the paths that end in it;
 *   - its inclusive counts, those of every path it is on (once, for a
 *     recursive one), so its callees' are in them;
 *   - its calls, the times a call or an exception's entry reached it.
 * The report is the functions by self cycles; the collapsed form writes the
 * paths as collapsed stacks ("main;f;g 1234", by cycles or by
 * instructions), which flamegraph.pl, speedscope and inferno read. A
 * machine with several cores roots each core's paths in "core0", "core1". */
#include <string.h>

#include "mcu/analysis.h"

typedef struct pfn {
    int fn;
    u64 self_i, self_c, incl_i, incl_c, calls;
} pfn;

static const char *fn_name(const esk_an *a, int fn, char *buf, size_t cap)
{
    if (fn >= 0)
        return a->fn[fn].name;
    if (fn <= -3) {
        esk_snprintf(buf, cap, "core%d", -3 - fn);
        return buf;
    }
    return "[unknown]";
}

static int self_before(const pfn *x, const pfn *y)
{
    if (x->self_c != y->self_c)
        return x->self_c > y->self_c;
    if (x->incl_c != y->incl_c)
        return x->incl_c > y->incl_c;
    return x->fn < y->fn;
}

static void sort_pfn(pfn *p, int n)
{
    int gap, i, j;
    for (gap = n / 2; gap > 0; gap /= 2)
        for (i = gap; i < n; i++) {
            pfn t = p[i];
            for (j = i; j >= gap && self_before(&t, &p[j - gap]); j -= gap)
                p[j] = p[j - gap];
            p[j] = t;
        }
}

/* the functions' counts from the paths: nfn + 1 entries, the last for the
 * code no symbol covers, sorted by self cycles */
static pfn *flat(esk_an *a, int *count)
{
    int nf = a->nfn + 1, n, i;
    pfn *p = (pfn *)esk_calloc(a->a, (size_t)nf, sizeof *p);
    int *mark = (int *)esk_calloc(a->a, (size_t)nf, sizeof *mark);
    if (!p || !mark) {
        esk_free(a->a, p, (size_t)nf * sizeof *p);
        esk_free(a->a, mark, (size_t)nf * sizeof *mark);
        return NULL;
    }
    for (i = 0; i < nf; i++)
        p[i].fn = i < nf - 1 ? i : -1;
    for (n = 1; n < a->nnode; n++) {
        const esk_an_node *d = &a->node[n];
        int f, up, depth;
        if (d->fn <= -2)
            continue; /* a core's root */
        f = d->fn >= 0 ? d->fn : nf - 1;
        p[f].self_i += d->insns;
        p[f].self_c += d->cycles;
        p[f].calls += d->calls;
        if (!d->insns && !d->cycles)
            continue;
        /* each function on the path, once */
        for (up = n, depth = 0; up > 0 && depth <= ESK_AN_DEPTH + 1; up = a->node[up].parent, depth++) {
            int g;
            if (a->node[up].fn <= -2)
                break;
            g = a->node[up].fn >= 0 ? a->node[up].fn : nf - 1;
            if (mark[g] == n)
                continue;
            mark[g] = n;
            p[g].incl_i += d->insns;
            p[g].incl_c += d->cycles;
        }
    }
    esk_free(a->a, mark, (size_t)nf * sizeof *mark);
    sort_pfn(p, nf);
    *count = nf;
    return p;
}

static void pct(char *b, size_t n, u64 part, u64 all)
{
    esk_snprintf(b, n, "%5.1f%%", all ? 100.0 * (double)part / (double)all : 0.0);
}

/* a path's names, root first, into o */
static void path(const esk_an *a, int n, esk_strbuf *o)
{
    int chain[ESK_AN_DEPTH + 2], k = 0;
    char buf[16];
    for (; n > 0 && k < ESK_AN_DEPTH + 2; n = a->node[n].parent)
        chain[k++] = n;
    while (k-- > 0) {
        esk_strbuf_puts(o, fn_name(a, a->node[chain[k]].fn, buf, sizeof buf));
        if (k)
            esk_strbuf_putc(o, ';');
    }
}

typedef struct stack {
    size_t at, len; /* in the scratch buffer */
    u64 w;
} stack;

static int stack_before(const char *base, const stack *x, const stack *y)
{
    size_t n = x->len < y->len ? x->len : y->len;
    int c = memcmp(base + x->at, base + y->at, n);
    return c ? c < 0 : x->len < y->len;
}

int esk_an_profile(esk_an *a, int fmt, const char *name, esk_strbuf *o)
{
    char p1[16], p2[16], buf[16];
    int i, nf = 0;
    if (!(a->what & ESK_AN_PROFILE))
        return 1;
    if (fmt == ESK_AN_COLLAPSED || fmt == ESK_AN_COLLAPSED_INSNS) {
        /* the paths with a weight, sorted by their names */
        esk_strbuf sb;
        stack *st = (stack *)esk_calloc(a->a, (size_t)a->nnode + 1, sizeof *st);
        int ns = 0, gap, j;
        if (!st)
            return 0;
        esk_strbuf_init(&sb, a->a);
        for (i = 1; i < a->nnode; i++) {
            u64 w = fmt == ESK_AN_COLLAPSED ? a->node[i].cycles : a->node[i].insns;
            if (!w)
                continue;
            st[ns].at = sb.len;
            path(a, i, &sb);
            st[ns].len = sb.len - st[ns].at;
            st[ns].w = w;
            ns++;
        }
        if (sb.oom) {
            esk_strbuf_free(&sb);
            esk_free(a->a, st, ((size_t)a->nnode + 1) * sizeof *st);
            return 0;
        }
        for (gap = ns / 2; gap > 0; gap /= 2)
            for (i = gap; i < ns; i++) {
                stack t = st[i];
                for (j = i; j >= gap && stack_before(sb.data, &t, &st[j - gap]); j -= gap)
                    st[j] = st[j - gap];
                st[j] = t;
            }
        for (i = 0; i < ns; i++)
            esk_strbuf_printf(o, "%s%s%.*s %llu\n", name ? name : "", name ? ";" : "", (int)st[i].len,
                              sb.data + st[i].at, (unsigned long long)st[i].w);
        esk_strbuf_free(&sb);
        esk_free(a->a, st, ((size_t)a->nnode + 1) * sizeof *st);
        return !o->oom;
    }
    {
        pfn *p = flat(a, &nf);
        u64 ti = 0, tc = 0;
        if (!p)
            return 0;
        esk_strbuf_printf(o, "embsim profile: %s, %llu instructions, %llu cycles run, %llu elapsed\n",
                          name ? name : "", (unsigned long long)a->insns, (unsigned long long)a->cycles,
                          (unsigned long long)(a->m->cycles - a->cycles0));
        esk_strbuf_printf(o, "%12s %12s %6s %12s %12s %6s %9s  %s\n", "self insns", "self cycles", "self",
                          "incl insns", "incl cycles", "incl", "calls", "function");
        for (i = 0; i < nf; i++) {
            if (!p[i].incl_i && !p[i].incl_c && !p[i].calls)
                continue;
            ti += p[i].self_i;
            tc += p[i].self_c;
            pct(p1, sizeof p1, p[i].self_c, a->cycles);
            pct(p2, sizeof p2, p[i].incl_c, a->cycles);
            esk_strbuf_printf(o, "%12llu %12llu %6s %12llu %12llu %6s %9llu  %s\n", (unsigned long long)p[i].self_i,
                              (unsigned long long)p[i].self_c, p1, (unsigned long long)p[i].incl_i,
                              (unsigned long long)p[i].incl_c, p2, (unsigned long long)p[i].calls,
                              fn_name(a, p[i].fn, buf, sizeof buf));
        }
        pct(p1, sizeof p1, tc, a->cycles);
        esk_strbuf_printf(o, "%12llu %12llu %6s %12s %12s %6s %9s  (total)\n", (unsigned long long)ti,
                          (unsigned long long)tc, p1, "", "", "", "");
        esk_free(a->a, p, (size_t)nf * sizeof *p);
    }
    return !o->oom;
}

esk_json *esk_an_profile_json(esk_an *a, esk_alloc *al, int top)
{
    esk_json *out, *fns;
    pfn *p;
    int i, nf = 0, shown = 0;
    char buf[16];
    if (!(a->what & ESK_AN_PROFILE))
        return NULL;
    p = flat(a, &nf);
    out = esk_json_new_object(al);
    fns = esk_json_new_array(al);
    if (!p || !out || !fns) {
        if (p)
            esk_free(a->a, p, (size_t)nf * sizeof *p);
        esk_json_free(al, fns);
        return out;
    }
    esk_json_set_integer(al, out, "insns", (int64_t)a->insns);
    esk_json_set_integer(al, out, "cycles", (int64_t)a->cycles);
    esk_json_set_integer(al, out, "elapsed_cycles", (int64_t)(a->m->cycles - a->cycles0));
    for (i = 0; i < nf && (!top || shown < top); i++) {
        esk_json *x;
        if (!p[i].incl_i && !p[i].incl_c && !p[i].calls)
            continue;
        x = esk_json_new_object(al);
        if (!x)
            break;
        esk_json_set_string(al, x, "name", fn_name(a, p[i].fn, buf, sizeof buf));
        esk_json_set_integer(al, x, "self_insns", (int64_t)p[i].self_i);
        esk_json_set_integer(al, x, "self_cycles", (int64_t)p[i].self_c);
        esk_json_set_integer(al, x, "incl_insns", (int64_t)p[i].incl_i);
        esk_json_set_integer(al, x, "incl_cycles", (int64_t)p[i].incl_c);
        esk_json_set_integer(al, x, "calls", (int64_t)p[i].calls);
        esk_json_array_push(al, fns, x);
        shown++;
    }
    esk_json_object_set(al, out, "functions", fns);
    esk_free(a->a, p, (size_t)nf * sizeof *p);
    return out;
}
