/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* dwarf.h -- source lines from an ELF image's DWARF line tables. */
#ifndef ESK_MCU_DWARF_H
#define ESK_MCU_DWARF_H

#include "mcu/mach.h"

typedef struct esk_dwarf_row {
    u32 addr;
    int file;  /* index into files */
    u32 line;
    u8 stmt;   /* a statement's start (a breakpoint's place) */
    u8 end;    /* the end of a sequence: no line from here */
} esk_dwarf_row;

typedef struct esk_dwarf {
    esk_alloc *a;
    char **files;
    size_t nfiles, cap_files;
    esk_dwarf_row *rows; /* by address */
    size_t nrows, cap_rows;
} esk_dwarf;

/* the image's line tables: ES_ENOTFOUND without .debug_line */
es_status esk_dwarf_load(esk_dwarf *d, esk_alloc *a, const u8 *elf, size_t len);
void esk_dwarf_free(esk_dwarf *d);
/* 1 and the file and line holding `addr`, or 0 */
int esk_dwarf_line_at(const esk_dwarf *d, u32 addr, const char **file, u32 *line);
/* the statement addresses of `file` (a path, or a suffix of one at a
 * separator) at `line`: how many, at most `cap` */
int esk_dwarf_line_addrs(const esk_dwarf *d, const char *file, u32 line, u32 *addrs, int cap);
/* the table's name for a file a request names, or NULL */
const char *esk_dwarf_file(const esk_dwarf *d, const char *file);

/* ---- types (.debug_info, DWARF 2 to 5, 32-bit DWARF) ---- */

/* the byte offsets of `n` members of the structure named `strct` (its tag's
 * name: "tskTaskControlBlock", not a typedef's), from the first complete
 * definition in the image: offs[i] is -1 for a member it does not have. How
 * many were found; 0 without .debug_info or that structure */
int esk_dwarf_members(esk_alloc *a, const u8 *elf, size_t len, const char *strct, const char *const *members, int n,
                      long *offs);

/* ---- the call frame information (.debug_frame, CIE versions 1, 3, 4) ---- */

#define ESK_CFI_REGS 64
enum { ESK_CFI_SAME, ESK_CFI_UNDEF, ESK_CFI_OFFSET, ESK_CFI_VALOFF, ESK_CFI_REG };

/* the rules at an address: the canonical frame address (a register plus an
 * offset) and where each of the caller's registers is (ESK_CFI_OFFSET:
 * saved at CFA + off; ESK_CFI_VALOFF: CFA + off itself; ESK_CFI_REG: in
 * register off) */
typedef struct esk_cfi_row {
    int cfa_reg;
    s64 cfa_off;
    int ra; /* the return address's column */
    u8 how[ESK_CFI_REGS];
    s64 off[ESK_CFI_REGS];
} esk_cfi_row;

/* the rules at pc in the ELF image's .debug_frame: 1, or 0 when no FDE
 * covers it or its CFI is beyond what is read (an expression) */
int esk_dwarf_cfi(const u8 *elf, size_t len, u32 pc, esk_cfi_row *row);

#endif
