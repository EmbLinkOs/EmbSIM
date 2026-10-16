/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Portions copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC): the call frame information */
/* dwarf.c -- source lines from an ELF image's DWARF line tables (.debug_line,
 * versions 2 to 5, 32-bit DWARF), for the debugger (SPEC-009 §10): the file
 * and line of an address, and the addresses of a file's line.
 *
 * Every line program is run into rows (address, file, line, is_stmt); rows
 * are kept sorted by address with each sequence's end. A file is named as
 * the table names it, joined to its directory when relative; matching a
 * request ("main.c", "firmware/main.c") compares path suffixes at a
 * separator. Version 5's entry formats are read for the forms compilers
 * use (string, line_strp, strp, udata, data1-16, block). An XC16 image's
 * (EM_DSPIC30F) debug sections hold each byte in the low half of a 16-bit
 * unit, as its program memory does: they are read packed.
 *
 * The call frame information (.debug_frame) for the fault analysis's
 * backtraces (fault.c): the rules at a pc, from its FDE's and its CIE's
 * instructions; DWARF expressions are not read (EmbCC's image.c cfi_at). */
#include <string.h>

#include "mcu/dwarf.h"
#include "util/str.h"

#define MAX_FILES 4096
#define MAX_ROWS 1000000

static u32 le16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }
static u32 le32(const u8 *p) { return le16(p) | le16(p + 2) << 16; }

typedef struct rd {
    const u8 *p, *end;
    int bad;
} rd;

static u32 u8_(rd *r)
{
    if (r->p >= r->end) {
        r->bad = 1;
        return 0;
    }
    return *r->p++;
}

static u32 u16_(rd *r)
{
    u32 v;
    if (r->end - r->p < 2) {
        r->bad = 1;
        return 0;
    }
    v = le16(r->p);
    r->p += 2;
    return v;
}

static u32 u32_(rd *r)
{
    u32 v;
    if (r->end - r->p < 4) {
        r->bad = 1;
        return 0;
    }
    v = le32(r->p);
    r->p += 4;
    return v;
}

static u64 uleb(rd *r)
{
    u64 v = 0;
    int shift = 0;
    for (;;) {
        u32 b = u8_(r);
        if (r->bad)
            return 0;
        if (shift < 64)
            v |= (u64)(b & 0x7fu) << shift;
        shift += 7;
        if (!(b & 0x80u))
            return v;
    }
}

static s64 sleb(rd *r)
{
    u64 v = 0;
    int shift = 0;
    u32 b;
    do {
        b = u8_(r);
        if (r->bad)
            return 0;
        if (shift < 64)
            v |= (u64)(b & 0x7fu) << shift;
        shift += 7;
    } while (b & 0x80u);
    if (shift < 64 && (b & 0x40u))
        v |= ~(u64)0 << shift;
    return (s64)v;
}

static const char *cstr(rd *r)
{
    const u8 *s = r->p;
    while (r->p < r->end && *r->p)
        r->p++;
    if (r->p >= r->end) {
        r->bad = 1;
        return "";
    }
    r->p++;
    return (const char *)s;
}

/* ---- the ELF's sections ------------------------------------------------------ */

static const u8 *section(const u8 *f, size_t len, const char *want, u32 *size)
{
    u32 shoff, shentsize, shnum, shstrndx, i;
    const u8 *strtab;
    if (len < 52 || memcmp(f, "\177ELF", 4) != 0 || f[4] != 1)
        return NULL;
    shoff = le32(f + 32);
    shentsize = le16(f + 46);
    shnum = le16(f + 48);
    shstrndx = le16(f + 50);
    if (shentsize < 40 || shstrndx >= shnum || (u64)shoff + (u64)shnum * shentsize > len)
        return NULL;
    strtab = f + shoff + (size_t)shstrndx * shentsize;
    if ((u64)le32(strtab + 16) + le32(strtab + 20) > len)
        return NULL;
    for (i = 0; i < shnum; i++) {
        const u8 *sh = f + shoff + (size_t)i * shentsize;
        u32 name = le32(sh), off = le32(sh + 16), sz = le32(sh + 20);
        const char *n;
        if (name >= le32(strtab + 20))
            continue;
        n = (const char *)f + le32(strtab + 16) + name;
        if (strcmp(n, want) != 0 || (u64)off + sz > len)
            continue;
        *size = sz;
        return f + off;
    }
    return NULL;
}

/* ---- building ------------------------------------------------------------------- */

static int add_file(esk_dwarf *d, const char *dir, const char *name)
{
    size_t dl = dir ? strlen(dir) : 0, nl = strlen(name), n;
    char *p;
    if (d->nfiles == MAX_FILES)
        return -1;
    if (d->nfiles == d->cap_files) {
        size_t cap = d->cap_files ? d->cap_files * 2 : 16;
        char **nf = (char **)esk_realloc(d->a, d->files, d->cap_files * sizeof *d->files, cap * sizeof *d->files);
        if (!nf)
            return -1;
        d->files = nf;
        d->cap_files = cap;
    }
    if (name[0] == '/' || (nl > 2 && name[1] == ':') || !dl) {
        p = esk_strdup(d->a, name);
    } else {
        n = dl + nl + 2;
        p = (char *)esk_malloc(d->a, n);
        if (p) {
            memcpy(p, dir, dl);
            p[dl] = '/';
            memcpy(p + dl + 1, name, nl + 1);
            /* sized as esk_strdup's, for esk_strfree */
            {
                char *q = esk_strdup(d->a, p);
                esk_free(d->a, p, n);
                p = q;
            }
        }
    }
    if (!p)
        return -1;
    d->files[d->nfiles] = p;
    return (int)d->nfiles++;
}

static int add_row(esk_dwarf *d, u32 addr, int file, u32 line, int stmt, int end)
{
    esk_dwarf_row *r;
    if (d->nrows == MAX_ROWS)
        return -1;
    if (d->nrows == d->cap_rows) {
        size_t cap = d->cap_rows ? d->cap_rows * 2 : 256;
        esk_dwarf_row *nr = (esk_dwarf_row *)esk_realloc(d->a, d->rows, d->cap_rows * sizeof *d->rows, cap * sizeof *d->rows);
        if (!nr)
            return -1;
        d->rows = nr;
        d->cap_rows = cap;
    }
    r = &d->rows[d->nrows++];
    r->addr = addr;
    r->file = file;
    r->line = line;
    r->stmt = (u8)(stmt != 0);
    r->end = (u8)(end != 0);
    return 0;
}

/* a form's value: a string (for paths), or a number */
static const char *form(rd *r, u64 f, const u8 *line_str, u32 line_str_len, const u8 *str, u32 str_len, u64 *num)
{
    u32 off;
    *num = 0;
    switch (f) {
    case 0x08: return cstr(r);                                 /* string */
    case 0x1f:                                                 /* line_strp */
    case 0x0e:                                                 /* strp */
        off = u32_(r);
        if (f == 0x1f ? (line_str && off < line_str_len) : (str && off < str_len))
            return (const char *)(f == 0x1f ? line_str : str) + off;
        return "";
    case 0x0f: *num = uleb(r); return NULL;                    /* udata */
    case 0x0b: *num = u8_(r); return NULL;                     /* data1 */
    case 0x05: *num = u16_(r); return NULL;                    /* data2 */
    case 0x06: *num = u32_(r); return NULL;                    /* data4 */
    case 0x07: *num = u32_(r); *num |= (u64)u32_(r) << 32; return NULL; /* data8 */
    case 0x1e: r->p += 16; if (r->p > r->end) r->bad = 1; return NULL; /* data16 (MD5) */
    case 0x09: {                                               /* block */
        u64 n = uleb(r);
        if ((u64)(r->end - r->p) < n) r->bad = 1; else r->p += n;
        return NULL;
    }
    default:
        r->bad = 1;
        return NULL;
    }
}

/* one unit's line program */
static int unit(esk_dwarf *d, rd *r, const u8 *line_str, u32 line_str_len, const u8 *str, u32 str_len)
{
    u32 ver, hlen, min_inst, ops_base, i, k;
    s32 line_base;
    u32 line_range;
    u8 std_len[256];
    const u8 *prog;
    int dirs_first = (int)d->nfiles, file_base;
    const char *dirs[256];
    u32 ndirs = 0;
    u32 addr = 0, line = 1;
    int file = 1, stmt;
    int default_stmt;
    rd hdr;
    ver = u16_(r);
    if (ver < 2 || ver > 5)
        return -1;
    if (ver >= 5) {
        u32 asz = u8_(r);
        (void)u8_(r);
        if (asz != 4 && asz != 2) /* 2: an AVR's */
            return -1;
    }
    hlen = u32_(r);
    if (r->bad || (u64)(r->end - r->p) < hlen)
        return -1;
    prog = r->p + hlen;
    hdr = *r;
    hdr.end = prog;
    min_inst = u8_(&hdr);
    if (ver >= 4)
        (void)u8_(&hdr);
    default_stmt = (int)u8_(&hdr);
    line_base = (s32)(int8_t)u8_(&hdr);
    line_range = u8_(&hdr);
    ops_base = u8_(&hdr);
    if (!line_range || !ops_base)
        return -1;
    memset(std_len, 0, sizeof std_len);
    for (i = 1; i < ops_base; i++)
        std_len[i] = (u8)u8_(&hdr);
    (void)dirs_first;
    if (ver < 5) {
        dirs[ndirs++] = NULL; /* 0: the compilation directory, unknown here */
        for (;;) {
            const char *s = cstr(&hdr);
            if (hdr.bad || !*s)
                break;
            if (ndirs < 256)
                dirs[ndirs++] = s;
        }
        file_base = (int)d->nfiles - 1; /* files count from 1 */
        for (;;) {
            const char *s = cstr(&hdr);
            u64 di;
            if (hdr.bad || !*s)
                break;
            di = uleb(&hdr);
            (void)uleb(&hdr);
            (void)uleb(&hdr);
            if (add_file(d, di < ndirs ? dirs[di] : NULL, s) < 0)
                return -1;
        }
    } else {
        u64 fmt[16][2], nfmt, n;
        nfmt = u8_(&hdr);
        if (nfmt > 16)
            return -1;
        for (k = 0; k < nfmt; k++) {
            fmt[k][0] = uleb(&hdr);
            fmt[k][1] = uleb(&hdr);
        }
        n = uleb(&hdr);
        for (i = 0; i < n && !hdr.bad; i++) {
            const char *path = NULL;
            for (k = 0; k < nfmt; k++) {
                u64 num;
                const char *s = form(&hdr, fmt[k][1], line_str, line_str_len, str, str_len, &num);
                if (fmt[k][0] == 1)
                    path = s;
            }
            if (ndirs < 256)
                dirs[ndirs++] = path;
        }
        nfmt = u8_(&hdr);
        if (nfmt > 16)
            return -1;
        for (k = 0; k < nfmt; k++) {
            fmt[k][0] = uleb(&hdr);
            fmt[k][1] = uleb(&hdr);
        }
        n = uleb(&hdr);
        file_base = (int)d->nfiles; /* files count from 0 */
        for (i = 0; i < n && !hdr.bad; i++) {
            const char *path = "";
            u64 di = 0;
            for (k = 0; k < nfmt; k++) {
                u64 num;
                const char *s = form(&hdr, fmt[k][1], line_str, line_str_len, str, str_len, &num);
                if (fmt[k][0] == 1 && s)
                    path = s;
                else if (fmt[k][0] == 2)
                    di = num;
            }
            if (add_file(d, di < ndirs && di ? dirs[di] : (ndirs ? dirs[0] : NULL), path) < 0)
                return -1;
        }
        file = 1;
    }
    if (hdr.bad)
        return -1;
    /* the program */
    r->p = prog;
    stmt = default_stmt;
    while (r->p < r->end && !r->bad) {
        u32 op = u8_(r);
        if (op >= ops_base) {
            u32 adj = op - ops_base;
            addr += (adj / line_range) * min_inst;
            line = (u32)((s32)line + line_base + (s32)(adj % line_range));
            if (add_row(d, addr, file_base + file, line, stmt, 0) < 0)
                return -1;
            continue;
        }
        switch (op) {
        case 0: {
            u64 n = uleb(r);
            const u8 *next = r->p + n;
            u32 sub;
            if (!n || (u64)(r->end - r->p) < n)
                return -1;
            sub = u8_(r);
            if (sub == 1) {
                if (add_row(d, addr, file_base + file, line, stmt, 1) < 0)
                    return -1;
                addr = 0;
                line = 1;
                file = 1;
                stmt = default_stmt;
            } else if (sub == 2 && n == 5) {
                addr = u32_(r);
            } else if (sub == 2 && n == 3) {
                addr = u16_(r); /* DW_LNE_set_address with 2-byte addresses */
            }
            r->p = next;
            break;
        }
        case 1:
            if (add_row(d, addr, file_base + file, line, stmt, 0) < 0)
                return -1;
            break;
        case 2: addr += (u32)uleb(r) * min_inst; break;
        case 3: line = (u32)((s64)line + sleb(r)); break;
        case 4: file = (int)uleb(r); break;
        case 5: (void)uleb(r); break;
        case 6: stmt = !stmt; break;
        case 7: break;
        case 8: addr += ((255u - ops_base) / line_range) * min_inst; break;
        case 9: addr += u16_(r); break;
        case 10:
        case 11: break;
        case 12: (void)uleb(r); break;
        default:
            for (k = 0; k < std_len[op]; k++)
                (void)uleb(r);
            break;
        }
    }
    return r->bad ? -1 : 0;
}

static int row_cmp(const esk_dwarf_row *a, const esk_dwarf_row *b)
{
    if (a->addr != b->addr)
        return a->addr < b->addr ? -1 : 1;
    return (int)b->end - (int)a->end; /* a sequence's end before the next one's start */
}

static void sort_rows(esk_dwarf_row *r, size_t n)
{
    /* a shell sort: deterministic, no recursion, no allocation */
    size_t gap, i, j;
    for (gap = n / 2; gap > 0; gap /= 2)
        for (i = gap; i < n; i++) {
            esk_dwarf_row t = r[i];
            for (j = i; j >= gap && row_cmp(&r[j - gap], &t) > 0; j -= gap)
                r[j] = r[j - gap];
            r[j] = t;
        }
}

#define EM_DSPIC30F 118

/* a section of an XC16 image packed: its even bytes; NULL when out of memory */
static u8 *packed(esk_alloc *a, const u8 *sec, u32 *size)
{
    u32 i, n = *size / 2;
    u8 *p = (u8 *)esk_malloc(a, n ? n : 1);
    if (!p)
        return NULL;
    for (i = 0; i < n; i++)
        p[i] = sec[2 * i];
    *size = n;
    return p;
}

static es_status load(esk_dwarf *d, const u8 *sec, u32 size, const u8 *line_str, u32 line_str_len,
                      const u8 *str, u32 str_len);

es_status esk_dwarf_load(esk_dwarf *d, esk_alloc *a, const u8 *f, size_t len)
{
    u32 size = 0, line_str_len = 0, str_len = 0, n[3];
    const u8 *sec, *line_str, *str;
    u8 *pk[3] = { NULL, NULL, NULL };
    es_status st;
    int i;
    memset(d, 0, sizeof *d);
    d->a = a;
    sec = section(f, len, ".debug_line", &size);
    if (!sec)
        return ES_ENOTFOUND;
    line_str = section(f, len, ".debug_line_str", &line_str_len);
    str = section(f, len, ".debug_str", &str_len);
    if (le16(f + 18) != EM_DSPIC30F)
        return load(d, sec, size, line_str, line_str_len, str, str_len);
    n[0] = size;
    n[1] = line_str_len;
    n[2] = str_len;
    pk[0] = packed(a, sec, &size);
    pk[1] = line_str ? packed(a, line_str, &line_str_len) : NULL;
    pk[2] = str ? packed(a, str, &str_len) : NULL;
    st = !pk[0] || (line_str && !pk[1]) || (str && !pk[2])
             ? ES_ENOMEM
             : load(d, pk[0], size, pk[1], line_str_len, pk[2], str_len);
    for (i = 0; i < 3; i++)
        if (pk[i])
            esk_free(a, pk[i], n[i] / 2 ? n[i] / 2 : 1);
    return st;
}

/* every unit of .debug_line into rows */
static es_status load(esk_dwarf *d, const u8 *sec, u32 size, const u8 *line_str, u32 line_str_len,
                      const u8 *str, u32 str_len)
{
    rd r;
    r.p = sec;
    r.end = sec + size;
    r.bad = 0;
    while (r.end - r.p >= 4) {
        u32 ulen = le32(r.p);
        rd u;
        if (ulen == 0xffffffffu || (u64)(r.end - r.p - 4) < ulen)
            break; /* 64-bit DWARF, or a truncated unit */
        u.p = r.p + 4;
        u.end = r.p + 4 + ulen;
        u.bad = 0;
        if (unit(d, &u, line_str, line_str_len, str, str_len) != 0 && d->nrows == 0) {
            esk_dwarf_free(d);
            return ES_EPARSE;
        }
        r.p += 4 + (size_t)ulen;
    }
    sort_rows(d->rows, d->nrows);
    return d->nrows ? ES_OK : ES_ENOTFOUND;
}

void esk_dwarf_free(esk_dwarf *d)
{
    size_t i;
    for (i = 0; i < d->nfiles; i++)
        esk_strfree(d->a, d->files[i]);
    if (d->files)
        esk_free(d->a, d->files, d->cap_files * sizeof *d->files);
    if (d->rows)
        esk_free(d->a, d->rows, d->cap_rows * sizeof *d->rows);
    memset(d, 0, sizeof *d);
}

int esk_dwarf_line_at(const esk_dwarf *d, u32 addr, const char **file, u32 *line)
{
    size_t lo = 0, hi = d->nrows;
    const esk_dwarf_row *r;
    while (lo < hi) { /* the first row past addr */
        size_t mid = (lo + hi) / 2;
        if (d->rows[mid].addr <= addr)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (!lo)
        return 0;
    r = &d->rows[lo - 1];
    if (r->end || r->file < 0 || (size_t)r->file >= d->nfiles)
        return 0;
    *file = d->files[r->file];
    *line = r->line;
    return 1;
}

/* `want` names `path`: equal, or a suffix at a separator */
static int names(const char *path, const char *want)
{
    size_t pl = strlen(path), wl = strlen(want);
    while (wl > 2 && want[0] == '.' && (want[1] == '/' || want[1] == '\\')) {
        want += 2;
        wl -= 2;
    }
    if (wl > pl)
        return 0;
    if (strcmp(path + pl - wl, want) != 0)
        return 0;
    return pl == wl || path[pl - wl - 1] == '/' || path[pl - wl - 1] == '\\';
}

int esk_dwarf_line_addrs(const esk_dwarf *d, const char *file, u32 line, u32 *addrs, int cap)
{
    size_t i;
    int n = 0, k;
    for (i = 0; i < d->nrows && n < cap; i++) {
        const esk_dwarf_row *r = &d->rows[i];
        if (r->end || !r->stmt || r->line != line || r->file < 0 || !names(d->files[r->file], file))
            continue;
        for (k = 0; k < n && addrs[k] != r->addr; k++)
            ;
        if (k == n)
            addrs[n++] = r->addr;
    }
    return n;
}

const char *esk_dwarf_file(const esk_dwarf *d, const char *file)
{
    size_t i;
    for (i = 0; i < d->nfiles; i++)
        if (names(d->files[i], file))
            return d->files[i];
    return NULL;
}

/* ---- the call frame information (.debug_frame) ----------------------------------- */

typedef struct cie {
    u32 code_align;
    s64 data_align;
    int ra;
    const u8 *init, *init_end;
} cie;

static int read_cie(const u8 *fr, u32 frlen, u32 off, cie *c)
{
    rd r;
    u32 len, ver;
    const char *aug;
    r.p = fr + off;
    r.end = fr + frlen;
    r.bad = 0;
    len = u32_(&r);
    if (r.bad || len >= 0xfffffff0u || len > (u32)(r.end - r.p))
        return 0;
    r.end = r.p + len;
    if (u32_(&r) != 0xffffffffu)
        return 0;
    ver = u8_(&r);
    aug = cstr(&r);
    if (*aug || (ver != 1 && ver != 3 && ver != 4))
        return 0;
    if (ver == 4) {
        (void)u8_(&r); /* address_size */
        (void)u8_(&r); /* segment_selector_size */
    }
    c->code_align = (u32)uleb(&r);
    c->data_align = sleb(&r);
    c->ra = (int)(ver == 1 ? u8_(&r) : uleb(&r));
    c->init = r.p;
    c->init_end = r.end;
    return !r.bad;
}

static void set_rule(esk_cfi_row *row, u64 reg, int how, s64 off)
{
    if (reg < ESK_CFI_REGS) {
        row->how[reg] = (u8)how;
        row->off[reg] = off;
    }
}

static void restore_rule(esk_cfi_row *row, u64 reg, const esk_cfi_row *init)
{
    if (reg < ESK_CFI_REGS && init) {
        row->how[reg] = init->how[reg];
        row->off[reg] = init->off[reg];
    }
}

/* the CFI instructions from address `loc` up to the row for `pc`; `init`
 * the CIE's row, which DW_CFA_restore goes back to (NULL while running the
 * CIE's own instructions). 0 for what is not understood (an expression) */
static int run_cfi(const u8 *p, const u8 *end, const cie *c, u64 loc, u32 pc, esk_cfi_row *row,
                   const esk_cfi_row *init)
{
    rd r;
    esk_cfi_row stack[8];
    int sp = 0;
    r.p = p;
    r.end = end;
    r.bad = 0;
    while (r.p < r.end && !r.bad) {
        u32 op = u8_(&r), hi = op & 0xc0u, lo = op & 0x3fu;
        u64 reg = lo, adv = 0;
        if (hi == 0x40u) {
            adv = lo; /* advance_loc */
        } else if (hi == 0x80u) {
            set_rule(row, reg, ESK_CFI_OFFSET, (s64)uleb(&r) * c->data_align); /* offset */
            continue;
        } else if (hi == 0xc0u) {
            restore_rule(row, reg, init); /* restore */
            continue;
        } else {
            switch (op) {
            case 0x00: /* nop */
                continue;
            case 0x01: { /* set_loc */
                u64 to = u32_(&r);
                if (to > pc)
                    return 1;
                loc = to;
                continue;
            }
            case 0x02: adv = u8_(&r); break;
            case 0x03: adv = u16_(&r); break;
            case 0x04: adv = u32_(&r); break;
            case 0x05: /* offset_extended */
            case 0x11: /* offset_extended_sf */
            case 0x14: /* val_offset */
            case 0x15: { /* val_offset_sf */
                s64 off;
                reg = uleb(&r);
                off = (op == 0x05 || op == 0x14 ? (s64)uleb(&r) : sleb(&r)) * c->data_align;
                set_rule(row, reg, op >= 0x14 ? ESK_CFI_VALOFF : ESK_CFI_OFFSET, off);
                continue;
            }
            case 0x06: /* restore_extended */
                restore_rule(row, uleb(&r), init);
                continue;
            case 0x07: /* undefined */
            case 0x08: /* same_value */
                reg = uleb(&r);
                set_rule(row, reg, op == 0x07 ? ESK_CFI_UNDEF : ESK_CFI_SAME, 0);
                continue;
            case 0x09: { /* register */
                u64 r2;
                reg = uleb(&r);
                r2 = uleb(&r);
                set_rule(row, reg, ESK_CFI_REG, (s64)r2);
                continue;
            }
            case 0x0a: /* remember_state */
                if (sp == 8)
                    return 0;
                stack[sp++] = *row;
                continue;
            case 0x0b: /* restore_state: the CFA rule with them */
                if (!sp)
                    return 0;
                *row = stack[--sp];
                continue;
            case 0x0c: /* def_cfa */
                row->cfa_reg = (int)uleb(&r);
                row->cfa_off = (s64)uleb(&r);
                continue;
            case 0x0d: /* def_cfa_register */
                row->cfa_reg = (int)uleb(&r);
                continue;
            case 0x0e: /* def_cfa_offset */
                row->cfa_off = (s64)uleb(&r);
                continue;
            case 0x12: /* def_cfa_sf */
                row->cfa_reg = (int)uleb(&r);
                row->cfa_off = sleb(&r) * c->data_align;
                continue;
            case 0x13: /* def_cfa_offset_sf */
                row->cfa_off = sleb(&r) * c->data_align;
                continue;
            case 0x2e: /* GNU_args_size */
                (void)uleb(&r);
                continue;
            default: /* the expressions */
                return 0;
            }
        }
        /* an advance: the row so far holds up to loc + adv */
        loc += adv * c->code_align;
        if (loc > pc)
            return 1;
    }
    return !r.bad;
}

int esk_dwarf_cfi(const u8 *elf, size_t len, u32 pc, esk_cfi_row *row)
{
    u32 frlen = 0, off = 0;
    const u8 *fr = section(elf, len, ".debug_frame", &frlen);
    if (!fr)
        return 0;
    while (frlen - off >= 8) {
        rd r;
        u32 n, next, cie_ptr, start, range;
        r.p = fr + off;
        r.end = fr + frlen;
        r.bad = 0;
        n = u32_(&r);
        if (!n || n >= 0xfffffff0u || n > (u32)(r.end - r.p))
            return 0;
        next = off + 4 + n;
        r.end = r.p + n;
        cie_ptr = u32_(&r);
        if (cie_ptr == 0xffffffffu) {
            off = next;
            continue;
        }
        start = u32_(&r);
        range = u32_(&r);
        if (!r.bad && pc - start < range) {
            cie c;
            esk_cfi_row init;
            if (cie_ptr >= frlen || !read_cie(fr, frlen, cie_ptr, &c))
                return 0;
            memset(&init, 0, sizeof init);
            init.ra = c.ra;
            if (!run_cfi(c.init, c.init_end, &c, 0, 0xffffffffu, &init, NULL))
                return 0;
            *row = init;
            return run_cfi(r.p, r.end, &c, start, pc, row, &init);
        }
        off = next;
    }
    return 0;
}
