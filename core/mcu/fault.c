/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Téo Motsou and contributors (EmbLinkOs/EmbCC) */
/* Modifications copyright 2026 Junior Deogracias */
/* fault.c -- what a fault was, decoded, as the core takes it
 * (analysis.h).
 *
 * The cores report each exception they enter (esk_mach_exc) before they
 * stack anything; a fault among them is decoded at the end of the step that
 * took it, when its frame is stacked and the core is at its handler:
 *   - a Cortex-M's HardFault, MemManage, BusFault or UsageFault: HFSR and
 *     CFSR decoded into words, MMFAR and BFAR when they are valid;
 *   - a RISC-V exception (not an interrupt, an ECALL or an EBREAK): mcause
 *     and mtval;
 *   - a MIPS exception (not an interrupt or a SYSCALL or BREAK): Cause's
 *     code, BadVAddr, the delay slot;
 *   - a PIC24 trap: what the instruction did (the core's account of it),
 *     the W registers and SR as the trap found them;
 * then the faulting instruction, disassembled, with its function and line;
 * the registers; a backtrace: by the image's .debug_frame when it covers
 * the pc (through a Cortex-M's exception frame where the unwinding reaches
 * one), else the calls the analysis saw the core make (its shadow stack);
 * and the handler the core goes to, or that there is none. A lockup, which
 * no handler follows, is reported when the core locks up.
 *
 * Each fault is a record (the first ESK_AN_FAULTS of them) and the error
 * diagnostic `mcu.fault`, its summary (a lockup is the core's
 * `mcu.lockup`); the run goes on as the part does. */
#include <string.h>

#include "mcu/analysis.h"
#include "mcu/cortexm/cortexm.h"
#include "mcu/dwarf.h"
#include "mcu/mips/mips.h"
#include "mcu/pic24/pic24.h"
#include "mcu/riscv/riscv.h"

/* analysis.c's architectures */
enum { AN_ARM, AN_RV, AN_AVR, AN_MIPS, AN_PIC24 };

static u32 rd32(esk_an *a, struct esk_cpu *c, u32 addr, int *ok)
{
    u32 v = 0;
    if (c->ops->phys)
        addr = c->ops->phys(c, addr);
    if (esk_bus_debug_read(&a->m->bus, addr, 4, &v))
        *ok = 0;
    return v;
}

/* "fn+0x6 (file.c:12)" */
static const char *where(const esk_an *a, u32 pc, char *b, size_t n)
{
    int f = esk_an_fn_at(a, pc);
    const char *file = NULL;
    u32 line = 0;
    size_t k;
    if (f >= 0 && pc != a->fn[f].lo)
        esk_snprintf(b, n, "%s+0x%x", a->fn[f].name, (unsigned)(pc - a->fn[f].lo));
    else if (f >= 0)
        esk_snprintf(b, n, "%s", a->fn[f].name);
    else
        esk_snprintf(b, n, "0x%08x", (unsigned)pc);
    k = strlen(b);
    if (a->dw && esk_dwarf_line_at(a->dw, pc, &file, &line) && file && k + 4 < n) {
        const char *s = strrchr(file, '/');
        esk_snprintf(b + k, n - k, " (%s:%u)", s ? s + 1 : file, (unsigned)line);
    }
    return b;
}

static void insn_line(esk_an *a, struct esk_cpu *c, u32 pc, esk_strbuf *o)
{
    char t[96];
    (void)a;
    if (!c->ops->disasm || c->ops->disasm(c, pc, t, sizeof t) <= 0)
        esk_snprintf(t, sizeof t, "(cannot be read)");
    esk_strbuf_printf(o, "  instruction 0x%08x: %s\n", (unsigned)pc, t);
}

/* ---- the backtrace -------------------------------------------------------- */

typedef struct regs {
    u32 r[32];
    u32 valid; /* which of them are known */
} regs;

static void bt_add(esk_an_fault *f, u32 pc)
{
    if (f->nbt < ESK_AN_BT)
        f->bt[f->nbt++] = pc;
}

/* the calls the analysis saw: the pc's function, then each frame's return */
static void shadow_bt(esk_an *a, esk_an_core *k, esk_an_fault *ft, u32 pc, esk_strbuf *o)
{
    char w[200];
    int i, n = 1;
    esk_strbuf_printf(o, "  backtrace (the calls the core was seen to make):\n");
    esk_strbuf_printf(o, "    #0  0x%08x in %s\n", (unsigned)pc, where(a, pc, w, sizeof w));
    bt_add(ft, pc);
    for (i = k->nfr - 1; i >= 0 && n < ESK_AN_BT; i--, n++) {
        const esk_an_frame *f = &k->fr[i];
        esk_strbuf_printf(o, "    #%-2d 0x%08x in %s%s\n", n, (unsigned)f->ret, where(a, f->ret, w, sizeof w),
                          f->exc ? "  <- interrupted here" : "");
        bt_add(ft, f->ret);
    }
}

/* By the CFI: from the registers and the pc at the fault, frame by frame,
 * through a Cortex-M's exception frame where EXC_RETURN is the return
 * address. When no FDE covers the faulting pc, the shadow stack's calls. */
static void cfi_bt(esk_an *a, esk_an_core *k, struct esk_cpu *c, esk_an_fault *ft, regs *g, u32 pc, int sp_reg,
                   esk_strbuf *o)
{
    char w[200];
    int exact = 1, n; /* exact: pc is the instruction, not a return */
    esk_cfi_row row;
    if (!esk_dwarf_cfi(a->elf, a->elf_len, pc, &row)) {
        shadow_bt(a, k, ft, pc, o);
        return;
    }
    esk_strbuf_printf(o, "  backtrace (.debug_frame):\n");
    for (n = 0; n < ESK_AN_BT; n++) {
        regs nx;
        u32 cfa, ra;
        int ok = 1, r;
        esk_strbuf_printf(o, "    #%-2d 0x%08x in %s\n", n, (unsigned)pc, where(a, pc, w, sizeof w));
        bt_add(ft, pc);
        if (!esk_dwarf_cfi(a->elf, a->elf_len, exact ? pc : pc - 1, &row)) {
            esk_strbuf_printf(o, "        (no call frame information for 0x%08x: the unwinding stops here)\n",
                              (unsigned)pc);
            return;
        }
        if (row.cfa_reg < 0 || row.cfa_reg >= 32 || !(g->valid >> row.cfa_reg & 1))
            return;
        cfa = g->r[row.cfa_reg] + (u32)row.cfa_off;
        nx = *g;
        for (r = 0; r < 32; r++) {
            switch (row.how[r]) {
            case ESK_CFI_OFFSET:
                nx.r[r] = rd32(a, c, cfa + (u32)row.off[r], &ok);
                nx.valid |= 1u << r;
                break;
            case ESK_CFI_VALOFF:
                nx.r[r] = cfa + (u32)row.off[r];
                nx.valid |= 1u << r;
                break;
            case ESK_CFI_REG:
                nx.r[r] = row.off[r] >= 0 && row.off[r] < 32 ? g->r[row.off[r]] : 0;
                break;
            case ESK_CFI_UNDEF:
                nx.valid &= ~(1u << r);
                break;
            default:
                break;
            }
        }
        if (!ok || row.ra < 0 || row.ra >= 32 || !(nx.valid >> row.ra & 1))
            return;
        ra = nx.r[row.ra];
        if (ra == 0xffffffffu)
            return; /* a Cortex-M's link register out of reset: the first frame */
        nx.r[sp_reg] = cfa;
        exact = 0;
        if (a->arch == AN_ARM && ra >= 0xf0000000u) {
            /* an exception's return: the frame its entry stacked, on the
             * stack EXC_RETURN names, holds the interrupted code's pc */
            static const int at[7] = { 0, 1, 2, 3, 12, 14, 15 };
            const struct esk_cm_state *cs = (const struct esk_cm_state *)c;
            u32 sp = ra & 4 ? (cs->psp_active ? cs->R[13] : cs->other_sp) : cfa, v[8];
            int i;
            for (i = 0; i < 8; i++)
                v[i] = rd32(a, c, sp + 4 * (u32)i, &ok);
            if (!ok)
                return;
            for (i = 0; i < 7; i++)
                nx.r[at[i]] = v[i];
            nx.r[13] = sp + (ra & 0x10 ? 0x20u : 0x68u) + (v[7] >> 9 & 1 ? 4u : 0u);
            nx.valid = 0xffff;
            esk_strbuf_printf(o, "        <- an exception's entry (EXC_RETURN 0x%08x): the code it interrupted\n",
                              (unsigned)ra);
            ra = v[6];
            exact = 1;
        }
        if (a->arch == AN_ARM || a->arch == AN_MIPS)
            ra &= ~1u;
        if (!ra || ra == 0xfffffffeu || (ra == pc && cfa == g->r[sp_reg]))
            return;
        pc = ra;
        *g = nx;
    }
}

/* ---- the Cortex-M ------------------------------------------------------- */

static const struct {
    u32 bit;
    const char *name, *what;
} cfsr_bits[] = {
    { 0, "IACCVIOL", "an instruction fetch the MPU refused" },
    { 1, "DACCVIOL", "a data access the MPU refused" },
    { 3, "MUNSTKERR", "a MemManage fault unstacking an exception's frame" },
    { 4, "MSTKERR", "a MemManage fault stacking an exception's frame" },
    { 5, "MLSPERR", "a MemManage fault in the FPU's lazy state preservation" },
    { 8, "IBUSERR", "a bus error fetching an instruction" },
    { 9, "PRECISERR", "a precise data bus error" },
    { 10, "IMPRECISERR", "an imprecise data bus error" },
    { 11, "UNSTKERR", "a bus error unstacking an exception's frame" },
    { 12, "STKERR", "a bus error stacking an exception's frame" },
    { 13, "LSPERR", "a bus error in the FPU's lazy state preservation" },
    { 16, "UNDEFINSTR", "an undefined instruction" },
    { 17, "INVSTATE", "an instruction in an invalid state (the Thumb bit clear: a branch to an even address, or a vector without bit 0)" },
    { 18, "INVPC", "an invalid EXC_RETURN in the pc" },
    { 19, "NOCP", "a coprocessor instruction with the coprocessor off or absent (the FPU off in CPACR?)" },
    { 24, "UNALIGNED", "an unaligned access" },
    { 25, "DIVBYZERO", "a divide by zero (CCR.DIV_0_TRP)" },
};

static const char *const exc_name[7] = { "", "", "NMI", "HardFault", "MemManage", "BusFault", "UsageFault" };

static void arm_fault(esk_an *a, esk_an_core *k, struct esk_cpu *c, esk_an_fault *ft, esk_strbuf *o)
{
    const struct esk_cm_state *cs = (const struct esk_cm_state *)c;
    char w[200], why[160];
    u32 lr = cs->R[14], sp, v[8], vec, frame_sp;
    int psp = (lr & 4) != 0, ok = 1, i, f;
    size_t n = 0;
    esk_snprintf(ft->name, sizeof ft->name, "%s", exc_name[ft->n]);
    why[0] = 0;
    esk_strbuf_printf(o, "%s (exception %d) at 0x%08x, in %s\n", ft->name, ft->n, (unsigned)ft->pc,
                      where(a, ft->pc, w, sizeof w));
    if (ft->n == EXC_HARD) {
        esk_strbuf_printf(o, "  HFSR  0x%08x", (unsigned)cs->hfsr);
        if (cs->hfsr & (1u << 30)) {
            esk_strbuf_printf(o, "  FORCED: a configurable fault escalated to HardFault (its handler disabled, or "
                                 "not able to preempt)");
            n += (size_t)esk_snprintf(why + n, sizeof why - n, "FORCED");
        }
        if (cs->hfsr & 2) {
            esk_strbuf_printf(o, "  VECTTBL: a bus error reading the vector table");
            n += (size_t)esk_snprintf(why + n, sizeof why - n, "%sVECTTBL", n ? ", " : "");
        }
        esk_strbuf_putc(o, '\n');
    }
    esk_strbuf_printf(o, "  CFSR  0x%08x\n", (unsigned)cs->cfsr);
    for (i = 0; i < (int)ES_COUNTOF(cfsr_bits); i++)
        if (cs->cfsr >> cfsr_bits[i].bit & 1) {
            u32 addr = 0;
            int has = 0;
            if (cfsr_bits[i].bit == 9 && (cs->cfsr & (1u << 15))) {
                addr = cs->bfar;
                has = 1;
            }
            if (cfsr_bits[i].bit == 1 && (cs->cfsr & (1u << 7))) {
                addr = cs->mmfar;
                has = 1;
            }
            esk_strbuf_printf(o, "        %s: %s", cfsr_bits[i].name, cfsr_bits[i].what);
            if (has)
                esk_strbuf_printf(o, " at 0x%08x", (unsigned)addr);
            esk_strbuf_putc(o, '\n');
            if (n < sizeof why)
                n += (size_t)esk_snprintf(why + n, sizeof why - n, "%s%s", n ? ", " : "", cfsr_bits[i].name);
            if (has && n < sizeof why)
                n += (size_t)esk_snprintf(why + n, sizeof why - n, " at 0x%08x", (unsigned)addr);
        }
    if (cs->cfsr & (1u << 15))
        esk_strbuf_printf(o, "  BFAR  0x%08x (BFARVALID)\n", (unsigned)cs->bfar);
    if (cs->cfsr & (1u << 7))
        esk_strbuf_printf(o, "  MMFAR 0x%08x (MMARVALID)\n", (unsigned)cs->mmfar);
    insn_line(a, c, ft->pc, o);
    /* the frame the entry stacked, on the stack EXC_RETURN names */
    sp = psp ? (cs->psp_active ? cs->R[13] : cs->other_sp) : (cs->psp_active ? cs->other_sp : cs->R[13]);
    for (i = 0; i < 8; i++)
        v[i] = rd32(a, c, sp + 4 * (u32)i, &ok);
    esk_strbuf_printf(o, "  stacked frame at 0x%08x (%s):\n", (unsigned)sp, psp ? "PSP" : "MSP");
    esk_strbuf_printf(o, "    r0  0x%08x  r1  0x%08x  r2  0x%08x  r3   0x%08x\n", (unsigned)v[0], (unsigned)v[1],
                      (unsigned)v[2], (unsigned)v[3]);
    esk_strbuf_printf(o, "    r12 0x%08x  lr  0x%08x  pc  0x%08x  xpsr 0x%08x\n", (unsigned)v[4], (unsigned)v[5],
                      (unsigned)v[6], (unsigned)v[7]);
    esk_strbuf_printf(o, "    lr: %s\n", where(a, v[5] & ~1u, w, sizeof w));
    esk_strbuf_printf(o, "  r4  0x%08x  r5  0x%08x  r6  0x%08x  r7   0x%08x\n", (unsigned)cs->R[4],
                      (unsigned)cs->R[5], (unsigned)cs->R[6], (unsigned)cs->R[7]);
    esk_strbuf_printf(o, "  r8  0x%08x  r9  0x%08x  r10 0x%08x  r11  0x%08x\n", (unsigned)cs->R[8],
                      (unsigned)cs->R[9], (unsigned)cs->R[10], (unsigned)cs->R[11]);
    frame_sp = sp + (lr & 0x10 ? 0x20u : 0x68u) + (v[7] >> 9 & 1 ? 4u : 0u);
    if (ok) {
        regs g;
        memset(&g, 0, sizeof g);
        for (i = 4; i < 12; i++)
            g.r[i] = cs->R[i];
        g.r[0] = v[0];
        g.r[1] = v[1];
        g.r[2] = v[2];
        g.r[3] = v[3];
        g.r[12] = v[4];
        g.r[14] = v[5];
        g.r[15] = v[6];
        g.r[13] = frame_sp;
        g.valid = 0xffff;
        cfi_bt(a, k, c, ft, &g, v[6] & ~1u, 13, o);
    } else {
        shadow_bt(a, k, ft, ft->pc, o);
    }
    ok = 1;
    vec = rd32(a, c, cs->vtor + 4u * (u32)ft->n, &ok);
    f = esk_an_fn_at(a, vec & ~1u);
    if (!ok)
        esk_snprintf(ft->handler, sizeof ft->handler, "none: the vector table cannot be read at 0x%08x",
                     (unsigned)(cs->vtor + 4u * (u32)ft->n));
    else if (!(vec & 1))
        esk_snprintf(ft->handler, sizeof ft->handler, "none: the vector 0x%08x has no Thumb bit (it faults again)",
                     (unsigned)vec);
    else if (f < 0 || a->fn[f].lo != (vec & ~1u))
        esk_snprintf(ft->handler, sizeof ft->handler, "none: the vector 0x%08x is no function's start",
                     (unsigned)vec);
    else
        esk_snprintf(ft->handler, sizeof ft->handler, "%s at 0x%08x", a->fn[f].name, (unsigned)(vec & ~1u));
    esk_snprintf(ft->summary, sizeof ft->summary, "%s at 0x%08x in %s%s%s", ft->name, (unsigned)ft->pc,
                 where(a, ft->pc, w, sizeof w), why[0] ? ": " : "", why);
}

/* ---- RISC-V ------------------------------------------------------------- */

static const char *const rv_cause[8] = { "instruction address misaligned", "instruction access fault",
                                         "illegal instruction", "breakpoint", "load address misaligned",
                                         "load access fault", "store/AMO address misaligned",
                                         "store/AMO access fault" };

static const char *const xn[32] = { "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0",
                                    "a1", "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5",
                                    "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6" };

static void rv_fault(esk_an *a, esk_an_core *k, struct esk_cpu *c, esk_an_fault *ft, esk_strbuf *o)
{
    const struct rv_state *r = (const struct rv_state *)c;
    char w[200], tv[64];
    u32 base;
    int i, f;
    regs g;
    esk_snprintf(ft->name, sizeof ft->name, "%s", rv_cause[ft->n]);
    esk_strbuf_printf(o, "%s (mcause %d) at 0x%08x, in %s\n", ft->name, ft->n, (unsigned)ft->pc,
                      where(a, ft->pc, w, sizeof w));
    if (ft->n == 2)
        esk_snprintf(tv, sizeof tv, "mtval 0x%08llx: the instruction's bits", (unsigned long long)r->mtval);
    else if (ft->n <= 1)
        esk_snprintf(tv, sizeof tv, "mtval 0x%08llx: the address fetched", (unsigned long long)r->mtval);
    else
        esk_snprintf(tv, sizeof tv, "mtval 0x%08llx: the address %s", (unsigned long long)r->mtval,
                     ft->n <= 5 ? "loaded from" : "stored to");
    esk_strbuf_printf(o, "  %s\n", tv);
    insn_line(a, c, ft->pc, o);
    esk_strbuf_printf(o, "  registers:\n");
    for (i = 0; i < 32; i += 4)
        esk_strbuf_printf(o, "    %-4s 0x%08x %-4s 0x%08x %-4s 0x%08x %-4s 0x%08x\n", xn[i], (unsigned)r->x[i],
                          xn[i + 1], (unsigned)r->x[i + 1], xn[i + 2], (unsigned)r->x[i + 2], xn[i + 3],
                          (unsigned)r->x[i + 3]);
    memset(&g, 0, sizeof g);
    for (i = 0; i < 32; i++)
        g.r[i] = (u32)r->x[i];
    g.valid = 0xffffffffu;
    cfi_bt(a, k, c, ft, &g, ft->pc, 2, o);
    base = (u32)(r->mtvec & ~(u64)3);
    f = esk_an_fn_at(a, base);
    if (!esk_bus_region(&a->m->bus, c->ops->phys ? c->ops->phys(c, base) : base, 2))
        esk_snprintf(ft->handler, sizeof ft->handler, "none: mtvec 0x%08x holds nothing to fetch",
                     (unsigned)base);
    else
        esk_snprintf(ft->handler, sizeof ft->handler, "mtvec 0x%08x, %s", (unsigned)base,
                     f >= 0 ? a->fn[f].name : "no symbol");
    esk_snprintf(ft->summary, sizeof ft->summary, "%s at 0x%08x in %s: %s", ft->name, (unsigned)ft->pc,
                 where(a, ft->pc, w, sizeof w), tv);
}

/* ---- MIPS ------------------------------------------------------------------ */

static const char *mips_exc(int code)
{
    switch (code) {
    case EXC_MOD: return "TLB modified: a store to a clean page";
    case EXC_TLBL: return "TLB miss or invalid entry on a load or fetch";
    case EXC_TLBS: return "TLB miss or invalid entry on a store";
    case EXC_ADEL: return "address error on a load or fetch (misaligned, or a kernel address in user mode)";
    case EXC_ADES: return "address error on a store (misaligned, or a kernel address in user mode)";
    case EXC_IBE: return "bus error on an instruction fetch: nothing is there";
    case EXC_DBE: return "bus error on a load or store: nothing is there";
    case EXC_RI: return "reserved instruction";
    case EXC_CPU: return "coprocessor unusable";
    case EXC_OV: return "arithmetic overflow (add, addi, sub)";
    case EXC_TR: return "trap (a t* instruction's condition held)";
    case EXC_FPE: return "floating-point exception";
    case EXC_DSPDIS: return "DSP ASE state disabled: a DSP instruction with Status.MX clear";
    default: return NULL;
    }
}

static const char *const mn[32] = { "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2",
                                    "t3", "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5",
                                    "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra" };

static void mips_fault(esk_an *a, esk_an_core *k, struct esk_cpu *c, esk_an_fault *ft, esk_strbuf *o)
{
    const struct mips_state *m = (const struct mips_state *)c;
    char w[200], extra[64];
    u32 cause = m->cause, pc = c->ops->pc(c);
    int i;
    regs g;
    const char *what = mips_exc(ft->n);
    esk_snprintf(ft->name, sizeof ft->name, "%.39s", what);
    extra[0] = 0;
    esk_strbuf_printf(o, "%s (Cause.ExcCode %d) at 0x%08x%s, in %s\n", what, ft->n, (unsigned)ft->pc,
                      cause & CA_BD ? " (a delay slot's branch)" : "", where(a, ft->pc, w, sizeof w));
    if (ft->n == EXC_ADEL || ft->n == EXC_ADES || ft->n <= EXC_TLBS) {
        esk_snprintf(extra, sizeof extra, "BadVAddr 0x%08x", (unsigned)m->badvaddr);
        esk_strbuf_printf(o, "  %s: the address\n", extra);
    }
    if (ft->n == EXC_CPU)
        esk_strbuf_printf(o, "  coprocessor %u (Cause.CE)%s\n", (unsigned)((cause >> 28) & 3),
                          ((cause >> 28) & 3) == 1 ? ": the FPU, with Status.CU1 clear" : "");
    if (m->epc & 1)
        esk_strbuf_printf(o, "  instruction 0x%08x: (MIPS16e)\n", (unsigned)(cause & CA_BD ? ft->pc + 2 : ft->pc));
    else
        insn_line(a, c, cause & CA_BD ? ft->pc + 4 : ft->pc, o);
    esk_strbuf_printf(o, "  registers:\n");
    for (i = 0; i < 32; i += 4)
        esk_strbuf_printf(o, "    %-4s 0x%08x %-4s 0x%08x %-4s 0x%08x %-4s 0x%08x\n", mn[i], (unsigned)m->r[i],
                          mn[i + 1], (unsigned)m->r[i + 1], mn[i + 2], (unsigned)m->r[i + 2], mn[i + 3],
                          (unsigned)m->r[i + 3]);
    esk_strbuf_printf(o, "    Status 0x%08x  Cause 0x%08x  EPC 0x%08x\n", (unsigned)m->status, (unsigned)cause,
                      (unsigned)m->epc);
    memset(&g, 0, sizeof g);
    for (i = 0; i < 32; i++)
        g.r[i] = m->r[i];
    g.valid = 0xffffffffu;
    cfi_bt(a, k, c, ft, &g, ft->pc, 29, o);
    esk_snprintf(ft->handler, sizeof ft->handler, "the exception vector 0x%08x, %.60s", (unsigned)pc,
                 where(a, pc, w, sizeof w));
    esk_snprintf(ft->summary, sizeof ft->summary, "%s at 0x%08x in %s%s%s", what, (unsigned)ft->pc,
                 where(a, ft->pc, w, sizeof w), extra[0] ? ": " : "", extra);
}

/* ---- the PIC24 ------------------------------------------------------------- */

static const char *const pic24_trap[8] = { "oscillator failure", "address error", "hard trap", "stack error",
                                           "math error", "", "software trap", "" };

static void pic24_fault(esk_an *a, esk_an_core *k, struct esk_cpu *c, esk_an_fault *ft, esk_strbuf *o)
{
    const struct pic24_state *p = (const struct pic24_state *)c;
    char w[200];
    /* the instruction that raised it: the core's record for the traps an
     * instruction raises, else the one the trap returns to */
    int own = ft->n == TRAP_ADDR || ft->n == TRAP_STACK || ft->n == TRAP_MATH, i;
    u32 hi = 0, pc = c->ops->pc(c);
    u16 r[16];
    if (own)
        ft->pc = p->trap_pc;
    esk_snprintf(ft->name, sizeof ft->name, "%s", pic24_trap[ft->n]);
    esk_strbuf_printf(o, "%s (trap %d) at 0x%06x, in %s\n", ft->name, ft->n, (unsigned)ft->pc,
                      where(a, ft->pc, w, sizeof w));
    if (own && p->trap_why[0])
        esk_strbuf_printf(o, "  %s\n", p->trap_why);
    insn_line(a, c, ft->pc, o);
    /* as the trap found them: the entry pushed the PC and SR's low byte
     * (with IPL3), and raised the priority */
    for (i = 0; i < 16; i++)
        r[i] = p->w[i];
    r[15] = (u16)(p->w[15] - 4);
    (void)esk_bus_debug_read(&a->m->bus, PIC24_DATA + (u16)(p->w[15] - 2), 2, &hi);
    esk_strbuf_printf(o, "  registers:\n");
    for (i = 0; i < 16; i += 4)
        esk_strbuf_printf(o, "    w%-2d 0x%04x  w%-2d 0x%04x  w%-2d 0x%04x  w%-2d 0x%04x\n", i, r[i], i + 1,
                          r[i + 1], i + 2, r[i + 2], i + 3, r[i + 3]);
    esk_strbuf_printf(o, "    sr  0x%04x  splim 0x%04x  corcon 0x%04x  rcount 0x%04x\n",
                      (unsigned)((p->sr & 0x100u) | (hi >> 8 & 0xffu)), (unsigned)p->splim,
                      (unsigned)((p->corcon & ~(u32)CORCON_IPL3) | ((hi & 0x80u) ? CORCON_IPL3 : 0u)),
                      (unsigned)p->rcount);
    shadow_bt(a, k, ft, ft->pc, o);
    esk_snprintf(ft->handler, sizeof ft->handler, "0x%06x, %.60s (the trap's vector)", (unsigned)pc,
                 where(a, pc, w, sizeof w));
    esk_snprintf(ft->summary, sizeof ft->summary, "%s trap at 0x%06x in %s%s%.100s", ft->name, (unsigned)ft->pc,
                 where(a, ft->pc, w, sizeof w), own && p->trap_why[0] ? ": " : "", own ? p->trap_why : "");
}

/* ---- the step's part ------------------------------------------------------- */

int esk_an_is_fault(const esk_an *a, int n)
{
    switch (a->arch) {
    case AN_ARM: return n >= EXC_HARD && n <= EXC_USAGE;
    case AN_RV: return n >= 0 && n < 8 && n != 3;
    case AN_MIPS: return n >= 0 && n < 32 && mips_exc(n) != NULL;
    case AN_PIC24: return n >= 0 && n < 8 && n != TRAP_RES5 && n != TRAP_RES7;
    default: return 0;
    }
}

static esk_an_fault *new_fault(esk_an *a, esk_an_core *k, int n, u32 pc)
{
    esk_an_fault *f;
    a->fault_count++;
    if (a->nfaults == ESK_AN_FAULTS)
        return NULL;
    f = &a->faults[a->nfaults++];
    memset(f, 0, sizeof *f);
    f->core = (int)(k - a->core);
    f->n = n;
    f->pc = pc;
    return f;
}

static void finish(esk_an *a, esk_an_fault *f, esk_strbuf *o)
{
    char head[24];
    if (f->handler[0])
        esk_strbuf_printf(o, "  handler: %s\n", f->handler);
    f->text = o->oom ? NULL : esk_strdup(a->a, o->data ? o->data : "");
    esk_strbuf_free(o);
    if (a->ncores > 1)
        esk_snprintf(head, sizeof head, "core%d: ", f->core);
    else
        head[0] = 0;
    if (a->m->host.diag && !f->lockup) { /* a lockup is mcu.lockup already */
        char msg[300];
        esk_snprintf(msg, sizeof msg, "%s%s%s%s", head, f->summary, f->handler[0] ? "; handler " : "", f->handler);
        a->m->host.diag(a->m->host.ctx, ES_SEV_ERROR, "mcu.fault", msg);
    }
}

void esk_an_fault_taken(esk_an *a, esk_an_core *k, struct esk_cpu *c, int n, u32 ret)
{
    esk_an_fault *f = new_fault(a, k, n, ret);
    esk_strbuf o;
    if (!f)
        return;
    esk_strbuf_init(&o, a->a);
    switch (a->arch) {
    case AN_ARM: arm_fault(a, k, c, f, &o); break;
    case AN_RV: rv_fault(a, k, c, f, &o); break;
    case AN_MIPS: mips_fault(a, k, c, f, &o); break;
    default: pic24_fault(a, k, c, f, &o); break;
    }
    finish(a, f, &o);
}

/* after each step: a lockup, which no handler follows, once a reset */
void esk_an_fault_step(esk_an *a, esk_an_core *k, struct esk_cpu *c)
{
    char w[200];
    esk_an_fault *f;
    esk_strbuf o;
    u32 pc;
    if (a->m->state != ESK_MACH_LOCKUP || a->lockup_seen)
        return;
    a->lockup_seen = 1;
    pc = c->ops->pc(c);
    f = new_fault(a, k, -1, pc);
    if (!f)
        return;
    f->lockup = 1;
    esk_snprintf(f->name, sizeof f->name, "lockup");
    esk_strbuf_init(&o, a->a);
    esk_strbuf_printf(&o, "the core locked up at 0x%08x, in %s%s\n", (unsigned)pc, where(a, pc, w, sizeof w),
                      a->nfaults > 1 ? ": the fault before had no handler" : "");
    if (a->arch != AN_AVR)
        insn_line(a, c, pc, &o);
    shadow_bt(a, k, f, pc, &o);
    esk_snprintf(f->summary, sizeof f->summary, "the core locked up at 0x%08x in %s", (unsigned)pc,
                 where(a, pc, w, sizeof w));
    finish(a, f, &o);
}

void esk_an_fault_clear(esk_an *a)
{
    int i;
    for (i = 0; i < a->nfaults; i++)
        esk_strfree(a->a, a->faults[i].text);
    a->nfaults = a->fault_count = 0;
}

/* ---- the report --------------------------------------------------------- */

int esk_an_faults(esk_an *a, const char *name, esk_strbuf *o)
{
    int i;
    if (!(a->what & ESK_AN_FAULT))
        return 1;
    esk_strbuf_printf(o, "embsim faults: %s: %d\n", name ? name : "", a->fault_count);
    for (i = 0; i < a->nfaults; i++) {
        const esk_an_fault *f = &a->faults[i];
        if (a->ncores > 1)
            esk_strbuf_printf(o, "core%d: ", f->core);
        esk_strbuf_printf(o, "%s", f->text ? f->text : "");
    }
    if (a->fault_count > a->nfaults)
        esk_strbuf_printf(o, "(%d more not shown)\n", a->fault_count - a->nfaults);
    return !o->oom;
}

esk_json *esk_an_faults_json(esk_an *a, esk_alloc *al)
{
    esk_json *out, *list;
    int i, j;
    char w[200];
    if (!(a->what & ESK_AN_FAULT))
        return NULL;
    out = esk_json_new_object(al);
    list = esk_json_new_array(al);
    if (!out || !list) {
        esk_json_free(al, list);
        return out;
    }
    esk_json_set_integer(al, out, "count", a->fault_count);
    for (i = 0; i < a->nfaults; i++) {
        const esk_an_fault *f = &a->faults[i];
        esk_json *x = esk_json_new_object(al), *bt = esk_json_new_array(al);
        if (!x || !bt) {
            esk_json_free(al, x);
            esk_json_free(al, bt);
            break;
        }
        esk_json_set_integer(al, x, "core", f->core);
        if (f->lockup)
            esk_json_set_bool(al, x, "lockup", 1);
        else
            esk_json_set_integer(al, x, "exception", f->n);
        esk_json_set_string(al, x, "name", f->name);
        esk_json_set_integer(al, x, "pc", f->pc);
        esk_json_set_string(al, x, "where", where(a, f->pc, w, sizeof w));
        esk_json_set_string(al, x, "summary", f->summary);
        if (f->handler[0])
            esk_json_set_string(al, x, "handler", f->handler);
        for (j = 0; j < f->nbt; j++) {
            esk_json *e = esk_json_new_object(al);
            if (!e)
                break;
            esk_json_set_integer(al, e, "pc", f->bt[j]);
            esk_json_set_string(al, e, "where", where(a, f->bt[j], w, sizeof w));
            esk_json_array_push(al, bt, e);
        }
        esk_json_object_set(al, x, "backtrace", bt);
        esk_json_set_string(al, x, "text", f->text ? f->text : "");
        esk_json_array_push(al, list, x);
    }
    esk_json_object_set(al, out, "faults", list);
    return out;
}
