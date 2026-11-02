/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* debug.c -- the Cortex-M's halting debug, as a debugger reaches it through
 * the debug port's memory access port (ARMv7-M ARM, ARM DDI 0403E, C1;
 * ARMv6-M ARM, ARM DDI 0419E, C1):
 *
 *   - DHCSR (0xE000EDF0): written with DBGKEY (0xA05F) in its top half.
 *     C_DEBUGEN only from the debug port (the core's own writes keep it);
 *     with it, C_HALT stops the core (Debug state: no instruction runs,
 *     the clocks and peripherals go on) and clearing C_HALT restarts it,
 *     after one instruction or exception entry when C_STEP is set;
 *     C_MASKINTS keeps PendSV, SysTick and the interrupts out while it
 *     steps. S_HALT, S_SLEEP, S_LOCKUP, S_REGRDY, and the sticky S_RETIRE_ST
 *     (an instruction has completed) and S_RESET_ST (the core has reset),
 *     each cleared by a read;
 *   - DCRSR and DCRDR (0xE000EDF4, EDF8): in Debug state, a core register
 *     by REGSEL (R0-R12, SP, LR, DebugReturnAddress, xPSR, MSP, PSP, the
 *     CONTROL, FAULTMASK, BASEPRI and PRIMASK word, FPSCR and S0-S31 with
 *     an FPU) read into or written from DCRDR, S_REGRDY at once;
 *   - DEMCR (0xE000EDFC): vector catch on the core's reset (VC_CORERESET:
 *     Debug state before its first instruction) and, on ARMv7-M, on a
 *     HardFault, an exception entry or return error, a BusFault, a state,
 *     checking or coprocessor UsageFault and a MemManage fault (ARMv6-M:
 *     VC_HARDERR); TRCENA (DWTENA) as written;
 *   - DFSR (0xE000ED30): why the core halted (HALTED, BKPT, DWTTRAP,
 *     VCATCH), each bit cleared by writing it;
 *   - BKPT with C_DEBUGEN set halts the core at it (DFSR.BKPT);
 *   - the Flash Patch and Breakpoint unit (FPBv1, 0xE0002000): FP_CTRL
 *     (KEY, ENABLE, NUM_CODE), FP_REMAP (no remapping), FP_COMPn: an
 *     instruction at a comparator's halfword halts the core before it
 *     runs (DFSR.BKPT). Six code and two literal comparators on the M3 and
 *     M4, four on the M0+'s breakpoint unit;
 *   - the DWT's comparators (dwt.c) as watchpoints: an instruction whose
 *     read or write matches one halts the core after it (DFSR.DWTTRAP).
 *
 * A system reset keeps all of it (the debug domain is reset only at power
 * on), so a debugger's "reset halt" (VC_CORERESET, then SYSRESETREQ or
 * NRST) stops the core at its reset vector.
 *
 * Omissions: the debug monitor exception (DEMCR's MON_ bits, DebugMonitor:
 * a BKPT without C_DEBUGEN is the HardFault it is without a monitor),
 * C_SNAPSTALL and imprecise entry, the FPB's flash patching (FP_REMAP's
 * remapping, REPLACE's literal comparators match nothing), a halted core's
 * supply current (it draws its sleep current, not its run current). */
#include "mcu/cortexm/cortexm.h"

#define DBGKEY 0xA05Fu

/* the bus access in progress comes from the debug port, not the core */
static int from_dap(const struct esk_cm_state *cs) { return cs->sim->bus.dap; }

static int fp_count(const struct esk_cm_state *cs)
{
    /* NUM_CODE: the M3 and M4 have six code comparators and two literal;
     * the M0+'s BPU four */
    return cs->model->arch == ARCH_V6M ? 4 : 8;
}

static u32 fp_ctrl_ro(const struct esk_cm_state *cs)
{
    return cs->model->arch == ARCH_V6M ? 4u << 4 : (6u << 4) | (2u << 8);
}

static void fp_update(struct esk_cm_state *cs)
{
    int i, any = 0;
    for (i = 0; i < fp_count(cs); i++)
        if (cs->dbg.fp_comp[i] & 1u)
            any = 1;
    cs->dbg.fp_on = (cs->dbg.fp_ctrl & 1u) && any;
}

void esk_cm_dbg_halt(struct esk_cm_state *cs, u32 why)
{
    struct esk_cm_debug *d = &cs->dbg;
    if (!(d->ctrl & DHCSR_C_DEBUGEN))
        return;
    d->dfsr |= why;
    if (d->halted)
        return;
    d->halted = 1;
    d->stepping = 0;
    d->ctrl |= DHCSR_C_HALT;
    cs->sim->dbg.halted = 1;
    if (cs->sim->state == ESK_MACH_LOCKUP) { /* entering Debug state ends a lockup */
        cs->sim->state = ESK_MACH_RUN;
        cs->sim->end_why[0] = 0;
    }
    cs->in_wfe = 0; /* a halt during a wait ends it */
    cs->sim->asleep = 1;
    cs->sim->yield = 1;
}

/* leaving Debug state: the core runs from DebugReturnAddress */
static void resume(struct esk_cm_state *cs, int step)
{
    struct esk_cm_debug *d = &cs->dbg;
    d->halted = 0;
    d->stepping = step;
    d->resumed = 1;
    cs->sim->dbg.halted = 0;
    cs->sim->asleep = 0;
}

static u32 dhcsr(struct esk_cm_state *cs)
{
    struct esk_cm_debug *d = &cs->dbg;
    u32 v = d->ctrl & (DHCSR_C_DEBUGEN | DHCSR_C_HALT | DHCSR_C_STEP | DHCSR_C_MASKINTS | DHCSR_C_SNAPSTALL);
    v |= 1u << 16; /* S_REGRDY: a DCRSR transfer completes at once */
    if (d->halted)
        v |= 1u << 17;
    if (cs->sim->asleep && !d->halted)
        v |= 1u << 18; /* S_SLEEP */
    if (cs->sim->state == ESK_MACH_LOCKUP)
        v |= 1u << 19;
    if (d->retire_st)
        v |= 1u << 24;
    if (d->reset_st)
        v |= 1u << 25;
    if (!cs->sim->bus.debug) /* a debugger's own look clears nothing */
        d->retire_st = d->reset_st = 0;
    return v;
}

static void dhcsr_write(struct esk_cm_state *cs, u32 v)
{
    struct esk_cm_debug *d = &cs->dbg;
    u32 c = v & (DHCSR_C_DEBUGEN | DHCSR_C_HALT | DHCSR_C_STEP | DHCSR_C_MASKINTS | DHCSR_C_SNAPSTALL);
    if ((v >> 16) != DBGKEY)
        return;
    if (!from_dap(cs)) /* the core's own write cannot change C_DEBUGEN */
        c = (c & ~DHCSR_C_DEBUGEN) | (d->ctrl & DHCSR_C_DEBUGEN);
    if (!(c & DHCSR_C_DEBUGEN)) {
        d->ctrl = 0;
        if (d->halted)
            resume(cs, 0);
        return;
    }
    d->ctrl = c;
    if ((c & DHCSR_C_HALT) && !d->halted)
        esk_cm_dbg_halt(cs, DFSR_HALTED);
    else if (!(c & DHCSR_C_HALT) && d->halted)
        resume(cs, (c & DHCSR_C_STEP) != 0);
}

static void dcrsr_write(struct esk_cm_state *cs, u32 v)
{
    struct esk_cm_debug *d = &cs->dbg;
    u32 val = d->dcrdr;
    if (!d->halted)
        return; /* outside Debug state the transfer is UNPREDICTABLE: none */
    if (v & (1u << 16)) {
        (void)esk_cm_dcrsr(cs, v & 0x7fu, 1, &val);
    } else if (esk_cm_dcrsr(cs, v & 0x7fu, 0, &val)) {
        d->dcrdr = val;
    }
}

int esk_cm_dbg_read(struct esk_cm_state *cs, u32 o, u32 *v)
{
    switch (o) {
    case 0xd30: *v = cs->dbg.dfsr; return 1;
    case 0xdf0: *v = dhcsr(cs); return 1;
    case 0xdf4: *v = 0; return 1; /* DCRSR is write-only */
    case 0xdf8: *v = cs->dbg.dcrdr; return 1;
    case 0xdfc:
        *v = cs->demcr & (cs->model->arch == ARCH_V6M ? (DEMCR_VC_CORERESET | 1u << 10 | DEMCR_TRCENA)
                                                      : 0x010F07F1u);
        return 1;
    }
    return 0;
}

int esk_cm_dbg_write(struct esk_cm_state *cs, u32 o, u32 v)
{
    switch (o) {
    case 0xd30: cs->dbg.dfsr &= ~(v & 0x1fu); return 1;
    case 0xdf0: dhcsr_write(cs, v); return 1;
    case 0xdf4: dcrsr_write(cs, v); return 1;
    case 0xdf8: cs->dbg.dcrdr = v; return 1;
    case 0xdfc:
        cs->demcr = v & (cs->model->arch == ARCH_V6M ? (DEMCR_VC_CORERESET | 1u << 10 | DEMCR_TRCENA)
                                                     : 0x010F07F1u);
        return 1;
    }
    return 0;
}

/* ---- around an instruction --------------------------------------------- */

int esk_cm_dbg_before(struct esk_cm_state *cs)
{
    struct esk_cm_debug *d = &cs->dbg;
    int i, n;
    if (d->halted) {
        cs->sim->asleep = 1;
        return 1;
    }
    if (!d->fp_on || cs->pc >= 0x20000000u)
        return 0;
    n = cs->model->arch == ARCH_V6M ? 4 : 6; /* the code comparators */
    for (i = 0; i < n; i++) {
        u32 c = d->fp_comp[i], at = c & 0x1ffffffcu, rep = c >> 30;
        if (!(c & 1u) || rep == 0)
            continue; /* disabled, or a literal comparator (REPLACE 00 remaps) */
        if (at == (cs->pc & ~3u) && (rep == 3u || (rep == 1u) == !(cs->pc & 2u))) {
            esk_cm_dbg_halt(cs, DFSR_BKPT);
            return 1;
        }
    }
    return 0;
}

void esk_cm_dbg_after(struct esk_cm_state *cs)
{
    struct esk_cm_debug *d = &cs->dbg;
    d->retire_st = 1;
    if (d->dwt_hit) {
        d->dwt_hit = 0;
        esk_cm_dbg_halt(cs, DFSR_DWTTRAP);
    }
    if (d->stepping && !d->halted)
        esk_cm_dbg_halt(cs, DFSR_HALTED);
}

/* a data access the DWT's comparators watch: FUNCTION 5 reads, 6 writes,
 * 7 both, over the address with MASK's low bits ignored */
void esk_cm_dbg_access(struct esk_cm_state *cs, u32 a, int n, int write)
{
    struct esk_cm_debug *d = &cs->dbg;
    int i, cmps = cs->model->arch == ARCH_V6M ? 2 : 4;
    for (i = 0; i < cmps; i++) {
        u32 f = d->dwt_func[i] & 0xfu, m = d->dwt_mask[i] & 0x1fu, lo, hi;
        u32 mask = m >= 32 ? 0 : ~((1u << m) - 1u);
        if (f < 5 || f > 7 || (f == 5 && write) || (f == 6 && !write))
            continue;
        lo = a & mask;
        hi = (a + (u32)n - 1u) & mask;
        if (lo == (d->dwt_comp[i] & mask) || hi == (d->dwt_comp[i] & mask)) {
            d->dwt_func[i] |= 1u << 24; /* MATCHED */
            if (d->ctrl & DHCSR_C_DEBUGEN)
                d->dwt_hit = 1;
        }
    }
}

/* the core's reset: the debug domain stays unless it is a power-on; the
 * core halts at its reset vector with VC_CORERESET, or when C_HALT stood */
void esk_cm_dbg_reset(struct esk_cm_state *cs)
{
    struct esk_cm_debug *d = &cs->dbg;
    d->reset_st = 1;
    d->halted = 0;
    cs->sim->dbg.halted = 0;
    d->stepping = 0;
    d->dwt_hit = 0;
    if ((d->ctrl & DHCSR_C_DEBUGEN) && ((cs->demcr & DEMCR_VC_CORERESET) || (d->ctrl & DHCSR_C_HALT)))
        esk_cm_dbg_halt(cs, (cs->demcr & DEMCR_VC_CORERESET) ? DFSR_VCATCH : DFSR_HALTED);
}

/* ---- the FPB ------------------------------------------------------------ */

static u32 fpb_read(void *ctx, u32 off, int n)
{
    struct esk_cm_state *cs = (struct esk_cm_state *)ctx;
    (void)n;
    if (off == 0)
        return (cs->dbg.fp_ctrl & 1u) | fp_ctrl_ro(cs);
    if (off == 4)
        return 0; /* FP_REMAP: RMPSPT 0, no remapping */
    if (off >= 8 && off < 8u + 4u * (u32)fp_count(cs))
        return cs->dbg.fp_comp[(off - 8) / 4];
    /* the CoreSight identification registers: an FPB (ARM DDI 0403E C1.11) */
    switch (off) {
    case 0xfd0: return 0x04;
    case 0xfe0: return cs->model->arch == ARCH_V6M ? 0x0b : 0x03;
    case 0xfe4: return 0xb0;
    case 0xfe8: return 0x0b;
    case 0xff0: return 0x0d;
    case 0xff4: return 0xe0;
    case 0xff8: return 0x05;
    case 0xffc: return 0xb1;
    }
    return 0;
}

static void fpb_write(void *ctx, u32 off, int n, u32 v)
{
    struct esk_cm_state *cs = (struct esk_cm_state *)ctx;
    (void)n;
    if (off == 0) {
        if (v & 2u) /* KEY */
            cs->dbg.fp_ctrl = v & 1u;
    } else if (off >= 8 && off < 8u + 4u * (u32)fp_count(cs)) {
        cs->dbg.fp_comp[(off - 8) / 4] = v & 0xdffffffdu;
    }
    fp_update(cs);
}

const struct esk_dev_ops esk_fpb_ops = { "fpb", fpb_read, fpb_write, 0, 0, 0 };

/* ---- the ROM table -------------------------------------------------------- */

static u32 rom_read(void *ctx, u32 off, int n)
{
    const struct esk_cm_state *cs = (const struct esk_cm_state *)ctx;
    int v6 = cs->model->arch == ARCH_V6M;
    (void)n;
    switch (off) {
    /* each entry the offset of its component from the table, bit 0 present
     * (bit 1: the entry's format) */
    case 0x000: return 0xFFF0F003u; /* SCS */
    case 0x004: return 0xFFF02003u; /* DWT */
    case 0x008: return 0xFFF03003u; /* FPB (BPU) */
    case 0x00c: return v6 ? 0 : 0xFFF01002u; /* ITM: not present */
    case 0x010: return v6 ? 0 : 0xFFF41002u; /* TPIU: not present */
    case 0x014: return v6 ? 0 : 0xFFF42002u; /* ETM: not present */
    case 0xfcc: return 1u;                  /* MEMTYPE: the system memory is on this bus */
    case 0xfd0: return 0x04;
    case 0xfe0: return v6 ? 0x71 : 0xC4;
    case 0xfe4: return 0xB4;
    case 0xfe8: return 0x0B;
    case 0xff0: return 0x0D;
    case 0xff4: return 0x10;                /* a ROM table */
    case 0xff8: return 0x05;
    case 0xffc: return 0xB1;
    }
    return 0;
}

const struct esk_dev_ops esk_romtable_ops = { "romtable", rom_read, 0, 0, 0, 0 };
