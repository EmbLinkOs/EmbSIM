/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- what an RTOS port needs from a Cortex-M4F core: the MPU
 * (regions, permissions, subregions, overlap, the background region,
 * HFNMIENA, XN), the default memory map's execute-never regions,
 * unprivileged execution and LDRT, unprivileged access to the system
 * control space, faults on stacking and unstacking an exception frame,
 * and lazy and eager floating-point context stacking with FPCCR, FPCAR
 * and FPDSCR. Each case prints one line through semihosting: the
 * exception taken (0 for none), CFSR, MMFAR or BFAR when valid, and what
 * else it observes.
 *
 * The same image runs on QEMU's mps2-an386 (Cortex-M4 with FPU and MPU)
 * and on EmbSIM's nRF52840 (Cortex-M4F); their outputs must agree
 * (expected.txt is QEMU's; tests/protocol/test_cm4_core.py). Flash at 0
 * and SRAM at 0x20000000, where both have them. */
#include <stdint.h>
#include "semihost.h"

#define REG(a) (*(volatile uint32_t *)(a))
#define ICSR REG(0xE000ED04u)
#define SHCSR REG(0xE000ED24u)
#define CFSR REG(0xE000ED28u)
#define HFSR REG(0xE000ED2Cu)
#define MMFAR REG(0xE000ED34u)
#define BFAR REG(0xE000ED38u)
#define CPACR REG(0xE000ED88u)
#define MPU_TYPE REG(0xE000ED90u)
#define MPU_CTRL REG(0xE000ED94u)
#define MPU_RNR REG(0xE000ED98u)
#define MPU_RBAR REG(0xE000ED9Cu)
#define MPU_RASR REG(0xE000EDA0u)
#define FPCCR REG(0xE000EF34u)
#define FPCAR REG(0xE000EF38u)
#define FPDSCR REG(0xE000EF3Cu)

/* RASR: AP, XN, the size as 2^bits bytes, enabled */
#define AP(x) ((uint32_t)(x) << 24)
#define XN (1u << 28)
#define SIZE(bits) ((uint32_t)((bits) - 1) << 1)
#define RASR(ap, bits) (AP(ap) | SIZE(bits) | 1u)

static volatile uint8_t buf_ro[32] __attribute__((aligned(32)));    /* region 2: privileged read-only */
static volatile uint8_t buf_na[32] __attribute__((aligned(32)));    /* region 3: no access */
static volatile uint8_t buf_sub[256] __attribute__((aligned(256))); /* region 4: no access, subregion 2 off */
static volatile uint16_t code_buf[16] __attribute__((aligned(32))); /* region 5: code, XN or not */
static volatile uint32_t buf_pr[8] __attribute__((aligned(32)));    /* region 6: privileged only */
static uint32_t bad_stack[16] __attribute__((aligned(64)));         /* region 7: a stack it may not use */
static uint32_t fake_frame[8] __attribute__((aligned(8)));

/* ---- what the fault handlers saw --------------------------------------- */

static volatile uint32_t f_exc, f_cfsr, f_far, f_hfsr, f_lr, f_count;
/* 0: skip the faulting instruction; 1: return to the caller of a branch
 * that faulted on its fetch; 2: the frame is unusable, resume a thread on
 * a fresh frame at f_resume */
static volatile int f_mode;
static volatile uint32_t f_resume;
static volatile uint32_t svc_count;
uint32_t saved_msp;

void fault_c(uint32_t *frame, uint32_t lr)
{
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    f_exc = ipsr & 0x1ff;
    f_cfsr = CFSR;
    f_hfsr = HFSR;
    f_far = (f_cfsr & 0x80u) ? MMFAR : (f_cfsr & 0x8000u) ? BFAR : 0;
    f_lr = lr;
    f_count++;
    CFSR = f_cfsr;
    HFSR = f_hfsr;
    if (f_mode == 0) {
        uint16_t hw = *(volatile uint16_t *)(frame[6]);
        frame[6] += (hw >> 11) >= 0x1d ? 4 : 2;
    } else if (f_mode == 1) {
        frame[6] = frame[5] & ~1u;
    } else {
        uint32_t i;
        for (i = 0; i < 8; i++)
            fake_frame[i] = 0;
        fake_frame[6] = f_resume;
        fake_frame[7] = 0x01000000u;
        __asm__ volatile("msr psp, %0" : : "r"(fake_frame));
    }
}

#define FAULT_ENTRY(name)                                                                          \
    __attribute__((naked)) void name(void)                                                         \
    {                                                                                              \
        __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n mov r1, lr\n"      \
                         " push {r1, lr}\n bl fault_c\n pop {r1, pc}\n");                          \
    }
FAULT_ENTRY(HardFault_Handler)
FAULT_ENTRY(MemManage_Handler)
FAULT_ENTRY(BusFault_Handler)
FAULT_ENTRY(UsageFault_Handler)

/* SVC: back to privileged thread mode; counted */
__attribute__((naked)) void SVC_Handler(void)
{
    __asm__ volatile("movw r0, #:lower16:svc_count\n movt r0, #:upper16:svc_count\n ldr r1, [r0]\n adds r1, #1\n str r1, [r0]\n"
                     "mrs r0, control\n bic r0, r0, #1\n msr control, r0\n isb\n bx lr\n");
}

/* PendSV: what the case in progress asks of it */
static volatile int pendsv_mode;
static volatile uint32_t p_fpccr1, p_fpcar, p_lr, p_sp, p_slot1, p_slot2, p_fpccr2, p_fpscr;
uint32_t bad_top;

void pendsv_fp(uint32_t sp, uint32_t lr)
{
    uint32_t v;
    /* ARMv7-M's FPCCR fields only: QEMU also sets bit 10 (ARMv8-M's
     * UFRDY), which ARMv7-M reserves and a Cortex-M4 leaves clear */
    p_fpccr1 = FPCCR & 0xC000017Bu;
    p_fpcar = FPCAR;
    p_lr = lr;
    p_sp = sp;
    p_slot1 = *(volatile uint32_t *)(sp + 0x20);
    /* the handler's first floating-point instruction: the frame's FP area
     * is written now (lazily), and the new context takes FPDSCR */
    __asm__ volatile("vmrs %0, fpscr" : "=r"(v));
    p_fpscr = v;
    p_slot2 = *(volatile uint32_t *)(sp + 0x20);
    p_fpccr2 = FPCCR & 0xC000017Bu;
    v = 0x40490FDBu;
    __asm__ volatile("vmov s0, %0" : : "r"(v) : "s0");
}

__attribute__((naked)) void PendSV_Handler(void)
{
    __asm__ volatile("movw r0, #:lower16:pendsv_mode\n movt r0, #:upper16:pendsv_mode\n ldr r0, [r0]\n cmp r0, #1\n beq 1f\n"
                     /* mode 0: the floating-point case */
                     "mrs r0, msp\n mov r1, lr\n push {r1, lr}\n bl pendsv_fp\n pop {r1, pc}\n"
                     /* mode 1: return to a thread on a stack it may not use */
                     "1: movw r0, #:lower16:bad_top\n movt r0, #:upper16:bad_top\n ldr r0, [r0]\n msr psp, r0\n"
                     " movw lr, #0xfffd\n movt lr, #0xffff\n bx lr\n");
}

/* ---- output ----------------------------------------------------------- */

static char line[256];
static int pos;

static void put(const char *s)
{
    while (*s && pos < (int)sizeof line - 1)
        line[pos++] = *s++;
}

static void hex(uint32_t v)
{
    static const char d[] = "0123456789abcdef";
    int i;
    for (i = 28; i >= 0; i -= 4)
        if (pos < (int)sizeof line - 1)
            line[pos++] = d[(v >> i) & 15];
}

static void field(const char *name, uint32_t v)
{
    put(" ");
    put(name);
    put("=");
    hex(v);
}

static void begin(const char *name)
{
    pos = 0;
    put(name);
    f_exc = f_cfsr = f_far = f_hfsr = f_lr = 0;
}

static void faults(void)
{
    field("exc", f_exc);
    field("cfsr", f_cfsr);
    field("far", f_far);
    field("hfsr", f_hfsr);
}

/* lines are kept and printed from privileged code only: QEMU refuses
 * semihosting to unprivileged code */
static char out[4096];
static int out_len;

static void end(void)
{
    int i;
    line[pos++] = '\n';
    for (i = 0; i < pos && out_len < (int)sizeof out - 1; i++)
        out[out_len++] = line[i];
    out[out_len] = 0;
}

static void flush(void)
{
    sh_puts(out);
    out_len = 0;
    out[0] = 0;
}

/* ---- the cases --------------------------------------------------------- */

static uint32_t rd8(volatile uint8_t *p) { return *p; }
static uint32_t ldrt(volatile uint32_t *p)
{
    uint32_t v;
    __asm__ volatile("ldrt %0, [%1]" : "=r"(v) : "r"(p) : "memory");
    return v;
}
static uint32_t call(uint32_t addr)
{
    uint32_t r;
    __asm__ volatile("mov r0, #0\n blx %1\n mov %0, r0" : "=r"(r) : "r"(addr) : "r0", "r1", "r2", "r3", "r12", "lr", "memory");
    return r;
}

static void region(int n, uint32_t base, uint32_t rasr)
{
    MPU_RNR = (uint32_t)n;
    MPU_RBAR = base;
    MPU_RASR = rasr;
}

static void mpu_setup(void)
{
    region(0, 0x20000000u, RASR(3, 16));                /* SRAM: read-write for all */
    region(1, 0x00000000u, RASR(6, 18));                /* flash: read-only for all */
    region(2, (uint32_t)buf_ro, RASR(5, 5));             /* privileged read-only */
    region(3, (uint32_t)buf_na, RASR(0, 5));             /* no access */
    region(4, (uint32_t)buf_sub, RASR(0, 8) | (1u << (8 + 2)));   /* subregion 2 off */
    region(5, (uint32_t)code_buf, RASR(3, 5) | XN);
    region(6, (uint32_t)buf_pr, RASR(1, 5));             /* privileged read-write only */
    region(7, 0, 0);
    MPU_CTRL = 5;                                        /* ENABLE, PRIVDEFENA */
    __asm__ volatile("dsb\n isb");
}

static void case_registers(void)
{
    begin("mpu-registers");
    field("type", MPU_TYPE);
    MPU_RNR = 3;
    field("rbar3", MPU_RBAR);
    field("rasr3", MPU_RASR);
    MPU_RBAR = (uint32_t)buf_pr | 0x10u | 6u;            /* VALID: region 6 */
    field("rnr", MPU_RNR);
    field("ctrl", MPU_CTRL);
    end();
}

static void case_privileged(void)
{
    begin("priv-write-ro");
    buf_ro[1] = 7;
    faults();
    end();
    begin("priv-read-ro");
    field("v", rd8(&buf_ro[2]));
    faults();
    end();
    begin("priv-read-na");
    (void)rd8(&buf_na[4]);
    faults();
    end();
    begin("subregion-on");
    (void)rd8(&buf_sub[8]);
    faults();
    end();
    begin("subregion-off");
    (void)rd8(&buf_sub[64 + 8]);
    faults();
    end();
    begin("overlap");
    region(7, (uint32_t)buf_na, RASR(3, 5));
    (void)rd8(&buf_na[4]);
    region(7, 0, 0);
    faults();
    end();
    begin("ldrt-priv-only");
    field("v", ldrt(&buf_pr[1]));
    faults();
    end();
    begin("ldr-priv-only");
    buf_pr[1] = 0x1234u;
    field("v", buf_pr[1]);
    faults();
    end();
    begin("faultmask-no-hfnmiena");
    __asm__ volatile("cpsid f");
    (void)rd8(&buf_na[4]);
    __asm__ volatile("cpsie f");
    faults();
    end();
    begin("no-privdefena");
    MPU_CTRL = 1;
    (void)REG(0x20010000u);
    MPU_CTRL = 5;
    faults();
    end();
    begin("privdefena");
    (void)REG(0x20010000u);
    faults();
    end();
    begin("mpu-off");
    MPU_CTRL = 0;
    (void)rd8(&buf_na[4]);
    MPU_CTRL = 5;
    faults();
    end();
}

static void case_execute(void)
{
    code_buf[0] = 0x202a;   /* movs r0, #42 */
    code_buf[1] = 0x4770;   /* bx lr */
    f_mode = 1;
    begin("exec-xn");
    field("r", call((uint32_t)code_buf | 1u));
    faults();
    end();
    region(5, (uint32_t)code_buf, RASR(3, 5));
    begin("exec-ram");
    field("r", call((uint32_t)code_buf | 1u));
    faults();
    end();
    begin("exec-peripheral");
    field("r", call(0x40000001u));
    faults();
    end();
    begin("exec-system");
    field("r", call(0xE0000001u));
    faults();
    end();
    MPU_CTRL = 0;
    begin("exec-peripheral-mpu-off");
    field("r", call(0x40000001u));
    faults();
    end();
    MPU_CTRL = 5;
    f_mode = 0;
}

static void case_unprivileged(void)
{
    uint32_t c;
    __asm__ volatile("mrs %0, control\n orr %0, %0, #1\n msr control, %0\n isb" : "=&r"(c));
    begin("unpriv-read-ram");
    (void)rd8(&buf_sub[64 + 8]);
    faults();
    end();
    begin("unpriv-read-ro");
    (void)rd8(&buf_ro[2]);
    faults();
    end();
    begin("unpriv-read-priv-only");
    (void)buf_pr[1];
    faults();
    end();
    begin("unpriv-read-scs");
    (void)REG(0xE000ED00u);
    faults();
    end();
    __asm__ volatile("svc 0");
    begin("svc-back");
    __asm__ volatile("mrs %0, control" : "=r"(c));
    field("control", c);
    field("svc", svc_count);
    end();
}

static void case_stacking(void)
{
    region(7, (uint32_t)bad_stack, RASR(0, 6));
    __asm__ volatile("dsb\n isb");
    /* SVC with the process stack in region 7: MSTKERR, the SVC pending */
    f_mode = 2;
    begin("stack-fault");
    svc_count = 0;
    __asm__ volatile("movw r0, #:lower16:stk_resume\n movt r0, #:upper16:stk_resume\n orr r0, r0, #1\n movw r1, #:lower16:f_resume\n movt r1, #:upper16:f_resume\n str r0, [r1]\n"
                     "mov r0, %0\n msr psp, r0\n mov r0, #2\n msr control, r0\n isb\n"
                     "svc 1\n"
                     "stk_resume:\n mov r0, #0\n msr control, r0\n isb\n"
                     : : "r"((uint32_t)bad_stack + 64) : "r0", "r1", "r2", "r3", "r12", "lr", "memory");
    faults();
    field("lr", f_lr);
    field("svc", svc_count);
    end();
    /* a return to a thread whose stack is in region 7: MUNSTKERR */
    begin("unstack-fault");
    bad_top = (uint32_t)bad_stack + 32;
    pendsv_mode = 1;
    __asm__ volatile("movw r0, #:lower16:unstk_resume\n movt r0, #:upper16:unstk_resume\n orr r0, r0, #1\n movw r1, #:lower16:f_resume\n movt r1, #:upper16:f_resume\n str r0, [r1]\n"
                     "movw r1, #:lower16:saved_msp\n movt r1, #:upper16:saved_msp\n mrs r0, msp\n str r0, [r1]\n"
                     "movw r0, #0xed04\n movt r0, #0xe000\n mov r1, #0x10000000\n str r1, [r0]\n dsb\n isb\n"
                     "nop\n"
                     "unstk_resume:\n movw r1, #:lower16:saved_msp\n movt r1, #:upper16:saved_msp\n ldr r0, [r1]\n msr msp, r0\n"
                     "mov r0, #0\n msr control, r0\n isb\n"
                     : : : "r0", "r1", "r2", "r3", "r12", "lr", "memory");
    pendsv_mode = 0;
    faults();
    field("lr", f_lr);
    end();
    region(7, 0, 0);
    f_mode = 0;
}

static void case_fp(uint32_t lazy)
{
    uint32_t a = 0x3FC00000u, b = 0x40200000u, s0, fpscr, i;
    volatile uint32_t *below;
    FPCCR = lazy ? 0xC0000000u : 0x80000000u;
    FPDSCR = 0;
    __asm__ volatile("vmov s0, %0\n vmov s1, %1\n" : : "r"(a), "r"(b) : "s0", "s1");
    FPDSCR = 1u << 25;                                   /* DN for the handler's new context */
    /* the frame's FP area below the stack, marked */
    __asm__ volatile("mrs %0, msp" : "=r"(below));
    for (i = 1; i <= 40; i++)
        below[-(int)i] = 0xA5A5A5A5u;
    ICSR = 1u << 28;                                     /* PENDSVSET */
    __asm__ volatile("dsb\n isb");
    __asm__ volatile("vmov %0, s0\n vmrs %1, fpscr" : "=r"(s0), "=r"(fpscr));
    begin(lazy ? "fp-lazy" : "fp-eager");
    field("fpccr-in", p_fpccr1);
    field("fpcar-off", p_fpcar - p_sp);
    field("lr", p_lr);
    field("slot-before", p_slot1);
    field("slot-after", p_slot2);
    field("fpccr-after", p_fpccr2);
    field("fpscr-handler", p_fpscr & 0x07C00000u);
    field("s0-thread", s0);
    field("fpscr-thread", fpscr & 0x07C00000u);
    end();
}

int main(void)
{
    SHCSR |= 7u << 16;                                   /* MemManage, BusFault, UsageFault */
    CPACR |= 0xFu << 20;                                 /* CP10, CP11: full access */
    __asm__ volatile("dsb\n isb");
    begin("reset");
    field("fpccr", FPCCR);
    field("mpu-ctrl", MPU_CTRL);
    end();
    mpu_setup();
    case_registers();
    case_privileged();
    flush();
    case_execute();
    case_unprivileged();
    flush();
    case_stacking();
    MPU_CTRL = 0;
    case_fp(1);
    case_fp(0);
    flush();
    sh_puts("done\n");
    sh_exit(0);
    return 0;
}
