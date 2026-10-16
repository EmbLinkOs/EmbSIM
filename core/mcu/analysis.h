/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* analysis.h -- what a run measures of its firmware beyond its counts
 * (SPEC-009 §12): the code it covered (coverage.c) and where its time went
 * (profile.c).
 *
 * Both watch the run one instruction at a time: when a machine has an
 * analysis, the run loop (mach.c), or smp.c for each core, steps the core
 * through esk_an_step instead of its own step, so a machine without one runs
 * as it always did. esk_an_step notes the program counter and the counts
 * before the step and gives the instruction that ran, and what it cost, to
 * the analyses. It follows the program's calls and returns on a shadow
 * stack of frames per core, as the core executes them (the profile's call
 * paths); the cores report exception entries and returns
 * (esk_mach_exc, esk_mach_exc_return), which no instruction shows.
 *
 * Functions come from the image's ELF symbol table, source lines from its
 * DWARF line tables (dwarf.h). Extracted from EmbCC's simulator
 * (tools/embsim/analysis.h, analysis.c, coverage.c, profile.c, commit
 * aa4f10a): the call and return recognition and the report formats are
 * EmbCC's; the per-core stacks, the reports into buffers and as JSON are
 * EmbSIM's. */
#ifndef ESK_MCU_ANALYSIS_H
#define ESK_MCU_ANALYSIS_H

#include "mcu/mach.h"
#include "util/json.h"
#include "util/str.h"

struct esk_dwarf;

/* the analyses a machine runs */
enum { ESK_AN_COVERAGE = 1, ESK_AN_PROFILE = 2 };

/* report formats: coverage takes TEXT and LCOV; the profile TEXT and the
 * two collapsed-stack forms (by cycles, by instructions) */
enum { ESK_AN_TEXT, ESK_AN_LCOV, ESK_AN_COLLAPSED, ESK_AN_COLLAPSED_INSNS };

typedef struct esk_an esk_an;

/* the analyses `what` on machine m, which runs the ELF image `elf` (its
 * symbols; it must outlive the analysis) with its line tables (NULL
 * without) on `cores` cores. ES_EINVAL when the image has no functions to
 * measure (a HEX image, a stripped ELF) */
es_status esk_an_create(struct esk_mach *m, unsigned what, const u8 *elf, size_t len,
                        const struct esk_dwarf *dw, int cores, esk_an **out);
void esk_an_free(esk_an *a);
unsigned esk_an_what(const esk_an *a);
/* the machine reset: the call stacks are empty, the counts kept */
void esk_an_restart(esk_an *a);
/* the counts start again from zero */
void esk_an_clear(esk_an *a);

/* the machine's hooks: one step of `cpu`, core `core` of the machine; an
 * exception's entry, returning to `ret`; an exception's return */
void esk_an_step(esk_an *a, int core, struct esk_cpu *cpu);
void esk_an_exc(esk_an *a, int core, int n, u32 ret);
void esk_an_exc_return(esk_an *a, int core);

/* ---- coverage (coverage.c) ---- */

/* the report, appended to `out`: TEXT (gcov's layout, with the source
 * beside the counts when a file is found as named or under `src_dir`) or
 * LCOV (a tracefile, test name `name`). 0 when out of memory */
int esk_an_coverage(esk_an *a, int fmt, const char *name, const char *src_dir, esk_strbuf *out);
/* {lines: {hit, total}, functions: {hit, total}, files: [{path, lines,
 * functions: [{name, line, count}], counts: [[line, count]...]}]}; without
 * `detail`, the totals and each file's {path, lines, functions} counts */
esk_json *esk_an_coverage_json(esk_an *a, esk_alloc *al, int detail);

/* ---- the profile (profile.c) ---- */

/* the report, appended to `out`: TEXT (the functions by self cycles, under
 * a header naming `name`), or collapsed stacks ("main;f;g 1234") by cycles
 * or by instructions, each with `name;` before it unless it is NULL */
int esk_an_profile(esk_an *a, int fmt, const char *name, esk_strbuf *out);
/* {insns, cycles, functions: [{name, self_insns, self_cycles, incl_insns,
 * incl_cycles, calls}]} by self cycles, at most `top` (0: all) */
esk_json *esk_an_profile_json(esk_an *a, esk_alloc *al, int top);

/* ---- shared by analysis.c, coverage.c and profile.c ---- */

#define ESK_AN_CORES 4
#define ESK_AN_DEPTH 4096

/* a function: [lo, hi) */
typedef struct esk_an_fn {
    u32 lo, hi;
    const char *name;
} esk_an_fn;

/* an address range the analysis counts instructions in, a count per
 * halfword (a PIC24's program word) */
typedef struct esk_an_code {
    u32 lo, hi;
    u64 *count;
} esk_an_code;

/* a call path: a function called along its parent's path; the counts are
 * the instructions and cycles run in the function itself on this path */
typedef struct esk_an_node {
    int parent, fn; /* fn: an index into fns; -1 no symbol; -2 the root;
                     * -3 - k core k's root */
    int child, sib;
    u64 insns, cycles, calls;
} esk_an_node;

/* a frame of a shadow stack: a call or an exception's entry */
typedef struct esk_an_frame {
    u32 ret;  /* where it returns to */
    int node; /* its call path */
    int exc;  /* an exception's: its number + 1 */
    u32 vec;  /* an exception's: where its entry landed */
} esk_an_frame;

/* a step's exception entries and returns, in order */
typedef struct esk_an_ev {
    int entry;
    int n;
    u32 ret;
} esk_an_ev;

typedef struct esk_an_core {
    esk_an_frame *fr;
    int nfr;
    int root;                       /* its root path */
    int cur_base, cur_fn, cur_node; /* the path the pc is on, cached */
    u32 fn_lo, fn_hi;               /* the cached function's range */
    int fn_idx;
    u32 pc0;
    int kind, size;
    int slot_kind; /* MIPS: the branch before this delay slot's kind */
    esk_an_ev ev[4];
    int nev;
} esk_an_core;

struct esk_an {
    esk_alloc *a;
    struct esk_mach *m;
    unsigned what;
    int arch;
    const struct esk_dwarf *dw;
    esk_an_fn *fn;
    int nfn, capfn;
    char *names;
    size_t names_len;
    esk_an_code *code;
    int ncode, code_last;
    esk_an_node *node;
    int nnode, capnode;
    esk_an_core core[ESK_AN_CORES];
    int ncores;
    u64 insns, cycles; /* the steps' */
    u64 cycles0;       /* the machine's time when the counts started */
    int oom;
};

/* the function holding pc: an index into fns, or -1 */
int esk_an_fn_at(const esk_an *a, u32 pc);
/* the count of the instruction at pc (0 outside the code) */
u64 esk_an_count_at(const esk_an *a, u32 pc);
/* whether [lo, hi) lies inside one function */
int esk_an_in_fn(const esk_an *a, u32 lo, u32 hi);

#endif
