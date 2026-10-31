/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* smp.c -- two Cortex-M cores on one machine (the RP2040's), behind the one
 * core interface the machine runs (SPEC-009 §3).
 *
 *   - Each core keeps its own time in cycles. A step runs the core that is
 *     furthest behind (core 0 on a tie); the machine's time is the earliest
 *     of the running cores', and the devices see it as it advances, so the
 *     two share one clock and neither runs ahead of the other by more than
 *     an instruction. A device a core accesses sees that core's time.
 *   - Each core has its own NVIC and SCB, SysTick and DWT: an access to the
 *     system control space reaches the core making it (a debugger's, the
 *     core that runs next). An interrupt line reaches every core's NVIC;
 *     esk_cortexm_core_interrupt pends one core's only (the RP2040's
 *     SIO_IRQ_PROCn and IO_IRQ_BANK0's per-core summaries).
 *   - WFI, WFE and a branch to itself put one core to sleep; the machine
 *     sleeps when no core runs. An interrupt wakes the cores it would
 *     interrupt; SEV sets every core's event register and wakes a core
 *     waiting in WFE.
 *   - A core other than core 0 starts powered but held (its bootrom's wait,
 *     the part's model answers for it: rp2040/sio.c) until the part launches
 *     it at an entry with a stack and a vector table; powered off (PSM's
 *     FRCE_OFF), it is reset and does nothing.
 *   - The debugger sees the core that runs next: its program counter (so a
 *     breakpoint stops either core) and its registers.
 *
 * Written for EmbSIM: EmbCC's simulator runs one core. */
#include <string.h>

#include "mcu/cortexm/cortexm.h"

#define SMP_MAX 2

enum { CORE_RUN, CORE_SLEEP, CORE_BOOT, CORE_OFF };

struct smp;

struct smp_core {
    struct smp *smp;
    int n;
};

struct smp {
    struct esk_cpu cpu; /* the interface; first */
    struct esk_mach *m;
    int n;
    struct esk_cm_state *c[SMP_MAX];
    void *st[SMP_MAX], *dw[SMP_MAX];
    struct smp_core self[SMP_MAX];
    int state[SMP_MAX];
    u64 lt[SMP_MAX]; /* each core's time */
    int cur;         /* the core whose instruction is in progress */
    int stepping;
};

static void smp_step(struct esk_cpu *cpu);

/* the machine's cores, when they are smp.c's (its step is ours) */
static struct smp *smp_of(struct esk_mach *m)
{
    return m->cpu && m->cpu->ops->step == smp_step ? (struct smp *)m->cpu : 0;
}

/* the running core furthest behind, or core 0 when none runs */
static int next_core(const struct smp *x)
{
    int i, best = -1;
    for (i = 0; i < x->n; i++)
        if (x->state[i] == CORE_RUN && (best < 0 || x->lt[i] < x->lt[best]))
            best = i;
    return best < 0 ? 0 : best;
}

static int current(const struct smp *x)
{
    return x->stepping ? x->cur : next_core(x);
}

static int any_running(const struct smp *x)
{
    int i;
    for (i = 0; i < x->n; i++)
        if (x->state[i] == CORE_RUN)
            return 1;
    return 0;
}

/* core i runs again from `now` */
static void wake(struct smp *x, int i, u64 now)
{
    x->state[i] = CORE_RUN;
    x->c[i]->in_wfe = 0;
    if (x->lt[i] < now)
        x->lt[i] = now;
    x->m->asleep = 0;
}

/* exception n pends on core i; a sleeping core it would interrupt wakes */
static void pend(struct smp *x, int i, int n)
{
    struct esk_cm_state *c = x->c[i];
    if (x->state[i] == CORE_OFF)
        return;
    esk_cm_set_pend(c, n);
    if (x->state[i] == CORE_SLEEP && c->cpu.ops->wake_pending(&c->cpu))
        wake(x, i, x->m->cycles);
}

static void core_pend(void *ctx, int n)
{
    struct smp_core *k = (struct smp_core *)ctx;
    pend(k->smp, k->n, n);
}

/* SEV: every core's event register; a core waiting in WFE wakes, its
 * event consumed */
static void smp_sev(void *ctx)
{
    struct smp *x = (struct smp *)ctx;
    int i;
    for (i = 0; i < x->n; i++) {
        if (x->state[i] == CORE_SLEEP && x->c[i]->in_wfe) {
            x->c[i]->event = 0;
            wake(x, i, x->m->cycles);
        } else if (x->state[i] == CORE_RUN) {
            x->c[i]->event = 1;
        }
    }
}

/* ---- the core interface ------------------------------------------------- */

static void smp_reset(struct esk_cpu *cpu)
{
    struct smp *x = (struct smp *)cpu;
    int i;
    for (i = 0; i < x->n; i++) {
        x->c[i]->cpu.ops->reset(&x->c[i]->cpu);
        x->state[i] = i == 0 ? CORE_RUN : CORE_BOOT;
        x->lt[i] = x->m->cycles;
    }
    x->stepping = 0;
    x->cur = 0;
}

static void smp_step(struct esk_cpu *cpu)
{
    struct smp *x = (struct smp *)cpu;
    struct esk_mach *m = x->m;
    u64 g0 = m->cycles, g = ~(u64)0;
    int i = next_core(x), counting = m->counting, j, slept;
    if (x->state[i] != CORE_RUN) {
        m->asleep = 1;
        return;
    }
    /* the core runs on its own time; the devices are advanced after */
    m->cycles = x->lt[i];
    m->counting = 0;
    m->asleep = 0;
    x->cur = i;
    x->stepping = 1;
    esk_mach_step_core(m, i, &x->c[i]->cpu);
    x->stepping = 0;
    slept = m->asleep;
    x->lt[i] = m->cycles;
    m->counting += counting; /* with the clocks the step started or stopped */
    if (slept && x->state[i] == CORE_RUN)
        x->state[i] = CORE_SLEEP;
    for (j = 0; j < x->n; j++)
        if (x->state[j] == CORE_RUN && x->lt[j] < g)
            g = x->lt[j];
    if (g == ~(u64)0)
        g = x->lt[i] > g0 ? x->lt[i] : g0; /* every core waits */
    m->cycles = g0;
    if (g > g0) {
        m->cycles = g;
        if (m->counting)
            esk_mach_advance(m, (u32)(g - g0));
    }
    m->asleep = !any_running(x);
}

static u32 smp_pc(struct esk_cpu *cpu)
{
    struct smp *x = (struct smp *)cpu;
    struct esk_cm_state *c = x->c[next_core(x)];
    return c->cpu.ops->pc(&c->cpu);
}

static int smp_reg_read(struct esk_cpu *cpu, int n, u8 *buf)
{
    struct smp *x = (struct smp *)cpu;
    struct esk_cm_state *c = x->c[next_core(x)];
    return c->cpu.ops->reg_read(&c->cpu, n, buf);
}

static int smp_reg_write(struct esk_cpu *cpu, int n, const u8 *buf)
{
    struct smp *x = (struct smp *)cpu;
    struct esk_cm_state *c = x->c[next_core(x)];
    return c->cpu.ops->reg_write(&c->cpu, n, buf);
}

static const int *smp_g_regs(struct esk_cpu *cpu)
{
    struct smp *x = (struct smp *)cpu;
    return x->c[0]->cpu.ops->gdb_g_regs(&x->c[0]->cpu);
}

static const char *smp_xml(struct esk_cpu *cpu, const char *annex)
{
    struct smp *x = (struct smp *)cpu;
    return x->c[0]->cpu.ops->gdb_xml(&x->c[0]->cpu, annex);
}

static void smp_interrupt(struct esk_cpu *cpu, int n)
{
    struct smp *x = (struct smp *)cpu;
    int i;
    for (i = 0; i < x->n; i++)
        pend(x, i, n);
}

static const char *smp_reg_name(struct esk_cpu *cpu, int n)
{
    struct smp *x = (struct smp *)cpu;
    return x->c[0]->cpu.ops->reg_name(&x->c[0]->cpu, n);
}

static int smp_wake_pending(struct esk_cpu *cpu)
{
    struct smp *x = (struct smp *)cpu;
    int i;
    for (i = 0; i < x->n; i++)
        if (x->state[i] == CORE_SLEEP && x->c[i]->cpu.ops->wake_pending(&x->c[i]->cpu))
            wake(x, i, x->m->cycles);
    return any_running(x);
}

static int smp_disasm(struct esk_cpu *cpu, u32 addr, char *text, size_t cap)
{
    struct smp *x = (struct smp *)cpu;
    return x->c[0]->cpu.ops->disasm(&x->c[0]->cpu, addr, text, cap);
}

static const struct esk_cpu_ops smp_ops = {
    "cortex-m", 40, "an ARM image",
    smp_reset, smp_step, smp_pc, smp_reg_read, smp_reg_write,
    smp_g_regs, smp_xml, 2, smp_interrupt, 15, 1, 0, smp_reg_name, smp_wake_pending,
    smp_disasm, 0, 0,
};

/* ---- each core's devices: the access goes to the core making it ---------- */

static u32 scs_read(void *ctx, u32 off, int n)
{
    struct smp *x = (struct smp *)ctx;
    return esk_scs_ops.read(x->c[current(x)], off, n);
}

static void scs_write(void *ctx, u32 off, int n, u32 v)
{
    struct smp *x = (struct smp *)ctx;
    esk_scs_ops.write(x->c[current(x)], off, n, v);
}

static void scs_reset(void *ctx)
{
    struct smp *x = (struct smp *)ctx;
    int i;
    for (i = 0; esk_scs_ops.reset && i < x->n; i++)
        esk_scs_ops.reset(x->c[i]);
}

static const struct esk_dev_ops smp_scs_ops = {
    "scs", scs_read, scs_write, scs_reset, 0, 0,
};

/* a device of each core's (SysTick, DWT): every core's keeps time */
#define PER_CORE(name, base, field)                                                                \
    static u32 name##_read(void *ctx, u32 off, int n)                                              \
    {                                                                                              \
        struct smp *x = (struct smp *)ctx;                                                         \
        return base.read ? base.read(x->field[current(x)], off, n) : 0;                            \
    }                                                                                              \
    static void name##_write(void *ctx, u32 off, int n, u32 v)                                     \
    {                                                                                              \
        struct smp *x = (struct smp *)ctx;                                                         \
        if (base.write)                                                                            \
            base.write(x->field[current(x)], off, n, v);                                           \
    }                                                                                              \
    static void name##_reset(void *ctx)                                                            \
    {                                                                                              \
        struct smp *x = (struct smp *)ctx;                                                         \
        int i;                                                                                     \
        for (i = 0; base.reset && i < x->n; i++)                                                   \
            base.reset(x->field[i]);                                                               \
    }                                                                                              \
    static void name##_tick(void *ctx, u32 cycles)                                                 \
    {                                                                                              \
        struct smp *x = (struct smp *)ctx;                                                         \
        int i;                                                                                     \
        for (i = 0; base.tick && i < x->n; i++)                                                    \
            base.tick(x->field[i], cycles);                                                        \
    }                                                                                              \
    static int name##_next(void *ctx, u32 *cycles)                                                 \
    {                                                                                              \
        struct smp *x = (struct smp *)ctx;                                                         \
        int i, any = 0;                                                                            \
        u32 c;                                                                                     \
        for (i = 0; base.next_event && i < x->n; i++)                                              \
            if (base.next_event(x->field[i], &c) && (!any || c < *cycles)) {                       \
                *cycles = c;                                                                       \
                any = 1;                                                                           \
            }                                                                                      \
        return any;                                                                                \
    }                                                                                              \
    static const struct esk_dev_ops name##_ops = {                                                 \
        #name, name##_read, name##_write, name##_reset, name##_tick, name##_next,                  \
    };

PER_CORE(smp_systick, esk_systick_ops, st)
PER_CORE(smp_dwt, esk_dwt_ops, dw)

/* a store clears every core's exclusive monitor on its address */
static void smp_snoop(void *ctx, u32 a)
{
    struct smp *x = (struct smp *)ctx;
    int i;
    for (i = 0; i < x->n; i++)
        if (x->c[i]->excl_valid && a - x->c[i]->excl_addr < 4)
            x->c[i]->excl_valid = 0;
}

/* ---- the machine's way in --------------------------------------------------- */

es_status esk_cortexm_create_smp(struct esk_mach *m, const char *model, int prio_bits, int cores,
                                 struct esk_cpu **out)
{
    struct esk_cpu *c0 = 0;
    struct smp *x;
    es_status st;
    int i, k;
    if (cores < 1 || cores > SMP_MAX)
        return ES_EINVAL;
    st = esk_cortexm_create(m, model, prio_bits, &c0);
    if (st != ES_OK || cores == 1) {
        *out = c0;
        return st;
    }
    x = (struct smp *)esk_mach_alloc(m, sizeof *x);
    if (!x)
        return ES_ENOMEM;
    x->cpu.ops = &smp_ops;
    x->m = m;
    x->n = cores;
    x->c[0] = (struct esk_cm_state *)c0;
    for (i = 1; i < cores; i++) {
        x->c[i] = esk_cm_new(m, model, prio_bits);
        x->st[i] = esk_systick_create(m);
        x->dw[i] = esk_dwt_create(m);
        if (!x->c[i] || !x->st[i] || !x->dw[i])
            return ES_ENOMEM;
    }
    /* core 0's devices become each core's */
    for (k = 0; k < m->bus.ndev; k++) {
        struct esk_device *d = &m->bus.dev[k];
        if (d->ops == &esk_scs_ops) {
            d->ops = &smp_scs_ops;
            d->ctx = x;
        } else if (d->ops == &esk_systick_ops) {
            x->st[0] = d->ctx;
            d->ops = &smp_systick_ops;
            d->ctx = x;
        } else if (d->ops == &esk_dwt_ops) {
            x->dw[0] = d->ctx;
            d->ops = &smp_dwt_ops;
            d->ctx = x;
        }
    }
    if (!x->st[0] || !x->dw[0])
        return ES_EUNSUPPORTED;
    for (i = 0; i < cores; i++) {
        x->self[i].smp = x;
        x->self[i].n = i;
        esk_systick_target(x->st[i], core_pend, &x->self[i]);
        x->c[i]->sev = smp_sev;
        x->c[i]->sev_ctx = x;
    }
    m->bus.snoop = smp_snoop;
    m->bus.snoop_ctx = x;
    m->cpu = &x->cpu;
    m->steps_cores = 1;
    *out = &x->cpu;
    return ES_OK;
}

es_status esk_cortexm_set_mpu(struct esk_mach *m, int regions)
{
    struct smp *x = smp_of(m);
    int i;
    if (regions < 0 || regions > MPU_MAX_REGIONS || !m->cpu)
        return ES_EINVAL;
    if (!x) {
        ((struct esk_cm_state *)m->cpu)->mpu_regions = regions;
        return ES_OK;
    }
    for (i = 0; i < x->n; i++)
        x->c[i]->mpu_regions = regions;
    return ES_OK;
}

es_status esk_cortexm_set_systick(struct esk_mach *m, u32 ref_div, u32 calib)
{
    struct smp *x = smp_of(m);
    int i;
    if (x) {
        for (i = 0; i < x->n; i++)
            esk_systick_config(x->st[i], ref_div, calib);
        return ES_OK;
    }
    for (i = 0; i < m->bus.ndev; i++)
        if (m->bus.dev[i].ops == &esk_systick_ops) {
            esk_systick_config(m->bus.dev[i].ctx, ref_div, calib);
            return ES_OK;
        }
    return ES_ENOTFOUND;
}

void esk_cortexm_event(struct esk_mach *m)
{
    struct smp *x = smp_of(m);
    struct esk_cm_state *cs;
    if (x) {
        smp_sev(x);
        return;
    }
    if (!m->cpu)
        return;
    cs = (struct esk_cm_state *)m->cpu;
    if (m->asleep && cs->in_wfe && !m->powered_down) {
        cs->event = 0; /* the event that ends a WFE is consumed */
        m->asleep = 0;
    } else {
        cs->event = 1;
    }
}

int esk_cortexm_cores(struct esk_mach *m)
{
    struct smp *x = smp_of(m);
    return x ? x->n : 1;
}

int esk_cortexm_core(struct esk_mach *m)
{
    struct smp *x = smp_of(m);
    return x ? current(x) : 0;
}

void esk_cortexm_core_interrupt(struct esk_mach *m, int core, int n)
{
    struct smp *x = smp_of(m);
    if (x && core >= 0 && core < x->n)
        pend(x, core, n);
    else if (!x && core == 0 && m->cpu)
        m->cpu->ops->interrupt(m->cpu, n);
}

void esk_cortexm_core_power(struct esk_mach *m, int core, int on)
{
    struct smp *x = smp_of(m);
    if (!x || core <= 0 || core >= x->n)
        return;
    if (!on) {
        if (x->state[core] != CORE_OFF)
            x->c[core]->cpu.ops->reset(&x->c[core]->cpu);
        x->state[core] = CORE_OFF;
    } else if (x->state[core] == CORE_OFF) {
        x->state[core] = CORE_BOOT;
    }
    m->asleep = !any_running(x);
}

int esk_cortexm_core_booting(struct esk_mach *m, int core)
{
    struct smp *x = smp_of(m);
    return x && core > 0 && core < x->n && x->state[core] == CORE_BOOT;
}

int esk_cortexm_core_running(struct esk_mach *m, int core)
{
    struct smp *x = smp_of(m);
    if (!x)
        return core == 0;
    return core >= 0 && core < x->n && (x->state[core] == CORE_RUN || x->state[core] == CORE_SLEEP);
}

void esk_cortexm_core_launch(struct esk_mach *m, int core, u32 vtor, u32 sp, u32 entry)
{
    struct smp *x = smp_of(m);
    struct esk_cm_state *c;
    if (!esk_cortexm_core_booting(m, core))
        return;
    c = x->c[core];
    c->vtor = vtor & ~0x7fu;
    c->R[13] = sp & ~3u;
    c->R[14] = 0xffffffffu;
    c->tbit = (int)(entry & 1u);
    c->pc = entry & ~1u;
    wake(x, core, m->cycles);
}
