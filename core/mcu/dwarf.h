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

#endif
