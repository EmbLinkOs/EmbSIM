/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* coverage.c -- which source lines and functions ran (analysis.h).
 *
 * esk_an_step counts each instruction executed, by its address, in a count
 * per halfword of the image's functions. At the end the line table maps the
 * counts to source lines: a line has code when the table gives it an
 * address range inside a function, and its count is that of its
 * most-executed instruction -- the times the line ran, for straight-line
 * code. A function's count is its first instruction's: the times it was
 * entered. The report is gcov's layout, per file, with the source beside
 * the counts when the file can be read; or lcov's tracefile, which genhtml
 * and the CI coverage services read; or JSON, for the protocol. */
#include <string.h>

#include "host/host.h"
#include "mcu/analysis.h"
#include "mcu/dwarf.h"

/* a file's lines: whether each has code, and its count */
typedef struct cline {
    int code;
    u64 count;
} cline;

typedef struct cfile {
    int idx;          /* in dw->files */
    const char *path; /* dw->files[idx] */
    cline *ln;
    int nln;
    int *fn; /* its functions, by index in a->fn */
    int nfn, capfn;
    int all, hit, fhit;
} cfile;

typedef struct cov {
    esk_an *a;
    cfile *f;
    int nf;
    int *nofile; /* the functions with no line */
    int nnofile;
    int *fline;  /* each function's first line, or 0 */
    int ln_all, ln_hit, fn_all, fn_hit;
    int oom;
} cov;

static int file_index(const esk_dwarf *dw, const char *name)
{
    size_t i;
    for (i = 0; i < dw->nfiles; i++)
        if (dw->files[i] == name)
            return (int)i;
    for (i = 0; i < dw->nfiles; i++)
        if (!strcmp(dw->files[i], name))
            return (int)i;
    return -1;
}

static cline *line_of(cov *c, cfile *f, u32 line)
{
    esk_alloc *al = c->a->a;
    if ((int)line >= f->nln) {
        int n = (int)line + 64;
        cline *p = (cline *)esk_realloc(al, f->ln, (size_t)f->nln * sizeof *p, (size_t)n * sizeof *p);
        if (!p) {
            c->oom = 1;
            return NULL;
        }
        memset(p + f->nln, 0, (size_t)(n - f->nln) * sizeof *p);
        f->ln = p;
        f->nln = n;
    }
    return &f->ln[line];
}

static void add_fn(cov *c, cfile *f, int fn)
{
    if (f->nfn == f->capfn) {
        int cap = f->capfn ? 2 * f->capfn : 8;
        int *p = (int *)esk_realloc(c->a->a, f->fn, (size_t)f->capfn * sizeof *p, (size_t)cap * sizeof *p);
        if (!p) {
            c->oom = 1;
            return;
        }
        f->fn = p;
        f->capfn = cap;
    }
    f->fn[f->nfn++] = fn;
}

static int path_before(const cfile *x, const cfile *y) { return strcmp(x->path, y->path) < 0; }

static void sort_files(cfile *f, int n)
{
    int gap, i, j;
    for (gap = n / 2; gap > 0; gap /= 2)
        for (i = gap; i < n; i++) {
            cfile t = f[i];
            for (j = i; j >= gap && path_before(&t, &f[j - gap]); j -= gap)
                f[j] = f[j - gap];
            f[j] = t;
        }
}

static void cov_free(cov *c)
{
    esk_alloc *al = c->a->a;
    int i;
    for (i = 0; i < c->nf; i++) {
        esk_free(al, c->f[i].ln, (size_t)c->f[i].nln * sizeof(cline));
        esk_free(al, c->f[i].fn, (size_t)c->f[i].capfn * sizeof(int));
    }
    esk_free(al, c->f, (size_t)(c->nf + 1) * sizeof *c->f);
    esk_free(al, c->nofile, (size_t)(c->a->nfn + 1) * sizeof(int));
    esk_free(al, c->fline, (size_t)(c->a->nfn + 1) * sizeof(int));
}

/* the counts by file and line, and the functions by file */
static int cov_build(cov *c, esk_an *a)
{
    const esk_dwarf *dw = a->dw;
    esk_alloc *al = a->a;
    size_t i;
    int k;
    memset(c, 0, sizeof *c);
    c->a = a;
    c->nf = dw ? (int)dw->nfiles : 0;
    c->f = (cfile *)esk_calloc(al, (size_t)c->nf + 1, sizeof *c->f);
    c->nofile = (int *)esk_calloc(al, (size_t)a->nfn + 1, sizeof(int));
    c->fline = (int *)esk_calloc(al, (size_t)a->nfn + 1, sizeof(int));
    if (!c->f || !c->nofile || !c->fline) {
        c->oom = 1;
        return 0;
    }
    for (k = 0; k < c->nf; k++) {
        c->f[k].idx = k;
        c->f[k].path = dw->files[k];
    }
    /* the lines */
    for (i = 0; dw && i + 1 < dw->nrows; i++) {
        const esk_dwarf_row *r = &dw->rows[i];
        u32 lo = r->addr, hi = dw->rows[i + 1].addr, pc;
        cline *l;
        if (r->end || !r->line || r->file < 0 || r->file >= c->nf || hi <= lo || hi - lo > 0x10000u ||
            !esk_an_in_fn(a, lo, hi))
            continue;
        l = line_of(c, &c->f[r->file], r->line);
        if (!l)
            return 0;
        l->code = 1;
        for (pc = lo; pc < hi; pc += 2) {
            u64 n = esk_an_count_at(a, pc);
            if (n > l->count)
                l->count = n;
        }
    }
    /* the functions, in the file of their first line; the ones without a
     * line go in their own list */
    for (k = 0; k < a->nfn; k++) {
        const char *file = NULL;
        u32 line = 0;
        int fi = -1;
        c->fn_all++;
        c->fn_hit += esk_an_count_at(a, a->fn[k].lo) != 0;
        if (dw && esk_dwarf_line_at(dw, a->fn[k].lo, &file, &line) && line)
            fi = file_index(dw, file);
        if (fi < 0) {
            c->nofile[c->nnofile++] = k;
            continue;
        }
        c->fline[k] = (int)line;
        add_fn(c, &c->f[fi], k);
        if (c->oom)
            return 0;
    }
    sort_files(c->f, c->nf);
    for (k = 0; k < c->nf; k++) {
        cfile *f = &c->f[k];
        int j;
        for (j = 0; j < f->nln; j++)
            if (f->ln[j].code) {
                f->all++;
                f->hit += f->ln[j].count != 0;
            }
        for (j = 0; j < f->nfn; j++)
            f->fhit += esk_an_count_at(a, a->fn[f->fn[j]].lo) != 0;
        c->ln_all += f->all;
        c->ln_hit += f->hit;
    }
    return 1;
}

static void pct(char *buf, size_t n, int hit, int all)
{
    if (all)
        esk_snprintf(buf, n, "%.1f%%", 100.0 * hit / all);
    else
        esk_snprintf(buf, n, "-");
}

/* the source beside the counts, as gcov lays it out; the lines the source
 * did not show (no source, or past its end) after it */
static void text_file(cov *c, const cfile *f, const char *path, const char *src_dir, esk_strbuf *o)
{
    esk_alloc *al = c->a->a;
    char *src = NULL, *full = NULL;
    size_t len = 0, at = 0;
    int no = 0, k;
    if (esk_host_read_file(al, path, &src, &len) != ES_OK && src_dir) {
        const char *base = strrchr(path, '/');
        full = esk_host_path_join(al, src_dir, path);
        if (full && esk_host_read_file(al, full, &src, &len) != ES_OK && base) {
            esk_strfree(al, full);
            full = esk_host_path_join(al, src_dir, base + 1);
            if (full && esk_host_read_file(al, full, &src, &len) != ES_OK)
                src = NULL;
        }
    }
    while (src && at < len) {
        size_t e = at;
        int n;
        while (e < len && src[e] != '\n')
            e++;
        n = (int)(e - at);
        if (n && src[at + (size_t)n - 1] == '\r')
            n--;
        no++;
        if (no < f->nln && f->ln[no].code && f->ln[no].count)
            esk_strbuf_printf(o, "%9llu:%5d:%.*s\n", (unsigned long long)f->ln[no].count, no, n, src + at);
        else if (no < f->nln && f->ln[no].code)
            esk_strbuf_printf(o, "%9s:%5d:%.*s\n", "#####", no, n, src + at);
        else
            esk_strbuf_printf(o, "%9s:%5d:%.*s\n", "-", no, n, src + at);
        at = e + 1;
    }
    if (src)
        esk_free(al, src, len + 1);
    esk_strfree(al, full);
    for (k = no + 1; k < f->nln; k++) {
        if (!f->ln[k].code)
            continue;
        if (f->ln[k].count)
            esk_strbuf_printf(o, "%9llu:%5d:\n", (unsigned long long)f->ln[k].count, k);
        else
            esk_strbuf_printf(o, "%9s:%5d:\n", "#####", k);
    }
}

int esk_an_coverage(esk_an *a, int fmt, const char *name, const char *src_dir, esk_strbuf *o)
{
    cov c;
    int i, k;
    char p1[16], p2[16];
    if (!(a->what & ESK_AN_COVERAGE))
        return 1;
    if (!cov_build(&c, a)) {
        cov_free(&c);
        return 0;
    }
    if (fmt == ESK_AN_LCOV)
        esk_strbuf_printf(o, "TN:%s\n", name ? name : "");
    else
        esk_strbuf_printf(o, "embsim coverage: %s, %llu instructions run\n", name ? name : "",
                          (unsigned long long)a->insns);
    for (i = 0; i < c.nf; i++) {
        const cfile *f = &c.f[i];
        const char *path = f->path;
        if (!f->all && !f->nfn)
            continue;
        if (fmt == ESK_AN_LCOV) {
            esk_strbuf_printf(o, "SF:%s\n", path);
            for (k = 0; k < f->nfn; k++)
                esk_strbuf_printf(o, "FN:%d,%s\n", c.fline[f->fn[k]], a->fn[f->fn[k]].name);
            for (k = 0; k < f->nfn; k++)
                esk_strbuf_printf(o, "FNDA:%llu,%s\n", (unsigned long long)esk_an_count_at(a, a->fn[f->fn[k]].lo),
                                  a->fn[f->fn[k]].name);
            esk_strbuf_printf(o, "FNF:%d\nFNH:%d\n", f->nfn, f->fhit);
            for (k = 0; k < f->nln; k++)
                if (f->ln[k].code)
                    esk_strbuf_printf(o, "DA:%d,%llu\n", k, (unsigned long long)f->ln[k].count);
            esk_strbuf_printf(o, "LF:%d\nLH:%d\nend_of_record\n", f->all, f->hit);
            continue;
        }
        pct(p1, sizeof p1, f->hit, f->all);
        pct(p2, sizeof p2, f->fhit, f->nfn);
        esk_strbuf_printf(o, "\n%s: %d of %d lines (%s), %d of %d functions (%s)\n", path, f->hit, f->all, p1,
                          f->fhit, f->nfn, p2);
        for (k = 0; k < f->nfn; k++)
            esk_strbuf_printf(o, "  function %s (line %d): %llu\n", a->fn[f->fn[k]].name, c.fline[f->fn[k]],
                              (unsigned long long)esk_an_count_at(a, a->fn[f->fn[k]].lo));
        text_file(&c, f, path, src_dir, o);
    }
    if (fmt != ESK_AN_LCOV) {
        if (c.nnofile) {
            esk_strbuf_printf(o, "\nfunctions with no line information:\n");
            for (k = 0; k < c.nnofile; k++)
                esk_strbuf_printf(o, "  function %s: %llu\n", a->fn[c.nofile[k]].name,
                                  (unsigned long long)esk_an_count_at(a, a->fn[c.nofile[k]].lo));
        }
        pct(p1, sizeof p1, c.ln_hit, c.ln_all);
        pct(p2, sizeof p2, c.fn_hit, c.fn_all);
        esk_strbuf_printf(o, "\ntotal: %d of %d lines (%s), %d of %d functions (%s)\n", c.ln_hit, c.ln_all, p1,
                          c.fn_hit, c.fn_all, p2);
    }
    cov_free(&c);
    return !o->oom;
}

static esk_json *hit_total(esk_alloc *al, int hit, int total)
{
    esk_json *j = esk_json_new_object(al);
    if (j) {
        esk_json_set_integer(al, j, "hit", hit);
        esk_json_set_integer(al, j, "total", total);
    }
    return j;
}

esk_json *esk_an_coverage_json(esk_an *a, esk_alloc *al, int detail)
{
    cov c;
    esk_json *out, *files;
    int i, k;
    if (!(a->what & ESK_AN_COVERAGE))
        return NULL;
    if (!cov_build(&c, a)) {
        cov_free(&c);
        return NULL;
    }
    out = esk_json_new_object(al);
    files = esk_json_new_array(al);
    if (!out || !files) {
        cov_free(&c);
        return out;
    }
    esk_json_object_set(al, out, "lines", hit_total(al, c.ln_hit, c.ln_all));
    esk_json_object_set(al, out, "functions", hit_total(al, c.fn_hit, c.fn_all));
    esk_json_set_integer(al, out, "insns", (int64_t)a->insns);
    for (i = 0; i < c.nf; i++) {
        const cfile *f = &c.f[i];
        esk_json *fj;
        if (!f->all && !f->nfn)
            continue;
        fj = esk_json_new_object(al);
        if (!fj)
            break;
        esk_json_set_string(al, fj, "path", f->path);
        esk_json_object_set(al, fj, "lines", hit_total(al, f->hit, f->all));
        if (!detail) {
            esk_json_object_set(al, fj, "functions", hit_total(al, f->fhit, f->nfn));
        } else {
            esk_json *fns = esk_json_new_array(al), *counts = esk_json_new_array(al);
            for (k = 0; fns && k < f->nfn; k++) {
                esk_json *x = esk_json_new_object(al);
                if (!x)
                    break;
                esk_json_set_string(al, x, "name", a->fn[f->fn[k]].name);
                esk_json_set_integer(al, x, "line", c.fline[f->fn[k]]);
                esk_json_set_integer(al, x, "count", (int64_t)esk_an_count_at(a, a->fn[f->fn[k]].lo));
                esk_json_array_push(al, fns, x);
            }
            for (k = 0; counts && k < f->nln; k++) {
                esk_json *x;
                if (!f->ln[k].code)
                    continue;
                x = esk_json_new_array(al);
                if (!x)
                    break;
                esk_json_array_push(al, x, esk_json_new_integer(al, k));
                esk_json_array_push(al, x, esk_json_new_integer(al, (int64_t)f->ln[k].count));
                esk_json_array_push(al, counts, x);
            }
            esk_json_object_set(al, fj, "functions", fns);
            esk_json_object_set(al, fj, "counts", counts);
        }
        esk_json_array_push(al, files, fj);
    }
    esk_json_object_set(al, out, "files", files);
    if (detail && c.nnofile) {
        esk_json *nf = esk_json_new_array(al);
        for (k = 0; nf && k < c.nnofile; k++) {
            esk_json *x = esk_json_new_object(al);
            if (!x)
                break;
            esk_json_set_string(al, x, "name", a->fn[c.nofile[k]].name);
            esk_json_set_integer(al, x, "count", (int64_t)esk_an_count_at(a, a->fn[c.nofile[k]].lo));
            esk_json_array_push(al, nf, x);
        }
        esk_json_object_set(al, out, "no_lines", nf);
    }
    cov_free(&c);
    return out;
}
