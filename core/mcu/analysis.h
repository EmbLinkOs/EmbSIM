/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* analysis.h -- what a run measures of its firmware beyond its counts
 * (SPEC-009 §12): the code it covered (coverage.c), where its time went
 * (profile.c), how deep its stacks went (stack.c) and what its faults were
 * (fault.c).
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
 * EmbCC's (stack.c: tools/embsim/stack.c, without the .su and embrt
 * comparisons; fault.c: tools/embsim/fault.c); the per-core stacks, the
 * reports into buffers and as JSON, and an overflow and a fault as
 * diagnostics rather than lines on stderr are EmbSIM's. */
#ifndef ESK_MCU_ANALYSIS_H
#define ESK_MCU_ANALYSIS_H

#include "mcu/mach.h"
#include "util/json.h"
#include "util/str.h"

struct esk_dwarf;

/* the analyses a machine runs */
enum { ESK_AN_COVERAGE = 1, ESK_AN_PROFILE = 2, ESK_AN_STACK = 4, ESK_AN_FAULT = 8 };
/* the analyses that follow the program's calls */
#define ESK_AN_CALLS (ESK_AN_PROFILE | ESK_AN_STACK | ESK_AN_FAULT)

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

/* ---- the stacks (stack.c) ---- */

/* the report, appended to `out`: each core's stacks (top, deepest, the
 * limit and where it came from), an overflow, the functions by depth with
 * their own frames and their depth with their callees */
int esk_an_stack(esk_an *a, const char *name, esk_strbuf *out);
/* {stacks: [{core, name, top, deepest, used, limit?, limit_from?, size?}],
 * overflow?: {core, pc, function, sp, limit, limit_from}, functions:
 * [{name, deepest, frame, incl}]} by depth, at most `top` (0: all) */
esk_json *esk_an_stack_json(esk_an *a, esk_alloc *al, int top);

/* ---- the faults (fault.c) ---- */

/* the report, appended to `out`: each fault as the core took it (decoded,
 * its instruction, its registers, a backtrace, its handler), a lockup */
int esk_an_faults(esk_an *a, const char *name, esk_strbuf *out);
/* {count, faults: [{core, exception, name, pc, where, summary, handler,
 * backtrace: [{pc, where}], text}]}, at most ESK_AN_FAULTS of them */
esk_json *esk_an_faults_json(esk_an *a, esk_alloc *al);

/* ---- shared by analysis.c, coverage.c, profile.c, stack.c and fault.c ---- */

#define ESK_AN_CORES 4
#define ESK_AN_DEPTH 4096

/* a function: [lo, hi) */
typedef struct esk_an_fn {
    u32 lo, hi;
    const char *name;
    int weak; /* a weak symbol: a strong one at its address names it */
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
    u32 sp;   /* the stack pointer at its first instruction */
    u32 min;  /* the lowest while it was on the stack */
} esk_an_frame;

#define ESK_AN_FAULTS 16
#define ESK_AN_BT 32

/* a fault the core took, decoded at the end of the step that took it */
typedef struct esk_an_fault {
    int core, n;      /* the exception's number as the core gave it */
    u32 pc;           /* the faulting instruction (or the one it returns to) */
    int lockup;       /* the core locked up: no handler followed */
    char name[40];    /* "HardFault", "illegal instruction" */
    char summary[240];
    char handler[96];
    u32 bt[ESK_AN_BT];
    int nbt;
    char *text;       /* the report (esk_strdup) */
} esk_an_fault;

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
    /* the stacks (stack.c): the main stack's (a Cortex-M's MSP) and the
     * process stack's top and lowest; a function entered by a jump, not a
     * call, and the sp it was entered with */
    u32 msp_top, msp_min, psp_top, psp_min;
    u32 last_msp; /* after the step before */
    int psp_used;
    int tail_fn;
    u32 tail_sp;
    u32 checked_top, check; /* the overflow's limit, for the top */
    const char *check_why;
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
    /* the stacks (stack.c): by function, the lowest sp, the own frame and
     * the depth with the callees (nfn + 1: the last for no symbol); the
     * link's limit; a PIC24's stack base (__SP_init); an overflow */
    const u8 *elf;
    size_t elf_len;
    u32 *fn_minsp, *fn_frame, *fn_incl;
    u32 stk_limit;
    const char *stk_limit_why;
    u32 sp0;
    int ovf, ovf_core, ovf_entry;
    u32 ovf_pc, ovf_sp, ovf_limit;
    const char *ovf_why;
    /* the faults (fault.c): the first ESK_AN_FAULTS, how many in all, and
     * whether a lockup is reported (once a reset) */
    esk_an_fault faults[ESK_AN_FAULTS];
    int nfaults, fault_count, lockup_seen;
};

/* stack.c's part of the step (analysis.c): set up, the machine reset, the
 * counts cleared, after a step, a frame popped, a function entered by a jump */
es_status esk_an_stack_init(esk_an *a);
void esk_an_stack_free(esk_an *a);
void esk_an_stack_restart(esk_an *a);
void esk_an_stack_clear(esk_an *a);
void esk_an_stack_step(esk_an *a, esk_an_core *k, struct esk_cpu *c, int ran, int entered, int sample_frame);
void esk_an_stack_pop(esk_an *a, esk_an_core *k, int i);
void esk_an_stack_first(esk_an *a, esk_an_core *k, struct esk_cpu *c, u32 pc0);
void esk_an_stack_entered(esk_an *a, esk_an_core *k, struct esk_cpu *c, u32 pc1, int called);
/* the stack pointer of a core (a Cortex-M's in use), deeper lower on every
 * core (a PIC24's mirrored), 0 before the program has one; and a Cortex-M's
 * main and process stack pointers, 1 when the process one is in use */
u32 esk_an_sp(const esk_an *a, struct esk_cpu *c);
int esk_an_sps(const esk_an *a, struct esk_cpu *c, u32 *msp, u32 *psp);

/* fault.c's part of the step: whether exception n is a fault; a fault the
 * step took (n, returning to ret), and its end (a lockup); the records
 * cleared */
int esk_an_is_fault(const esk_an *a, int n);
void esk_an_fault_taken(esk_an *a, esk_an_core *k, struct esk_cpu *c, int n, u32 ret);
void esk_an_fault_step(esk_an *a, esk_an_core *k, struct esk_cpu *c);
void esk_an_fault_clear(esk_an *a);

/* the function holding pc: an index into fns, or -1 */
int esk_an_fn_at(const esk_an *a, u32 pc);
/* the count of the instruction at pc (0 outside the code) */
u64 esk_an_count_at(const esk_an *a, u32 pc);
/* whether [lo, hi) lies inside one function */
int esk_an_in_fn(const esk_an *a, u32 lo, u32 hi);

#endif
