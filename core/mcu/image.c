/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* image.c -- firmware images into the machine's memory (SPEC-009 §10, SPEC-010
 * §4): an ELF file's PT_LOAD segments at their physical (load) addresses, as a
 * flash programmer places them, or an Intel HEX file's data records at bus
 * addresses; for a part whose flash is a store no address reaches (an
 * ESP32-C3's), any other file is a flash image from offset 0. A segment that falls outside the described memory is an error
 * naming the address: the MCU description, not the image, says where memory
 * is. Flash is written here; the core's stores to it are ignored afterwards.
 *
 * Provenance: EmbLinkOs/EmbCC aa4f10a tools/embsim/loader.c, reworked to read
 * from a buffer (the session reads files through the host layer) and to
 * return statuses instead of exiting. */
#include <stdio.h>
#include <string.h>

#include "mcu/mach.h"

static u32 le16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }
static u32 le32(const u8 *p) { return le16(p) | le16(p + 2) << 16; }

/* `n` bytes at bus address `pa`, into memory regions or a device's registers */
static es_status put(struct esk_mach *m, u32 pa, const u8 *b, u32 n, char *why, size_t cap)
{
    u32 k, x;
    if (!esk_bus_region(&m->bus, pa, 1) && esk_bus_debug_read(&m->bus, pa, 1, &x) == 0) {
        /* a segment for a device's registers, written as a debugger writes */
        for (k = 0; k < n; k++)
            if (esk_bus_debug_write(&m->bus, pa + k, 1, b[k]) != 0)
                break;
        if (k == n)
            return ES_OK;
    }
    for (k = 0; k < n; k++) {
        struct esk_region *r = esk_bus_region(&m->bus, pa + k, 1);
        if (!r) {
            snprintf(why, cap, "the image has bytes at 0x%08lx, outside the MCU's memory",
                     (unsigned long)(pa + k));
            return ES_EINVAL;
        }
        if (r->kind == ESK_MEM_WINDOW) {
            snprintf(why, cap,
                     "the image has bytes at 0x%08lx, in the cache's window on the flash: the flash takes "
                     "a flash image (a .bin as esptool writes it at 0x0, such as merge_bin's)",
                     (unsigned long)(pa + k));
            return ES_EINVAL;
        }
        r->mem[pa + k - r->base] = b[k];
    }
    esk_bus_code_changed(&m->bus);
    return ES_OK;
}

static es_status load_hex(struct esk_mach *m, const u8 *f, size_t len, u32 *entry, char *why,
                          size_t cap);

/* A flash image for a part whose flash is a store (an ESP32-C3's): its
 * bytes from offset 0, as esptool's write_flash 0x0 puts them -- each 4 KB
 * sector it touches erased first, the rest of the flash as it was. Only at
 * a power-on (a new image, the session's start): through the part's other
 * resets the flash keeps what the program wrote there. */
static es_status load_flash(struct esk_mach *m, struct esk_store *fl, const u8 *f, size_t len, char *why,
                            size_t cap)
{
    size_t end;
    if (len > fl->size) {
        snprintf(why, cap, "the flash image is %lu bytes, more than the %lu of the part's flash",
                 (unsigned long)len, (unsigned long)fl->size);
        return ES_EINVAL;
    }
    if (m->reset_cause != ESK_RESET_POWER)
        return ES_OK;
    end = (len + 4095u) & ~(size_t)4095u;
    memset(fl->mem, 0xff, end < fl->size ? end : fl->size);
    memcpy(fl->mem, f, len);
    return ES_OK;
}

es_status esk_mach_load_image(struct esk_mach *m, const u8 *f, size_t len, u32 *entry, char *why,
                              size_t cap)
{
    size_t sp = 0;
    u32 phoff, phentsize, phnum, i;
    int loaded = 0, r;
    es_status st;
    *entry = 0;
    why[0] = 0;
    for (r = 0; r < m->bus.nrg; r++)
        m->bus.rg[r].rom = 0;
    while (sp < len && (f[sp] == ' ' || f[sp] == '\r' || f[sp] == '\n'))
        sp++;
    if (sp < len && f[sp] == ':') {
        st = load_hex(m, f, len, entry, why, cap);
        goto done;
    }
    st = ES_EINVAL;
    if (len < 52 || memcmp(f, "\177ELF", 4) != 0) {
        struct esk_store *fl = esk_mach_store(m, "flash");
        if (fl && len) {
            st = load_flash(m, fl, f, len, why, cap);
            goto done;
        }
        snprintf(why, cap, "not an ELF file or an Intel HEX image");
        goto done;
    }
    if (f[4] != 1 || f[5] != 1) {
        snprintf(why, cap, "not a 32-bit little-endian ELF image");
        goto done;
    }
    if (le16(f + 18) != (u32)m->cpu->ops->elf_machine) {
        snprintf(why, cap, "not %s (e_machine %lu)", m->cpu->ops->elf_name,
                 (unsigned long)le16(f + 18));
        goto done;
    }
    if (le16(f + 16) != 2) {
        snprintf(why, cap, "not a linked image (ET_EXEC)");
        goto done;
    }
    phoff = le32(f + 28);
    phentsize = le16(f + 42);
    phnum = le16(f + 44);
    *entry = le32(f + 24);
    if (phentsize < 32 || phnum > 4096) {
        snprintf(why, cap, "bad program headers");
        goto done;
    }
    for (i = 0; i < phnum; i++) {
        const u8 *ph;
        u32 off, pa, fsz;
        if ((u64)phoff + (u64)i * phentsize + 32u > len) {
            snprintf(why, cap, "truncated program headers");
            goto done;
        }
        ph = f + phoff + (size_t)i * phentsize;
        if (le32(ph) != 1) /* PT_LOAD */
            continue;
        off = le32(ph + 4);
        pa = le32(ph + 12);
        fsz = le32(ph + 16);
        if (!fsz)
            continue;
        if ((u64)off + fsz > len) {
            snprintf(why, cap, "a segment runs past the end of the file");
            goto done;
        }
        if ((u64)pa + fsz - 1u > 0xffffffffu) {
            snprintf(why, cap, "a segment at 0x%08lx runs past the 32-bit bus", (unsigned long)pa);
            goto done;
        }
        if (m->cpu->ops->phys)
            pa = m->cpu->ops->phys(m->cpu, pa);
        if (put(m, pa, f + off, fsz, why, cap) != ES_OK)
            goto done;
        loaded++;
    }
    if (!loaded) {
        snprintf(why, cap, "the image has nothing to load");
        goto done;
    }
    st = ES_OK;
done:
    for (r = 0; r < m->bus.nrg; r++)
        m->bus.rg[r].rom = m->bus.rg[r].kind == ESK_MEM_FLASH || m->bus.rg[r].kind == ESK_MEM_ROM ||
                           m->bus.rg[r].kind == ESK_MEM_WINDOW;
    return st;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* An Intel HEX image: data records at their addresses; a start address
 * record, if any, the entry. */
static es_status load_hex(struct esk_mach *m, const u8 *f, size_t len, u32 *entry, char *why,
                          size_t cap)
{
    u32 base = 0;
    int loaded = 0, line = 1;
    u8 rec[260];
    size_t i = 0;
    while (i < len) {
        int n = 0;
        u8 sum = 0;
        u32 a;
        while (i < len && (f[i] == '\r' || f[i] == '\n' || f[i] == ' ' || f[i] == '\t')) {
            if (f[i] == '\n')
                line++;
            i++;
        }
        if (i >= len)
            break;
        if (f[i] != ':') {
            snprintf(why, cap, "line %d: not an Intel HEX record", line);
            return ES_EPARSE;
        }
        i++;
        while (i + 1 < len && n < (int)sizeof rec) {
            int hi = hexval(f[i]), lo = hexval(f[i + 1]);
            if (hi < 0 || lo < 0)
                break;
            rec[n++] = (u8)(hi * 16 + lo);
            sum = (u8)(sum + rec[n - 1]);
            i += 2;
        }
        if (n < 5 || n != rec[0] + 5 || sum) {
            snprintf(why, cap, "line %d: a bad Intel HEX record", line);
            return ES_EPARSE;
        }
        a = (u32)rec[1] << 8 | rec[2];
        switch (rec[3]) {
        case 0:
            if (put(m, base + a, rec + 4, rec[0], why, cap) != ES_OK)
                return ES_EINVAL;
            loaded++;
            break;
        case 1:
            i = len;
            break;
        case 2:
            base = ((u32)rec[4] << 8 | rec[5]) << 4;
            break;
        case 4:
            base = ((u32)rec[4] << 8 | rec[5]) << 16;
            break;
        case 3:
            *entry = ((u32)rec[4] << 8 | rec[5]) << 4 | ((u32)rec[6] << 8 | rec[7]);
            break;
        case 5:
            *entry = (u32)rec[4] << 24 | (u32)rec[5] << 16 | (u32)rec[6] << 8 | rec[7];
            break;
        default:
            break;
        }
    }
    if (!loaded) {
        snprintf(why, cap, "the image has nothing to load");
        return ES_EINVAL;
    }
    return ES_OK;
}

/* ---- symbols ---------------------------------------------------------------- */

/* the ELF's symbol table and its string table, or 0 */
static int symtab(const u8 *f, size_t len, const u8 **syms, u32 *nsyms, const u8 **strs, u32 *strs_len)
{
    u32 shoff, shentsize, shnum, i;
    if (len < 52 || memcmp(f, "\177ELF", 4) != 0 || f[4] != 1)
        return 0;
    shoff = le32(f + 32);
    shentsize = le16(f + 46);
    shnum = le16(f + 48);
    if (shentsize < 40 || (u64)shoff + (u64)shnum * shentsize > len)
        return 0;
    for (i = 0; i < shnum; i++) {
        const u8 *sh = f + shoff + (size_t)i * shentsize;
        u32 link, off, size;
        const u8 *lk;
        if (le32(sh + 4) != 2) /* SHT_SYMTAB */
            continue;
        off = le32(sh + 16);
        size = le32(sh + 20);
        link = le32(sh + 24);
        if ((u64)off + size > len || link >= shnum)
            return 0;
        lk = f + shoff + (size_t)link * shentsize;
        if ((u64)le32(lk + 16) + le32(lk + 20) > len)
            return 0;
        *syms = f + off;
        *nsyms = size / 16u;
        *strs = f + le32(lk + 16);
        *strs_len = le32(lk + 20);
        return 1;
    }
    return 0;
}

static const char *sym_name(const u8 *strs, u32 strs_len, u32 at)
{
    u32 k;
    if (at >= strs_len)
        return "";
    for (k = at; k < strs_len; k++)
        if (!strs[k])
            return (const char *)strs + at;
    return "";
}

int esk_elf_symbol_size(const u8 *f, size_t len, const char *name, u32 *value, u32 *size)
{
    const u8 *syms, *strs;
    u32 n, sl, i;
    if (!symtab(f, len, &syms, &n, &strs, &sl))
        return 0;
    for (i = 0; i < n; i++) {
        const u8 *s = syms + (size_t)i * 16u;
        u32 type = s[12] & 15u;
        if ((type == 1 || type == 2) && !strcmp(sym_name(strs, sl, le32(s)), name)) {
            *value = type == 2 ? le32(s + 4) & ~1u : le32(s + 4);
            *size = le32(s + 8);
            return 1;
        }
    }
    return 0;
}

int esk_elf_symbol(const u8 *f, size_t len, const char *name, u32 *value)
{
    u32 size;
    return esk_elf_symbol_size(f, len, name, value, &size);
}

int esk_elf_label(const u8 *f, size_t len, const char *name, u32 *value)
{
    const u8 *syms, *strs;
    u32 n, sl, i;
    if (!symtab(f, len, &syms, &n, &strs, &sl))
        return 0;
    for (i = 0; i < n; i++) {
        const u8 *s = syms + (size_t)i * 16u;
        u32 type = s[12] & 15u, shndx = le16(s + 14);
        if (type <= 2 && shndx != 0 && shndx != 0xfff2u && !strcmp(sym_name(strs, sl, le32(s)), name)) {
            *value = type == 2 ? le32(s + 4) & ~1u : le32(s + 4);
            return 1;
        }
    }
    return 0;
}

int esk_elf_symbol_at(const u8 *f, size_t len, u32 addr, char *name, size_t cap, u32 *offset)
{
    const u8 *syms, *strs, *best = NULL;
    u32 n, sl, i, best_v = 0;
    if (!symtab(f, len, &syms, &n, &strs, &sl))
        return 0;
    for (i = 0; i < n; i++) {
        const u8 *s = syms + (size_t)i * 16u;
        u32 type = s[12] & 15u, v, size;
        if (type != 1 && type != 2)
            continue;
        v = type == 2 ? le32(s + 4) & ~1u : le32(s + 4);
        size = le32(s + 8);
        if (addr >= v && (addr - v < size || (!size && addr == v)) && (!best || v > best_v)) {
            best = s;
            best_v = v;
        }
    }
    if (!best)
        return 0;
    snprintf(name, cap, "%s", sym_name(strs, sl, le32(best)));
    *offset = addr - best_v;
    return 1;
}

/* whether section `k` holds instructions (SHF_EXECINSTR) */
static int exec_section(const u8 *f, size_t len, u32 k)
{
    u32 shoff = le32(f + 32), shentsize = le16(f + 46), shnum = le16(f + 48);
    if (k >= shnum || (u64)shoff + (u64)(k + 1) * shentsize > len)
        return 0;
    return (le32(f + shoff + (size_t)k * shentsize + 8) & 4u) != 0;
}

void esk_elf_functions(const u8 *f, size_t len,
                       void (*fn)(void *ctx, const char *name, u32 addr, u32 size, int weak), void *ctx)
{
    const u8 *syms, *strs;
    u32 n, sl, i;
    if (!symtab(f, len, &syms, &n, &strs, &sl))
        return;
    for (i = 0; i < n; i++) {
        const u8 *s = syms + (size_t)i * 16u;
        u32 type = s[12] & 15u, bind = s[12] >> 4, shndx = le16(s + 14);
        if (shndx == 0 || shndx >= 0xff00u)
            continue; /* undefined, absolute, common */
        /* a function; or an assembler's global label in code (libgcc's
         * division helpers, a C runtime's entry), which has no size */
        if (type == 2 || (type == 0 && bind != 0 && exec_section(f, len, shndx)))
            fn(ctx, sym_name(strs, sl, le32(s)), le32(s + 4) & ~1u, type == 2 ? le32(s + 8) : 0, bind == 2);
    }
}
