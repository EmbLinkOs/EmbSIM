/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* debug.c -- the debug methods of the session protocol (SPEC-008 §5 debug,
 * SPEC-009 §10): a board's MCU halted, stepped and inspected.
 *
 * A breakpoint or watchpoint stops the core at the instruction, and the
 * whole session with it: the scheduler returns from its advance at that
 * event, so the circuit is observed at the stop. control.run, control.advance
 * and debug.continue resume a stopped core; debug.step runs `count`
 * instructions and stops again. Registers are named and numbered as GDB's
 * target description has them; memory and peripheral reads are a
 * debugger's (no read side effects). Symbols come from the image's ELF
 * symbol table. */
#include <stdio.h>
#include <string.h>

#include "host/host.h"
#include "mcu/avr/avr.h"
#include "mcu/cortexm/cortexm.h"
#include "mcu/dwarf.h"
#include "mcu/mcu.h"
#include "mcu/rf.h"
#include "protocol/wsutil.h"
#include "session/session_priv.h"
#include "util/num.h"
#include "util/str.h"

#define MAX_MEMORY_READ 65536u
#define MAX_DISASSEMBLY 512
#define STEP_LIMIT_PS (1000 * ES_PS_PER_MS) /* a step that sleeps longer gives up */

typedef struct dbg {
    es_session *s;
    const esk_json *params;
    esk_json *result;
    int code;
    char msg[200];
    esk_board_inst *board;
    esk_comp *mcu;
    struct esk_mach *m;
} dbg;

static es_status fail(dbg *d, int code, const char *msg)
{
    d->code = code;
    esk_snprintf(d->msg, sizeof d->msg, "%s", msg);
    return ES_EPROTO;
}

static esk_json *obj(dbg *d) { return esk_json_new_object(d->s->a); }

static int pu32(dbg *d, const char *key, u32 *out)
{
    uint64_t v = 0;
    const esk_json *j = esk_json_get(d->params, key);
    if (!j || esk_json_to_u64(j, &v) != 0 || v > 0xffffffffu)
        return -1;
    *out = (u32)v;
    return 0;
}

/* the board the request names, with firmware on its MCU */
static es_status find_board(dbg *d)
{
    const char *id = esk_json_get_str(d->params, "board", NULL);
    size_t i;
    if (!d->s->loaded)
        return fail(d, -32000 + ES_ESTATE, "no project is loaded");
    for (i = 0; i < d->s->boards.len; i++) {
        esk_board_inst *bi = d->s->boards.data[i];
        if (id ? esk_str_eq(bi->id, id) : d->s->boards.len == 1) {
            d->board = bi;
            d->mcu = bi->mcu;
            d->m = bi->mcu ? esk_mcu_machine(bi->mcu) : NULL;
            if (!d->m)
                return fail(d, -32602, "the board's MCU has no firmware");
            return ES_OK;
        }
    }
    return fail(d, -32602, id ? "no such board" : "board is required");
}

static const unsigned char *image(dbg *d, size_t *len) { return esk_mcu_image(d->mcu, len); }

/* an address: "address" (a number or "0x..."), or "symbol" */
static es_status address_of(dbg *d, u32 *addr)
{
    const char *sym = esk_json_get_str(d->params, "symbol", NULL);
    size_t len = 0;
    const unsigned char *img;
    if (pu32(d, "address", addr) == 0)
        return ES_OK;
    if (!sym)
        return fail(d, -32602, "address or symbol is required");
    img = image(d, &len);
    if (!img || !esk_elf_symbol(img, len, sym, addr))
        return fail(d, -32602, "the image has no such symbol");
    return ES_OK;
}

static void describe_pc(dbg *d, esk_json *o, u32 pc)
{
    char name[128];
    u32 off = 0, line = 0;
    size_t len = 0;
    const char *file = NULL;
    const unsigned char *img = image(d, &len);
    const esk_dwarf *dw = esk_mcu_dwarf(d->mcu);
    esk_json_set_integer(d->s->a, o, "pc", (int64_t)pc);
    if (img && esk_elf_symbol_at(img, len, pc, name, sizeof name, &off)) {
        esk_json_set_string(d->s->a, o, "function", name);
        esk_json_set_integer(d->s->a, o, "offset", (int64_t)off);
    }
    if (dw && esk_dwarf_line_at(dw, pc, &file, &line)) {
        esk_json_set_string(d->s->a, o, "file", file);
        esk_json_set_integer(d->s->a, o, "line", (int64_t)line);
    }
}

/* ---- the methods --------------------------------------------------------------- */

/* a breakpoint on each statement of file:line */
static es_status line_breakpoint(dbg *d, const char *file, u32 line)
{
    const esk_dwarf *dw = esk_mcu_dwarf(d->mcu);
    const char *op = esk_json_get_str(d->params, "op", "add");
    esk_json *addrs;
    u32 at[16];
    int n, i;
    char id[300];
    if (!dw)
        return fail(d, -32602, "the image has no DWARF line information (build it with -g)");
    n = esk_dwarf_line_addrs(dw, file, line, at, 16);
    if (!n)
        return fail(d, -32602, "no code at that line");
    d->result = obj(d);
    addrs = esk_json_new_array(d->s->a);
    if (!d->result || !addrs) {
        esk_json_free(d->s->a, addrs);
        return ES_ENOMEM;
    }
    for (i = 0; i < n; i++) {
        if (esk_str_eq(op, "remove"))
            (void)esk_mach_bp_remove(d->m, at[i]);
        else if (esk_mach_bp_add(d->m, at[i]) != 0) {
            esk_json_free(d->s->a, addrs);
            return fail(d, -32602, "too many breakpoints");
        }
        esk_json_array_push(d->s->a, addrs, esk_json_new_integer(d->s->a, (int64_t)at[i]));
    }
    esk_snprintf(id, sizeof id, "line:%s:%lu", file, (unsigned long)line);
    esk_json_set_string(d->s->a, d->result, "id", id);
    esk_json_set_integer(d->s->a, d->result, "address", (int64_t)at[0]);
    esk_json_object_set(d->s->a, d->result, "addresses", addrs);
    return ES_OK;
}

static es_status m_breakpoint(dbg *d)
{
    const char *op = esk_json_get_str(d->params, "op", "add");
    const char *kind = esk_json_get_str(d->params, "kind", "code");
    const char *file = esk_json_get_str(d->params, "file", NULL);
    u32 addr = 0, len = 4, line = 0;
    int r, wk;
    char id[48];
    es_status st;
    if (file && pu32(d, "line", &line) == 0)
        return line_breakpoint(d, file, line);
    st = address_of(d, &addr);
    if (st != ES_OK)
        return st;
    (void)pu32(d, "length", &len);
    if (esk_str_eq(kind, "code")) {
        addr &= ~1u;
        r = esk_str_eq(op, "remove") ? esk_mach_bp_remove(d->m, addr) : esk_mach_bp_add(d->m, addr);
    } else {
        wk = esk_str_eq(kind, "write") ? ESK_WATCH_WRITE : esk_str_eq(kind, "read") ? ESK_WATCH_READ
             : esk_str_eq(kind, "access")                                        ? ESK_WATCH_ACCESS
                                                                                 : 0;
        if (!wk)
            return fail(d, -32602, "kind is code, read, write or access");
        r = esk_str_eq(op, "remove") ? esk_bus_watch_remove(&d->m->bus, wk, addr, len)
                                     : esk_bus_watch_add(&d->m->bus, wk, addr, len ? len : 1);
    }
    if (r != 0)
        return fail(d, -32602, esk_str_eq(op, "remove") ? "no such breakpoint" : "too many breakpoints");
    d->result = obj(d);
    if (!d->result)
        return ES_ENOMEM;
    esk_snprintf(id, sizeof id, "%s:0x%08lx", kind, (unsigned long)addr);
    esk_json_set_string(d->s->a, d->result, "id", id);
    esk_json_set_integer(d->s->a, d->result, "address", (int64_t)addr);
    return ES_OK;
}

static es_status m_breakpoints(dbg *d)
{
    esk_json *arr = esk_json_new_array(d->s->a), *b;
    int i;
    d->result = obj(d);
    if (!d->result || !arr) {
        esk_json_free(d->s->a, arr);
        return ES_ENOMEM;
    }
    for (i = 0; i < d->m->nbp; i++) {
        b = obj(d);
        if (!b)
            break;
        esk_json_set_string(d->s->a, b, "kind", "code");
        describe_pc(d, b, d->m->bp[i]);
        esk_json_array_push(d->s->a, arr, b);
    }
    for (i = 0; i < d->m->bus.nwatch; i++) {
        const struct esk_watch *w = &d->m->bus.watch[i];
        b = obj(d);
        if (!b)
            break;
        esk_json_set_string(d->s->a, b, "kind", w->kind == ESK_WATCH_WRITE ? "write" : w->kind == ESK_WATCH_READ ? "read" : "access");
        esk_json_set_integer(d->s->a, b, "address", (int64_t)w->addr);
        esk_json_set_integer(d->s->a, b, "length", (int64_t)w->len);
        esk_json_array_push(d->s->a, arr, b);
    }
    esk_json_object_set(d->s->a, d->result, "breakpoints", arr);
    return ES_OK;
}

static es_status where(dbg *d)
{
    const char *why = NULL;
    esk_json *o = obj(d);
    if (!o)
        return ES_ENOMEM;
    describe_pc(d, o, d->m->cpu->ops->pc(d->m->cpu));
    esk_json_set_string(d->s->a, o, "board", d->board->id);
    if (esk_mcu_stopped(d->mcu, &why))
        esk_json_set_string(d->s->a, o, "stopped", why ? why : "halt");
    esk_json_set_u64_string(d->s->a, o, "now_ps", d->s->eng.sched.now);
    esk_json_set_integer(d->s->a, o, "instructions", (int64_t)d->m->insns);
    esk_json_set_integer(d->s->a, o, "cycles", (int64_t)d->m->cycles);
    d->result = o;
    return ES_OK;
}

static es_status m_where(dbg *d) { return where(d); }

static es_status m_halt(dbg *d)
{
    esk_mcu_halt(d->mcu);
    d->s->eng.sched.stop_requested = 0;
    return where(d);
}

static es_status m_continue(dbg *d)
{
    esk_mcu_resume(d->mcu, 0);
    return where(d);
}

static es_status m_step(dbg *d)
{
    u32 count = 1;
    es_time_t limit;
    es_status st = ES_OK;
    (void)pu32(d, "count", &count);
    if (!count)
        count = 1;
    if (esk_time_add(d->s->eng.sched.now, STEP_LIMIT_PS, &limit))
        limit = ES_TIME_MAX - 1;
    esk_mcu_resume(d->mcu, count);
    while (st == ES_OK && !esk_mcu_stopped(d->mcu, NULL) && d->s->eng.sched.now < limit) {
        es_time_t until;
        if (esk_time_add(d->s->eng.sched.now, ES_PS_PER_MS, &until) || until > limit)
            until = limit;
        st = esk_session_do_advance(d->s, until);
        d->s->eng.sched.stop_requested = 0;
    }
    if (!esk_mcu_stopped(d->mcu, NULL))
        esk_mcu_halt(d->mcu); /* asleep: no instruction came */
    d->s->eng.sched.stop_requested = 0;
    if (st != ES_OK)
        return fail(d, -32000 + st, es_strerror(st));
    return where(d);
}

static es_status m_registers(dbg *d)
{
    esk_json *arr = esk_json_new_array(d->s->a);
    struct esk_cpu *c = d->m->cpu;
    int n;
    d->result = obj(d);
    if (!d->result || !arr) {
        esk_json_free(d->s->a, arr);
        return ES_ENOMEM;
    }
    for (n = 0; n < 64; n++) {
        u8 buf[8];
        const char *name = c->ops->reg_name ? c->ops->reg_name(c, n) : NULL;
        int len;
        esk_json *r;
        if (!name)
            continue;
        len = c->ops->reg_read(c, n, buf);
        if (len != 1 && len != 2 && len != 4)
            continue;
        r = obj(d);
        if (!r)
            break;
        {
            u32 v = 0;
            int k;
            for (k = len - 1; k >= 0; k--)
                v = v << 8 | buf[k];
            esk_json_set_string(d->s->a, r, "name", name);
            esk_json_set_integer(d->s->a, r, "regnum", n);
            esk_json_set_integer(d->s->a, r, "value", (int64_t)v);
            esk_json_set_integer(d->s->a, r, "size", len);
        }
        esk_json_array_push(d->s->a, arr, r);
    }
    esk_json_object_set(d->s->a, d->result, "regs", arr);
    return ES_OK;
}

static es_status m_set_register(dbg *d)
{
    const char *name = esk_json_get_str(d->params, "name", "");
    struct esk_cpu *c = d->m->cpu;
    u32 v;
    int n;
    if (pu32(d, "value", &v) != 0)
        return fail(d, -32602, "value is required");
    for (n = 0; n < 64; n++) {
        const char *rn = c->ops->reg_name ? c->ops->reg_name(c, n) : NULL;
        if (rn && esk_str_eq(rn, name)) {
            u8 buf[4];
            buf[0] = (u8)v;
            buf[1] = (u8)(v >> 8);
            buf[2] = (u8)(v >> 16);
            buf[3] = (u8)(v >> 24);
            if (c->ops->reg_write(c, n, buf) <= 0)
                break;
            return m_registers(d);
        }
    }
    return fail(d, -32602, "no such register");
}

static es_status m_memory(dbg *d)
{
    u32 addr, len, i;
    unsigned char *buf;
    char *b64;
    size_t cap;
    if (address_of(d, &addr) != ES_OK)
        return ES_EPROTO;
    if (pu32(d, "length", &len) != 0 || !len || len > MAX_MEMORY_READ)
        return fail(d, -32602, "length is 1 to 65536 bytes");
    buf = (unsigned char *)esk_malloc(d->s->a, len);
    cap = 4u * ((len + 2u) / 3u) + 1u;
    b64 = (char *)esk_malloc(d->s->a, cap);
    if (!buf || !b64) {
        esk_free(d->s->a, buf, len);
        esk_free(d->s->a, b64, cap);
        return ES_ENOMEM;
    }
    for (i = 0; i < len; i++) {
        u32 v = 0;
        buf[i] = esk_bus_debug_read(&d->m->bus, addr + i, 1, &v) == 0 ? (unsigned char)v : 0;
    }
    esk_base64_encode(buf, len, b64, cap);
    d->result = obj(d);
    if (d->result) {
        esk_json_set_integer(d->s->a, d->result, "address", (int64_t)addr);
        esk_json_set_string(d->s->a, d->result, "base64", b64);
    }
    esk_free(d->s->a, buf, len);
    esk_free(d->s->a, b64, cap);
    return d->result ? ES_OK : ES_ENOMEM;
}

static es_status m_write_memory(dbg *d)
{
    const char *b64 = esk_json_get_str(d->params, "base64", NULL);
    unsigned char buf[4096];
    size_t n, i;
    u32 addr;
    if (address_of(d, &addr) != ES_OK)
        return ES_EPROTO;
    if (!b64)
        return fail(d, -32602, "base64 is required");
    n = esk_base64_decode(b64, strlen(b64), buf, sizeof buf);
    for (i = 0; i < n; i++)
        if (esk_bus_debug_write(&d->m->bus, addr + (u32)i, 1, buf[i]) != 0)
            return fail(d, -32602, "nothing is mapped at that address");
    d->result = obj(d);
    if (!d->result)
        return ES_ENOMEM;
    esk_json_set_integer(d->s->a, d->result, "written", (int64_t)n);
    return ES_OK;
}

static es_status m_disassemble(dbg *d)
{
    u32 addr = 0, count = 16, i;
    esk_json *arr;
    size_t len = 0;
    const unsigned char *img = image(d, &len);
    if (address_of(d, &addr) != ES_OK) {
        d->code = 0;
        addr = d->m->cpu->ops->pc(d->m->cpu);
    }
    (void)pu32(d, "count", &count);
    if (!count || count > MAX_DISASSEMBLY)
        count = 16;
    if (!esk_avr_is(d->m->cpu))
        addr &= ~1u;
    arr = esk_json_new_array(d->s->a);
    d->result = obj(d);
    if (!arr || !d->result) {
        esk_json_free(d->s->a, arr);
        return ES_ENOMEM;
    }
    for (i = 0; i < count; i++) {
        u32 h1 = 0, h2 = 0, off = 0;
        char text[96], name[128];
        int size;
        esk_json *o = obj(d);
        if (!o)
            break;
        size = d->m->cpu->ops->disasm ? d->m->cpu->ops->disasm(d->m->cpu, addr, text, sizeof text) : 0;
        if (size <= 0) {
            esk_json_free(d->s->a, o);
            break;
        }
        (void)esk_bus_debug_read(&d->m->bus, addr, 2, &h1);
        (void)esk_bus_debug_read(&d->m->bus, addr + 2, 2, &h2);
        esk_json_set_integer(d->s->a, o, "address", (int64_t)addr);
        esk_json_set_integer(d->s->a, o, "size", size);
        esk_json_set_integer(d->s->a, o, "encoding", (int64_t)(size == 4 ? h1 << 16 | h2 : h1));
        esk_json_set_string(d->s->a, o, "text", text);
        if (img && esk_elf_symbol_at(img, len, addr, name, sizeof name, &off) && off == 0)
            esk_json_set_string(d->s->a, o, "label", name);
        esk_json_array_push(d->s->a, arr, o);
        addr += (u32)(size > 0 ? size : 2);
    }
    esk_json_object_set(d->s->a, d->result, "insns", arr);
    return ES_OK;
}

static es_status m_peripherals(dbg *d)
{
    struct esk_rf *rf = d->m->rf;
    esk_json *arr = esk_json_new_array(d->s->a);
    const char *only = esk_json_get_str(d->params, "name", NULL);
    int i, k;
    d->result = obj(d);
    if (!arr || !d->result) {
        esk_json_free(d->s->a, arr);
        return ES_ENOMEM;
    }
    for (i = 0; rf && i < rf->np; i++) {
        struct esk_rf_periph *p = &rf->p[i];
        esk_json *po, *regs;
        if (only && !esk_str_eq(only, p->name))
            continue;
        po = obj(d);
        regs = esk_json_new_array(d->s->a);
        if (!po || !regs) {
            esk_json_free(d->s->a, po);
            esk_json_free(d->s->a, regs);
            break;
        }
        esk_json_set_string(d->s->a, po, "name", p->name);
        esk_json_set_integer(d->s->a, po, "base", (int64_t)p->base);
        esk_json_set_string(d->s->a, po, "model", p->model ? p->model : "registers");
        esk_json_set_bool(d->s->a, po, "clock", !esk_rf_gated(p));
        for (k = 0; k < p->nreg; k++) {
            esk_json *r = obj(d);
            if (!r)
                break;
            esk_json_set_string(d->s->a, r, "name", p->reg[k]->name);
            esk_json_set_integer(d->s->a, r, "offset", (int64_t)(p->reg[k]->addr - p->base));
            esk_json_set_integer(d->s->a, r, "value", (int64_t)(p->reg[k]->value & 0xffffffffu));
            esk_json_array_push(d->s->a, regs, r);
        }
        esk_json_object_set(d->s->a, po, "registers", regs);
        esk_json_array_push(d->s->a, arr, po);
    }
    esk_json_object_set(d->s->a, d->result, "peripherals", arr);
    return ES_OK;
}

/* a source file the image's DWARF names: its text, and the lines with code */
static es_status m_source(dbg *d)
{
    const esk_dwarf *dw = esk_mcu_dwarf(d->mcu);
    const char *want = esk_json_get_str(d->params, "file", NULL), *name;
    char *path = NULL, *text = NULL;
    size_t len = 0, i;
    esk_json *lines;
    u32 last = 0;
    if (!dw)
        return fail(d, -32602, "the image has no DWARF line information (build it with -g)");
    if (!want)
        return fail(d, -32602, "file is required");
    name = esk_dwarf_file(dw, want);
    if (!name)
        return fail(d, -32602, "the image's line tables name no such file");
    /* as named; a relative name from the project's directory */
    path = esk_host_path_join(d->s->a, d->s->base_dir ? d->s->base_dir : ".", name);
    if (!path)
        return ES_ENOMEM;
    if (esk_host_read_file(d->s->a, path, &text, &len) != ES_OK) {
        /* the image's own directory, as a build.sh beside the sources has it */
        const char *img = d->board->firmware ? esk_json_get_str(d->board->firmware, "image", NULL) : NULL;
        char *dir = NULL, *base = NULL;
        if (img) {
            base = esk_host_path_join(d->s->a, d->s->base_dir ? d->s->base_dir : ".", img);
            dir = base ? esk_host_dirname(d->s->a, base) : NULL;
        }
        esk_strfree(d->s->a, path);
        path = dir ? esk_host_path_join(d->s->a, dir, name) : NULL;
        esk_strfree(d->s->a, dir);
        esk_strfree(d->s->a, base);
        if (!path || esk_host_read_file(d->s->a, path, &text, &len) != ES_OK) {
            esk_strfree(d->s->a, path);
            return fail(d, -32602, "the source file cannot be read");
        }
    }
    d->result = obj(d);
    lines = esk_json_new_array(d->s->a);
    if (d->result && lines) {
        esk_json_set_string(d->s->a, d->result, "file", name);
        esk_json_set_string(d->s->a, d->result, "path", path);
        esk_json_set_string(d->s->a, d->result, "text", text);
        /* the lines a breakpoint can go on, ascending */
        u32 *ln = (u32 *)esk_malloc(d->s->a, (dw->nrows ? dw->nrows : 1) * sizeof *ln);
        size_t n = 0, gap, k, j;
        for (i = 0; ln && i < dw->nrows; i++) {
            const esk_dwarf_row *r = &dw->rows[i];
            if (!r->end && r->stmt && r->file >= 0 && !strcmp(dw->files[r->file], name))
                ln[n++] = r->line;
        }
        for (gap = n / 2; gap > 0; gap /= 2)
            for (k = gap; k < n; k++) {
                u32 t = ln[k];
                for (j = k; j >= gap && ln[j - gap] > t; j -= gap)
                    ln[j] = ln[j - gap];
                ln[j] = t;
            }
        for (k = 0; k < n; k++)
            if (!k || ln[k] != last) {
                esk_json_array_push(d->s->a, lines, esk_json_new_integer(d->s->a, (int64_t)ln[k]));
                last = ln[k];
            }
        if (ln)
            esk_free(d->s->a, ln, (dw->nrows ? dw->nrows : 1) * sizeof *ln);
        esk_json_object_set(d->s->a, d->result, "lines", lines);
        lines = NULL;
    }
    esk_json_free(d->s->a, lines);
    esk_free(d->s->a, text, len + 1);
    esk_strfree(d->s->a, path);
    return d->result ? ES_OK : ES_ENOMEM;
}

static es_status m_symbol(dbg *d)
{
    u32 addr = 0;
    es_status st = address_of(d, &addr);
    if (st != ES_OK)
        return st;
    d->result = obj(d);
    if (!d->result)
        return ES_ENOMEM;
    describe_pc(d, d->result, addr);
    esk_json_set_integer(d->s->a, d->result, "address", (int64_t)addr);
    return ES_OK;
}

typedef struct dbg_method {
    const char *name;
    es_status (*fn)(dbg *);
} dbg_method;

static const dbg_method dbg_methods[] = {
    { "debug.breakpoint", m_breakpoint }, { "debug.breakpoints", m_breakpoints },
    { "debug.where", m_where },           { "debug.halt", m_halt },
    { "debug.continue", m_continue },     { "debug.step", m_step },
    { "debug.registers", m_registers },   { "debug.set_register", m_set_register },
    { "debug.memory", m_memory },         { "debug.write_memory", m_write_memory },
    { "debug.disassemble", m_disassemble }, { "debug.peripherals", m_peripherals },
    { "debug.symbol", m_symbol },         { "debug.source", m_source },
};

int esk_debug_has(const char *method)
{
    size_t i;
    for (i = 0; i < ES_COUNTOF(dbg_methods); i++)
        if (!strcmp(dbg_methods[i].name, method))
            return 1;
    return 0;
}

es_status esk_debug_call(es_session *s, const char *method, const esk_json *params, esk_json **result,
                         int *code, char *msg, size_t cap)
{
    dbg d;
    size_t i;
    es_status st;
    memset(&d, 0, sizeof d);
    d.s = s;
    d.params = params;
    *result = NULL;
    *code = 0;
    st = find_board(&d);
    for (i = 0; st == ES_OK && i < ES_COUNTOF(dbg_methods); i++)
        if (!strcmp(dbg_methods[i].name, method)) {
            st = dbg_methods[i].fn(&d);
            break;
        }
    if (d.code) {
        esk_json_free(s->a, d.result);
        *code = d.code;
        esk_snprintf(msg, cap, "%s", d.msg);
        return ES_EPROTO;
    }
    *result = d.result;
    return st;
}

int esk_debug_stopped_board(es_session *s, esk_json **info)
{
    size_t i;
    for (i = 0; i < s->boards.len; i++) {
        esk_board_inst *bi = s->boards.data[i];
        const char *why = NULL;
        if (bi->mcu && esk_mcu_stopped(bi->mcu, &why)) {
            if (info) {
                dbg d;
                memset(&d, 0, sizeof d);
                d.s = s;
                d.board = bi;
                d.mcu = bi->mcu;
                d.m = esk_mcu_machine(bi->mcu);
                *info = NULL;
                if (d.m && where(&d) == ES_OK) {
                    esk_json_set_string(s->a, d.result, "reason", why ? why : "halt");
                    *info = d.result;
                }
            }
            return 1;
        }
    }
    return 0;
}

void esk_debug_resume_all(es_session *s)
{
    size_t i;
    for (i = 0; i < s->boards.len; i++) {
        esk_board_inst *bi = s->boards.data[i];
        if (bi->mcu && esk_mcu_stopped(bi->mcu, NULL))
            esk_mcu_resume(bi->mcu, 0);
    }
}
