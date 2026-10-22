/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_esp32c3.c -- the ESP32-C3's ESP-RV core and its interrupt matrix
 * (ESP32-C3 TRM v1.4, chapters 1 and 8): hand-assembled RV32 programs at
 * the reset pc, run on the machine the catalog's description builds; then
 * its boot: Espressif's own ROM from the catalog booting the in-package
 * flash (an ESP image through SPI1, the SHA accelerator, the cache and
 * MMU), the flash's commands, the PC logging. */
#include "harness.h"
#include "util/wcrypto.h"
#include "mcu/build.h"
#include "mcu/esp32c3/esp32c3.h"
#include "mcu/riscv/riscv.h"
#include "util/json.h"
#include "util/sha256.h"

#define BASE 0x40000000u    /* the reset pc */
#define VEC (BASE + 0x100u) /* mtvec: 256-byte aligned */
#define INTMTX 0x600C2000u
#define UART0_MAP (INTMTX + 4u * 21u)
#define CPU_INT_ENABLE (INTMTX + 0x104u)
#define CPU_INT_TYPE (INTMTX + 0x108u)
#define CPU_INT_CLEAR (INTMTX + 0x10Cu)
#define CPU_INT_EIP (INTMTX + 0x110u)
#define CPU_INT_PRI(n) (INTMTX + 0x114u + 4u * (n))
#define CPU_INT_THRESH (INTMTX + 0x194u)
#define INTR_STATUS_0 (INTMTX + 0xF8u)

/* ---- encodings ------------------------------------------------------------ */

static u32 i_type(u32 op, u32 f3, u32 rd, u32 rs1, s32 imm)
{
    return ((u32)imm & 0xfffu) << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op;
}
static u32 addi(u32 rd, u32 rs1, s32 imm) { return i_type(0x13, 0, rd, rs1, imm); }
static u32 lui(u32 rd, u32 imm20) { return imm20 << 12 | rd << 7 | 0x37; }
static u32 lw(u32 rd, u32 rs1, s32 imm) { return i_type(0x03, 2, rd, rs1, imm); }
static u32 csrrw(u32 rd, u32 csr, u32 rs1) { return csr << 20 | rs1 << 15 | 1u << 12 | rd << 7 | 0x73; }
static u32 csrrs(u32 rd, u32 csr, u32 rs1) { return csr << 20 | rs1 << 15 | 2u << 12 | rd << 7 | 0x73; }
static u32 csrrsi(u32 rd, u32 csr, u32 z) { return csr << 20 | z << 15 | 6u << 12 | rd << 7 | 0x73; }
static u32 jal_back(int words) /* jal x0, -4 * words */
{
    u32 off = (u32)(-4 * words);
    return (off >> 20 & 1u) << 31 | (off >> 1 & 0x3ffu) << 21 | (off >> 11 & 1u) << 20 |
           (off >> 12 & 0xffu) << 12 | 0x6fu;
}
#define ECALL 0x00000073u
#define MRET 0x30200073u
#define JAL_SELF 0x0000006fu
#define AMOADD_W 0x00b5252fu /* amoadd.w a0, a1, (a0) */

/* ---- the machine ------------------------------------------------------------ */

typedef struct c3 {
    esk_alloc a;
    esk_json *mcu, *regs;
    struct esk_mach m;
} c3;

static unsigned char *slurp(const char *rel, size_t *len)
{
    char path[1024];
    FILE *f;
    unsigned char *buf;
    long n;
    snprintf(path, sizeof path, "%s/%s", EMBSIM_SOURCE_DIR, rel);
    f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc(n > 0 ? (size_t)n : 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static esk_json *load_json(esk_alloc *a, const char *rel)
{
    size_t len = 0;
    unsigned char *text = slurp(rel, &len);
    esk_json *j;
    int line = 0;
    const char *err = NULL;
    if (!text)
        return NULL;
    j = esk_json_parse(a, (const char *)text, len, &line, &err);
    free(text);
    return j;
}

/* an ESP32-C3 out of reset with `prog` at the reset pc and `vec` at mtvec's
 * base (0x40000100): its word 0 is the exception entry, word n interrupt n's */
static int c3_open(c3 *f, const u32 *prog, size_t n, const u32 *vec, size_t nv)
{
    char why[200];
    es_status st;
    size_t i;
    memset(f, 0, sizeof *f);
    esk_alloc_init(&f->a, NULL);
    f->mcu = load_json(&f->a, "catalog/mcus/espressif/esp32c3/mcu.json");
    f->regs = load_json(&f->a, "catalog/mcus/espressif/esp32c3/registers.json");
    CHECK(f->mcu != NULL && f->regs != NULL);
    esk_mach_init(&f->m, &f->a);
    st = esk_mach_build(&f->m, f->mcu, f->regs, 40000000u, why, sizeof why);
    if (st != ES_OK) {
        FAIL(why);
        return 0;
    }
    for (i = 0; i < n; i++)
        CHECK_EQ(esk_bus_debug_write(&f->m.bus, BASE + (u32)(4 * i), 4, prog[i]), 0);
    for (i = 0; i < nv; i++)
        CHECK_EQ(esk_bus_debug_write(&f->m.bus, VEC + (u32)(4 * i), 4, vec[i]), 0);
    esk_mach_reset(&f->m);
    return 1;
}

static void c3_close(c3 *f)
{
    esk_mach_free(&f->m);
    esk_json_free(&f->a, f->mcu);
    esk_json_free(&f->a, f->regs);
    CHECK_EQ(f->a.live_bytes, 0);
}

static u32 x(c3 *f, int n)
{
    u8 b[8] = { 0 };
    f->m.cpu->ops->reg_read(f->m.cpu, n, b);
    return (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24;
}

static u32 rd(c3 *f, u32 a)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_read(&f->m.bus, a, 4, &v), 0);
    return v;
}

static void wr(c3 *f, u32 a, u32 v)
{
    CHECK_EQ(esk_bus_write(&f->m.bus, a, 4, v), 0);
}

/* the vector table: the exception entry saves mcause and mepc in x20 and
 * x21; interrupt 5's in x22 and x23, then returns */
#define NVEC5 8
static const u32 *vec5(void)
{
    static u32 v[NVEC5];
    v[0] = csrrs(20, 0x342, 0);
    v[1] = csrrs(21, 0x341, 0);
    v[2] = JAL_SELF;
    v[5] = csrrs(22, 0x342, 0);
    v[6] = csrrs(23, 0x341, 0);
    v[7] = MRET;
    return v;
}

/* the prologue: mtvec at VEC */
#define MTVEC_AT_VEC lui(1, BASE >> 12), addi(1, 1, 0x100), csrrw(0, 0x305, 1)

/* ---- the core ------------------------------------------------------------------ */

static void test_csrs(void)
{
    /* misa: RV32 C I M U, no A; the IDs; mtvec keeps only its 256-byte
     * base, vectored; then mie, not the ESP-RV's, is an illegal instruction */
    const u32 prog[] = { csrrs(10, 0x301, 0), csrrs(11, 0xf11, 0), csrrs(12, 0xf12, 0),
                         MTVEC_AT_VEC, addi(2, 1, 0x7f), csrrw(0, 0x305, 2), csrrs(13, 0x305, 0),
                         csrrw(0, 0x305, 1), csrrs(14, 0x304, 0), JAL_SELF };
    c3 f;
    if (!c3_open(&f, prog, sizeof prog / sizeof prog[0], vec5(), 3))
        return;
    CHECK_STR(f.m.cpu->ops->name, "riscv");
    CHECK_EQ(esk_mach_core_hz(&f.m), 20000000u); /* XTAL_CLK / 2 (TRM 6.2.4.1) */
    esk_mach_run(&f.m, 0xffffff, 13);
    CHECK_EQ(x(&f, 10), 0x40101104u);
    CHECK_EQ(x(&f, 11), 0x612u);
    CHECK_EQ(x(&f, 12), 0x80000001u);
    CHECK_EQ(x(&f, 13), VEC | 1u);
    CHECK_EQ(x(&f, 20), 2u);          /* illegal instruction */
    CHECK_EQ(x(&f, 21), BASE + 40u);  /* the csrrs of mie */
    CHECK_EQ(x(&f, 32), VEC + 8u);    /* at the exception entry */
    c3_close(&f);
}

static void test_no_atomics(void)
{
    const u32 prog[] = { MTVEC_AT_VEC, lui(10, 0x3fc80), AMOADD_W, JAL_SELF };
    c3 f;
    if (!c3_open(&f, prog, sizeof prog / sizeof prog[0], vec5(), 3))
        return;
    esk_mach_run(&f.m, 0xffffff, 7);
    CHECK_EQ(x(&f, 20), 2u); /* the A extension is not the ESP32-C3's */
    CHECK_EQ(x(&f, 21), BASE + 16u);
    c3_close(&f);
}

static void test_counter(void)
{
    /* mpccr counts instructions (mpcer.INST) while enabled, then cycles */
    const u32 prog[] = { lui(8, 0x3fc80), addi(5, 0, 2), csrrw(0, 0x7e0, 5), csrrw(0, 0x7e2, 0),
                         addi(6, 0, 1), addi(6, 6, 1), addi(6, 6, 1), csrrs(10, 0x7e2, 0),
                         addi(5, 0, 1), csrrw(0, 0x7e0, 5), csrrw(0, 0x7e2, 0), lw(7, 8, 0),
                         csrrs(11, 0x7e2, 0), csrrs(12, 0x7e1, 0), JAL_SELF };
    c3 f;
    if (!c3_open(&f, prog, sizeof prog / sizeof prog[0], NULL, 0))
        return;
    esk_mach_run(&f.m, 0xffffff, 14);
    CHECK_EQ(x(&f, 10), 4u); /* the write of mpccr itself, then three addi */
    CHECK_EQ(x(&f, 11), 3u); /* cycles: the write (1) and a load (2) */
    CHECK_EQ(x(&f, 12), 3u); /* COUNT_SAT and COUNT_EN out of reset */
    c3_close(&f);
}

static void test_trigger(void)
{
    /* trigger 0 on the execution of BASE + 36 in machine mode, with
     * tcontrol.MTE: a breakpoint exception before it runs, its hit bit set,
     * and MTE pushed into MPTE */
    const u32 prog[] = { MTVEC_AT_VEC, lui(5, BASE >> 12), addi(5, 5, 36), csrrw(0, 0x7a2, 5),
                         addi(6, 0, 0x44), csrrw(0, 0x7a1, 6), csrrsi(0, 0x7a5, 8),
                         addi(7, 0, 1), addi(7, 7, 1), JAL_SELF };
    const u32 vec[] = { csrrs(20, 0x342, 0), csrrs(21, 0x341, 0), csrrs(24, 0x7a1, 0), csrrs(25, 0x7a5, 0),
                        JAL_SELF };
    c3 f;
    if (!c3_open(&f, prog, sizeof prog / sizeof prog[0], vec, 5))
        return;
    esk_mach_run(&f.m, 0xffffff, 14);
    CHECK_EQ(x(&f, 20), 3u);              /* breakpoint */
    CHECK_EQ(x(&f, 21), BASE + 36u);
    CHECK_EQ(x(&f, 7), 0u);               /* not run */
    CHECK_EQ(x(&f, 24) & 0x00100000u, 0x00100000u); /* hit */
    CHECK_EQ(x(&f, 24) >> 28, 2u);        /* match type */
    CHECK_EQ(x(&f, 25), 0x80u);           /* MPTE 1, MTE 0 */
    c3_close(&f);
}

/* an mret to user mode at BASE + 0x80, which holds an ecall. Entry 0
 * covers BASE to BASE + 0x200 for reads only (TOR); entry 1, NAPOT over
 * 0x40000000-0x7FFFFFFF (1 GB, the most it may cover), grants RWX. With
 * the standard's static priority entry 0 would refuse the fetch; the
 * ESP32-C3's PMP lets any matching entry grant it. Without entry 1, the
 * fetch faults. */
static u32 user_cause(int second)
{
    u32 prog[40];
    const u32 head[] = { MTVEC_AT_VEC, lui(5, BASE >> 12), addi(5, 5, 0x80), csrrw(0, 0x341, 5),
                         lui(6, 0x10000), addi(6, 6, 0x80), csrrw(0, 0x3b0, 6),
                         lui(7, 0x18000), addi(7, 7, -1), csrrw(0, 0x3b1, 7),
                         lui(8, 0x2), addi(8, 8, -0xf7), csrrw(0, 0x3a0, 8), MRET };
    c3 f;
    size_t i;
    u32 cause;
    for (i = 0; i < 40; i++)
        prog[i] = JAL_SELF;
    for (i = 0; i < sizeof head / sizeof head[0]; i++)
        prog[i] = head[i];
    if (!second)
        prog[11] = addi(8, 0, 0x09); /* entry 0 alone */
    prog[32] = ECALL;
    if (!c3_open(&f, prog, 40, vec5(), 3))
        return 0;
    esk_mach_run(&f.m, 0xffffff, 20);
    cause = x(&f, 20);
    c3_close(&f);
    return cause;
}

static void test_pmp_no_priority(void)
{
    CHECK_EQ(user_cause(1), 8u); /* the ecall, from user mode */
    CHECK_EQ(user_cause(0), 1u); /* an instruction access fault */
}

/* ---- the interrupt matrix and controller ---------------------------------------- */

/* UART0's source (21) on CPU interrupt 5 at priority 3 above threshold 1;
 * the program counts in x5 with MIE set */
static int int_open(c3 *f, u32 type)
{
    const u32 prog[] = { MTVEC_AT_VEC, csrrsi(0, 0x300, 8), addi(5, 5, 1), jal_back(1) };
    if (!c3_open(f, prog, sizeof prog / sizeof prog[0], vec5(), NVEC5))
        return 0;
    wr(f, UART0_MAP, 5);
    wr(f, CPU_INT_PRI(5), 3);
    wr(f, CPU_INT_THRESH, 1);
    wr(f, CPU_INT_TYPE, type);
    wr(f, CPU_INT_ENABLE, 1u << 5);
    esk_mach_run(&f->m, 0xffffff, 10);
    return 1;
}

static void test_level_interrupt(void)
{
    c3 f;
    int i;
    u32 before;
    if (!int_open(&f, 0))
        return;
    CHECK_EQ(rd(&f, CPU_INT_EIP), 0u);
    esk_esp32c3_source(&f.m, 21, 1);
    CHECK_EQ(rd(&f, INTR_STATUS_0), 1u << 21);
    CHECK_EQ(rd(&f, CPU_INT_EIP), 1u << 5);
    /* taken 4 cycles after it shows: a few instructions of the loop first */
    before = x(&f, 5);
    for (i = 0; i < 8 && x(&f, 32) < VEC; i++)
        esk_mach_run(&f.m, 0xffffff, 1);
    CHECK(x(&f, 32) >= VEC);
    CHECK(x(&f, 5) > before);
    esk_mach_run(&f.m, 0xffffff, 2);
    CHECK_EQ(x(&f, 22), 0x80000005u);       /* interrupt 5 */
    CHECK(x(&f, 23) >= BASE + 16u && x(&f, 23) <= BASE + 20u);
    /* a level is cleared at its source; MRET clears MPIE */
    esk_esp32c3_source(&f.m, 21, 0);
    CHECK_EQ(rd(&f, CPU_INT_EIP), 0u);
    esk_mach_run(&f.m, 0xffffff, 1);
    {
        u8 b[8] = { 0 };
        f.m.cpu->ops->reg_read(f.m.cpu, 66 + 0x300, b);
        CHECK_EQ(b[0] & 0x88u, 0x08u);      /* MIE back on, MPIE cleared */
    }
    c3_close(&f);
}

static void test_edge_interrupt(void)
{
    c3 f;
    if (!int_open(&f, 1u << 5))
        return;
    /* a pulse is latched */
    esk_esp32c3_source(&f.m, 21, 1);
    esk_esp32c3_source(&f.m, 21, 0);
    CHECK_EQ(rd(&f, CPU_INT_EIP), 1u << 5);
    /* an unclaimed, enabled edge is not cleared by the toggle */
    wr(&f, CPU_INT_CLEAR, 1u << 5);
    wr(&f, CPU_INT_CLEAR, 0);
    CHECK_EQ(rd(&f, CPU_INT_EIP), 1u << 5);
    esk_mach_run(&f.m, 0xffffff, 8);
    CHECK_EQ(x(&f, 22), 0x80000005u);
    /* claimed now: the toggle clears it */
    wr(&f, CPU_INT_CLEAR, 1u << 5);
    wr(&f, CPU_INT_CLEAR, 0);
    CHECK_EQ(rd(&f, CPU_INT_EIP), 0u);
    c3_close(&f);
}

static void test_threshold(void)
{
    c3 f;
    if (!int_open(&f, 0))
        return;
    wr(&f, CPU_INT_THRESH, 4);
    esk_esp32c3_source(&f.m, 21, 1);
    CHECK_EQ(rd(&f, CPU_INT_EIP), 0u); /* priority 3 below the threshold */
    esk_mach_run(&f.m, 0xffffff, 20);
    CHECK_EQ(x(&f, 22), 0u);
    wr(&f, CPU_INT_THRESH, 3);
    CHECK_EQ(rd(&f, CPU_INT_EIP), 1u << 5);
    wr(&f, CPU_INT_PRI(5), 0);         /* priority 0 never interrupts */
    CHECK_EQ(rd(&f, CPU_INT_EIP), 0u);
    wr(&f, CPU_INT_PRI(5), 3);
    esk_mach_run(&f.m, 0xffffff, 10);
    CHECK_EQ(x(&f, 22), 0x80000005u);
    c3_close(&f);
}

static void test_wfi_wakes(void)
{
    /* WFI with MIE clear sleeps until an interrupt is pending, then runs on */
    const u32 prog[] = { MTVEC_AT_VEC, 0x10500073u, addi(5, 0, 7), JAL_SELF };
    c3 f;
    if (!c3_open(&f, prog, sizeof prog / sizeof prog[0], vec5(), NVEC5))
        return;
    wr(&f, UART0_MAP, 5);
    wr(&f, CPU_INT_PRI(5), 3);
    wr(&f, CPU_INT_ENABLE, 1u << 5);
    esk_mach_run(&f.m, 0xffffff, 4);
    CHECK(f.m.asleep);
    esk_esp32c3_source(&f.m, 21, 1);
    CHECK(!f.m.asleep);
    esk_mach_run(&f.m, 0xffffff, 1);
    CHECK_EQ(x(&f, 5), 7u);
    c3_close(&f);
}

/* ---- the boot ------------------------------------------------------------------ */

#define SPI1          0x60002000u
#define SPI_CMD       (SPI1 + 0x00u)
#define SPI_ADDR      (SPI1 + 0x04u)
#define SPI_USER      (SPI1 + 0x18u)
#define SPI_USER1     (SPI1 + 0x1Cu)
#define SPI_USER2     (SPI1 + 0x20u)
#define SPI_MISO_DLEN (SPI1 + 0x28u)
#define SPI_RD_STATUS (SPI1 + 0x2Cu)
#define SPI_W0        (SPI1 + 0x58u)
#define EXTMEM        0x600C4000u
#define MMU           0x600C5000u
#define SHA           0x6003B000u
#define ASSIST        0x600CE000u
#define SYSTEM          0x600C0000u
#define CPU_PERI_CLK_EN (SYSTEM + 0x00u)
#define CPU_PERI_RST_EN (SYSTEM + 0x04u)
#define PERIP_CLK_EN1   (SYSTEM + 0x14u)
#define PERIP_RST_EN0   (SYSTEM + 0x18u)
#define PERIP_RST_EN1   (SYSTEM + 0x1Cu)

static char uart_out[65536];
static size_t uart_n;

static void uart_tx(void *ctx, const char *unit, int c, u32 bit_cycles)
{
    (void)ctx;
    (void)bit_cycles;
    if (strcmp(unit, "UART0") == 0 && uart_n + 1 < sizeof uart_out) {
        uart_out[uart_n++] = (char)c;
        uart_out[uart_n] = 0;
    }
}

/* the ESP32-C3 with its ROM from the catalog and `image` at flash 0 */
static int c3_rom(c3 *f, const u8 *image, size_t len)
{
    size_t rlen = 0;
    unsigned char *rom = slurp("catalog/mcus/espressif/esp32c3/rom.bin", &rlen);
    struct esk_store *fl;
    if (!c3_open(f, NULL, 0, NULL, 0))
        return 0;
    CHECK(rom != NULL);
    if (!rom)
        return 0;
    CHECK_EQ(esk_mach_factory(&f->m, 0x40000000u, rom, rlen), ES_OK);
    free(rom);
    fl = esk_mach_store(&f->m, "flash");
    CHECK(fl != NULL && fl->size == 4u * 1024u * 1024u);
    if (!fl)
        return 0;
    if (image)
        memcpy(fl->mem, image, len);
    uart_n = 0;
    uart_out[0] = 0;
    f->m.host.uart_tx = uart_tx;
    esk_mach_reset(&f->m);
    return 1;
}

/* runs `cycles`, doing the resets the part asks for as the runtime does */
static void c3_run(c3 *f, u64 cycles)
{
    /* a reset starts the machine's cycle count again */
    while (cycles && f->m.state == ESK_MACH_RUN) {
        u64 ran = esk_mach_run(&f->m, cycles, 0);
        cycles = ran >= cycles ? 0 : cycles - ran; /* the last instruction may overshoot */
        if (f->m.reset_req) {
            f->m.reset_req = 0;
            f->m.yield = 0;
            f->m.asleep = 0;
            if (f->m.reset_cpu_only) {
                f->m.reset_cpu_only = 0;
                f->m.cpu->ops->reset(f->m.cpu);
            } else {
                int cause = f->m.reset_cause, r;
                for (r = 0; r < f->m.bus.nrg; r++)
                    if (!f->m.bus.rg[r].mirror && f->m.bus.rg[r].kind != ESK_MEM_RAM)
                        esk_bus_erase(&f->m.bus.rg[r]);
                esk_mach_reset(&f->m);
                f->m.reset_cause = cause;
            }
        }
    }
}

/* an ESP image as esptool's elf2image writes it for this part: the header
 * (DIO, 40 MHz, 4 MB; the extended header's chip ID 5, hash appended), one
 * segment at `load`, the padding and checksum, the SHA-256 of all before */
static size_t esp_image(u8 *out, u32 load, const u8 *code, size_t n)
{
    static const u8 ext[16] = { 0xee, 0, 0, 0, 5, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 0, 0, 1 };
    esk_sha256 h;
    size_t k = 0, i;
    u8 cs = 0xef;
    out[k++] = 0xe9;
    out[k++] = 1;
    out[k++] = 2;
    out[k++] = 0x20;
    for (i = 0; i < 4; i++)
        out[k++] = (u8)(load >> (8 * i));
    memcpy(out + k, ext, sizeof ext);
    k += sizeof ext;
    for (i = 0; i < 4; i++)
        out[k++] = (u8)(load >> (8 * i));
    for (i = 0; i < 4; i++)
        out[k++] = (u8)(n >> (8 * i));
    memcpy(out + k, code, n);
    k += n;
    for (i = 0; i < n; i++)
        cs ^= code[i];
    while (k % 16 != 15)
        out[k++] = 0;
    out[k++] = cs;
    esk_sha256_init(&h);
    esk_sha256_update(&h, out, k);
    esk_sha256_final(&h, out + k);
    return k + 32;
}

/* the app (rv32imc, as assembled by riscv32-esp-elf-as): waits for UART0's
 * TX FIFO to empty, turns the cache and both buses on, maps flash page 0 at
 * the data bus's page 1 (MMU entry 1), prints 'M' if it reads the image's
 * magic there ('X' if not), then "OK\n", then loops */
static const u8 app[] = {
    0x37, 0x05, 0x00, 0x60, 0x83, 0x22, 0xc5, 0x01, 0x93, 0xd2, 0x02, 0x01, 0x93, 0xf2, 0xf2, 0x3f,
    0xe3, 0x9a, 0x02, 0xfe, 0xb7, 0x45, 0x0c, 0x60, 0x23, 0xa2, 0x05, 0x00, 0x85, 0x42, 0x23, 0xa0,
    0x55, 0x00, 0xb7, 0x55, 0x0c, 0x60, 0x23, 0xa2, 0x05, 0x00, 0x37, 0x06, 0x01, 0x3c, 0x03, 0x43,
    0x06, 0x00, 0x93, 0x03, 0x90, 0x0e, 0x93, 0x02, 0xd0, 0x04, 0x63, 0x04, 0x73, 0x00, 0x93, 0x02,
    0x80, 0x05, 0x23, 0x20, 0x55, 0x00, 0x93, 0x02, 0xf0, 0x04, 0x23, 0x20, 0x55, 0x00, 0x93, 0x02,
    0xb0, 0x04, 0x23, 0x20, 0x55, 0x00, 0xa9, 0x42, 0x23, 0x20, 0x55, 0x00, 0x01, 0xa0, 0x01, 0x00,
};

static void test_rom_blank_flash(void)
{
    /* the boot ROM's banner, the strapping (GPIO9's pull-up: SPI boot),
     * then the erased flash's header */
    c3 f;
    if (!c3_rom(&f, NULL, 0))
        return;
    c3_run(&f, 4000000);
    CHECK(strstr(uart_out, "ESP-ROM:esp32c3-api1-20210207") == uart_out);
    CHECK(strstr(uart_out, "Build:Feb  7 2021") != NULL);
    CHECK(strstr(uart_out, "rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)") != NULL);
    CHECK(strstr(uart_out, "invalid header: 0xffffffff") != NULL);
    /* TIMG0's flash boot protection: its stage 0, 26 000 000 APB_CLK
     * cycles at 20 MHz, then a core reset; the ROM again, with the PC */
    CHECK(strstr(uart_out, "rst:0x7") == NULL);
    c3_run(&f, 26000000u - 4000000u + 2000000u);
    CHECK(strstr(uart_out, "rst:0x7 (TG0WDT_SYS_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)") != NULL);
    /* the PC in the ROM's character output (uart_tx_one_char and the UART
     * and USB routines it calls), where it prints the headers */
    CHECK(strstr(uart_out, "Saved PC:0x4004c") != NULL);
    c3_close(&f);
}

static void test_rom_boots_image(void)
{
    /* the ROM reads the image through SPI1, checks its checksum and SHA-256
     * on the accelerator, copies the segment to SRAM and jumps to it; the
     * app reads the flash through the cache */
    static u8 img[256];
    size_t n = esp_image(img, 0x403CE000u, app, sizeof app);
    c3 f;
    if (!c3_rom(&f, img, n))
        return;
    c3_run(&f, 4000000);
    CHECK(strstr(uart_out, "SPIWP:0xee") != NULL);
    CHECK(strstr(uart_out, "mode:DIO, clock div:2") != NULL);
    CHECK(strstr(uart_out, "load:0x403ce000,len:0x60") != NULL);
    CHECK(strstr(uart_out, "SHA-256 comparison failed") == NULL);
    CHECK(strstr(uart_out, "entry 0x403ce000") != NULL);
    CHECK(strstr(uart_out, "MOK\n") != NULL);
    CHECK_EQ(f.m.cpu->ops->pc(f.m.cpu), 0x403CE05Cu);
    /* ROM1's last word: ets_rom_layout_p, to the ROM's ets_rom_layout */
    CHECK_EQ(rd(&f, 0x3FF1FFFCu), 0x3FF1BE3Cu);
    c3_close(&f);
}

static void test_rom_bad_digest(void)
{
    /* a changed byte after the digest was taken: the ROM says so and, with
     * secure boot off, boots it anyway */
    static u8 img[256];
    size_t n = esp_image(img, 0x403CE000u, app, sizeof app);
    c3 f;
    img[n - 1] = (u8)(img[n - 1] ^ 1u);
    if (!c3_rom(&f, img, n))
        return;
    c3_run(&f, 4000000);
    CHECK(strstr(uart_out, "SHA-256 comparison failed:") != NULL);
    CHECK(strstr(uart_out, "Attempting to boot anyway...") != NULL);
    CHECK(strstr(uart_out, "MOK\n") != NULL);
    c3_close(&f);
}

/* SPI1's user command: `cmd`, a 24-bit address, `nin` bytes into W0 */
static void spi_read(c3 *f, u8 cmd, u32 addr, int nin)
{
    wr(f, SPI_USER, 1u << 31 | 1u << 30 | 1u << 28);
    wr(f, SPI_USER1, 23u << 26);
    wr(f, SPI_USER2, 7u << 28 | cmd);
    wr(f, SPI_MISO_DLEN, (u32)(8 * nin - 1));
    wr(f, SPI_ADDR, addr);
    wr(f, SPI_CMD, 1u << 18);
    CHECK_EQ(rd(f, SPI_CMD), 0u);
}

static u32 spi_status(c3 *f)
{
    wr(f, SPI_CMD, 1u << 27); /* RDSR */
    return rd(f, SPI_RD_STATUS) & 0xffu;
}

static void test_flash_commands(void)
{
    /* the XM25QH32C through SPI1: its JEDEC ID; Page Program needs WEL, is
     * busy for tPP and ignores reads meanwhile; bits only clear; a sector
     * erase sets them again after tSE */
    c3 f;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    wr(&f, SPI_CMD, 1u << 28); /* RDID */
    CHECK_EQ(rd(&f, SPI_W0) & 0xffffffu, 0x164020u);
    spi_read(&f, 0x9f, 0, 3);
    CHECK_EQ(rd(&f, SPI_W0) & 0xffffffu, 0x164020u);
    spi_read(&f, 0x03, 0x1000, 4);
    CHECK_EQ(rd(&f, SPI_W0), 0xffffffffu);
    wr(&f, SPI_W0, 0x44332211u);
    wr(&f, SPI_ADDR, 0x1000u | 4u << 24);
    wr(&f, SPI_CMD, 1u << 25); /* PP without WREN: nothing */
    spi_read(&f, 0x03, 0x1000, 4);
    CHECK_EQ(rd(&f, SPI_W0), 0xffffffffu);
    wr(&f, SPI_CMD, 1u << 30); /* WREN */
    CHECK_EQ(spi_status(&f), 0x02u);
    wr(&f, SPI_W0, 0x44332211u);
    wr(&f, SPI_ADDR, 0x1000u | 4u << 24);
    wr(&f, SPI_CMD, 1u << 25);
    CHECK_EQ(spi_status(&f), 0x03u); /* BUSY and WEL */
    spi_read(&f, 0x03, 0x1000, 4);
    CHECK_EQ(rd(&f, SPI_W0), 0xffffffffu);         /* ignored while busy */
    esk_mach_idle(&f.m, esk_mach_core_hz(&f.m) / 2000u + 100u); /* tPP 0.5 ms */
    CHECK_EQ(spi_status(&f), 0x00u);
    spi_read(&f, 0x0b, 0x1000, 4);
    CHECK_EQ(rd(&f, SPI_W0), 0x44332211u);
    wr(&f, SPI_CMD, 1u << 30);
    wr(&f, SPI_W0, 0xf0f0f0f0u);
    wr(&f, SPI_ADDR, 0x1000u | 4u << 24);
    wr(&f, SPI_CMD, 1u << 25);
    esk_mach_idle(&f.m, esk_mach_core_hz(&f.m) / 2000u + 100u);
    spi_read(&f, 0x03, 0x1000, 4);
    CHECK_EQ(rd(&f, SPI_W0), 0x40302010u); /* only 1 to 0 */
    wr(&f, SPI_CMD, 1u << 30);
    wr(&f, SPI_ADDR, 0x1000u);
    wr(&f, SPI_CMD, 1u << 24);            /* SE */
    esk_mach_idle(&f.m, esk_mach_core_hz(&f.m) / 25u); /* 40 ms: tSE is 50 */
    CHECK_EQ(spi_status(&f) & 1u, 1u);
    esk_mach_idle(&f.m, esk_mach_core_hz(&f.m) / 50u);
    CHECK_EQ(spi_status(&f), 0x00u);
    spi_read(&f, 0x03, 0x1000, 4);
    CHECK_EQ(rd(&f, SPI_W0), 0xffffffffu);
    spi_read(&f, 0x90, 0, 2); /* manufacturer and device ID */
    CHECK_EQ(rd(&f, SPI_W0) & 0xffffu, 0x1520u);
    c3_close(&f);
}

static void test_cache_mmu(void)
{
    /* a page maps where its entry says, on both buses; an invalid entry
     * reads zeros; a programmed page shows its new bytes after an
     * invalidate */
    c3 f;
    struct esk_store *fl;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    fl = esk_mach_store(&f.m, "flash");
    memcpy(fl->mem + 0x20000, "ABCD", 4);
    CHECK_EQ(rd(&f, MMU + 4u * 5u), 0x100u); /* invalid out of reset */
    CHECK_EQ(rd(&f, 0x3C050000u), 0u);
    wr(&f, MMU + 4u * 5u, 2u);
    CHECK_EQ(rd(&f, MMU + 4u * 5u), 2u);
    CHECK_EQ(rd(&f, 0x3C050000u), 0x44434241u);
    CHECK_EQ(rd(&f, 0x42050000u), 0x44434241u);
    wr(&f, SPI_CMD, 1u << 30);
    wr(&f, SPI_W0, 0u);
    wr(&f, SPI_ADDR, 0x20000u | 4u << 24);
    wr(&f, SPI_CMD, 1u << 25);
    CHECK_EQ(rd(&f, 0x3C050000u), 0x44434241u); /* the cache's copy */
    wr(&f, EXTMEM + 0x2Cu, 0x3C050000u);        /* ICACHE_SYNC_ADDR */
    wr(&f, EXTMEM + 0x30u, 0x10000u);           /* ICACHE_SYNC_SIZE */
    wr(&f, EXTMEM + 0x28u, 1u);                 /* INVALIDATE_ENA */
    CHECK_EQ(rd(&f, EXTMEM + 0x28u), 2u);       /* SYNC_DONE */
    CHECK_EQ(rd(&f, 0x3C050000u), 0u);
    wr(&f, MMU + 4u * 5u, 0x100u);
    CHECK_EQ(rd(&f, 0x42050000u), 0u);
    c3_close(&f);
}

/* the accelerator's digest of "abc" in MODE `mode` */
static void sha_abc(c3 *f, u32 mode, u8 *out, int nwords)
{
    u8 blk[64];
    int i;
    memset(blk, 0, sizeof blk);
    memcpy(blk, "abc", 3);
    blk[3] = 0x80;
    blk[63] = 24; /* the length in bits, big-endian */
    wr(f, SHA, mode);
    for (i = 0; i < 16; i++)
        wr(f, SHA + 0x80u + 4u * (u32)i,
           (u32)blk[4 * i] | (u32)blk[4 * i + 1] << 8 | (u32)blk[4 * i + 2] << 16 |
               (u32)blk[4 * i + 3] << 24);
    wr(f, SHA + 0x10u, 1u);
    CHECK_EQ(rd(f, SHA + 0x18u), 0u);
    for (i = 0; i < nwords; i++) {
        u32 w = rd(f, SHA + 0x40u + 4u * (u32)i);
        out[4 * i] = (u8)w;
        out[4 * i + 1] = (u8)(w >> 8);
        out[4 * i + 2] = (u8)(w >> 16);
        out[4 * i + 3] = (u8)(w >> 24);
    }
}

static void test_sha(void)
{
    /* FIPS 180-4's "abc" examples, the digest bytes in the registers in
     * memory order */
    static const u8 d1[20] = { 0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                              0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d };
    static const u8 d224[28] = { 0x23, 0x09, 0x7d, 0x22, 0x34, 0x05, 0xd8, 0x22, 0x86, 0x42,
                                0xa4, 0x77, 0xbd, 0xa2, 0x55, 0xb3, 0x2a, 0xad, 0xbc, 0xe4,
                                0xbd, 0xa0, 0xb3, 0xf7, 0xe3, 0x6c, 0x9d, 0xa7 };
    static const u8 d256[32] = { 0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad };
    u8 out[32];
    c3 f;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    CHECK_EQ(rd(&f, SHA + 0x2Cu), 0u); /* its clock is off out of reset */
    wr(&f, PERIP_CLK_EN1, rd(&f, PERIP_CLK_EN1) | 1u << 2);
    wr(&f, PERIP_RST_EN1, rd(&f, PERIP_RST_EN1) & ~(1u << 2));
    CHECK_EQ(rd(&f, SHA + 0x2Cu), 0x20200616u); /* DATE */
    sha_abc(&f, 0, out, 5);
    CHECK(memcmp(out, d1, sizeof d1) == 0);
    sha_abc(&f, 1, out, 7);
    CHECK(memcmp(out, d224, sizeof d224) == 0);
    sha_abc(&f, 2, out, 8);
    CHECK(memcmp(out, d256, sizeof d256) == 0);
    c3_close(&f);
}

static void test_pc_logging(void)
{
    /* with RCD_PDEBUGEN and RCD_RECORDEN, PDEBUGPC follows the PC; a CPU
     * reset clears RCD_EN and leaves it the PC at the reset */
    const u32 prog[] = { addi(5, 0, 1), JAL_SELF };
    c3 f;
    if (!c3_open(&f, prog, 2, NULL, 0))
        return;
    esk_mach_run(&f.m, 0xffff, 4);
    wr(&f, CPU_PERI_CLK_EN, 1u << 6); /* the Debug Assistant's clock, out of reset */
    wr(&f, CPU_PERI_RST_EN, 0u);
    CHECK_EQ(rd(&f, ASSIST + 0x48u), 0u);
    wr(&f, ASSIST + 0x44u, 3u);
    CHECK_EQ(rd(&f, ASSIST + 0x48u), BASE + 4u);
    esk_esp32c3_reset(&f.m, 0x0C); /* RTC_SW_CPU_RST */
    CHECK(f.m.reset_req && f.m.reset_cpu_only);
    CHECK_EQ(rd(&f, ASSIST + 0x44u), 0u);
    CHECK_EQ(rd(&f, ASSIST + 0x48u), BASE + 4u);
    c3_close(&f);
}

/* ---- clocks and timers ------------------------------------------------------- */

#define ST_CONF       (0x60023000u + 0x00u)
#define ST_UNIT0_OP   (0x60023000u + 0x04u)
#define ST_UNIT1_OP   (0x60023000u + 0x08u)
#define ST_UNIT1_LDHI (0x60023000u + 0x14u)
#define ST_UNIT1_LDLO (0x60023000u + 0x18u)
#define ST_T0_HI      (0x60023000u + 0x1Cu)
#define ST_T0_LO      (0x60023000u + 0x20u)
#define ST_T0_CONF    (0x60023000u + 0x34u)
#define ST_T1_CONF    (0x60023000u + 0x38u)
#define ST_VAL0_LO    (0x60023000u + 0x44u)
#define ST_VAL1_HI    (0x60023000u + 0x48u)
#define ST_VAL1_LO    (0x60023000u + 0x4Cu)
#define ST_COMP0_LOAD (0x60023000u + 0x50u)
#define ST_COMP1_LOAD (0x60023000u + 0x54u)
#define ST_UNIT1_LOAD (0x60023000u + 0x60u)
#define ST_INT_ENA    (0x60023000u + 0x64u)
#define ST_INT_RAW    (0x60023000u + 0x68u)
#define ST_INT_CLR    (0x60023000u + 0x6Cu)
#define ST_INT_ST     (0x60023000u + 0x70u)

static u32 st_unit0(c3 *f)
{
    wr(f, ST_UNIT0_OP, 1u << 30);
    CHECK(rd(f, ST_UNIT0_OP) & (1u << 29)); /* VALUE_VALID */
    return rd(f, ST_VAL0_LO);
}

static void test_cpu_clock(void)
{
    /* CPU_CLK: XTAL_CLK / (PRE_DIV_CNT + 1), 20 MHz out of reset; the PLL's
     * 80 or 160 MHz; RC_FAST_CLK; APB_CLK 80 MHz from the PLL */
    c3 f;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    CHECK_EQ(esk_mach_core_hz(&f.m), 20000000u);
    CHECK_EQ(esk_esp32c3_apb_hz(&f.m), 20000000u);
    wr(&f, SYSTEM + 0x58u, 0u); /* SYSCLK_CONF: XTAL_CLK / 1 */
    CHECK_EQ(esk_mach_core_hz(&f.m), 40000000u);
    wr(&f, SYSTEM + 0x08u, 1u << 2 | 1u); /* CPU_PER_CONF: 480 MHz PLL, 160 MHz */
    wr(&f, SYSTEM + 0x58u, 1u << 10);      /* SOC_CLK_SEL: PLL_CLK */
    CHECK_EQ(esk_mach_core_hz(&f.m), 160000000u);
    CHECK_EQ(esk_esp32c3_apb_hz(&f.m), 80000000u);
    wr(&f, SYSTEM + 0x08u, 0u); /* 320 MHz PLL, 80 MHz */
    CHECK_EQ(esk_mach_core_hz(&f.m), 80000000u);
    wr(&f, SYSTEM + 0x58u, 2u << 10 | 1u); /* RC_FAST_CLK / 2 */
    CHECK_EQ(esk_mach_core_hz(&f.m), 8750000u);
    c3_close(&f);
}

static void test_software_interrupt(void)
{
    /* CPU_INTR_FROM_CPU_0 is matrix source 50, at the level written */
    c3 f;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    wr(&f, SYSTEM + 0x28u, 1u);
    CHECK(rd(&f, INTR_STATUS_0 + 4u) & (1u << (50 - 32)));
    wr(&f, SYSTEM + 0x28u, 0u);
    CHECK(!(rd(&f, INTR_STATUS_0 + 4u) & (1u << (50 - 32))));
    c3_close(&f);
}

static void test_systimer(void)
{
    /* UNIT0 counts 16 MHz from reset; a target alarm on COMP0; a period
     * alarm on COMP1 from UNIT1 loaded with a value; the matrix sources */
    const u32 hz = 20000000u;
    u32 a, b;
    c3 f;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    a = st_unit0(&f);
    esk_mach_idle(&f.m, hz / 1000u); /* 1 ms */
    b = st_unit0(&f);
    CHECK_EQ(b - a, 16000u);
    /* COMP0: target 1 ms ahead */
    wr(&f, ST_T0_HI, 0u);
    wr(&f, ST_T0_LO, b + 16000u);
    wr(&f, ST_T0_CONF, 0u);
    wr(&f, ST_COMP0_LOAD, 1u);
    wr(&f, ST_INT_ENA, 1u);
    wr(&f, ST_CONF, rd(&f, ST_CONF) | 1u << 24);
    CHECK_EQ(rd(&f, ST_INT_RAW), 0u);
    CHECK(esk_mach_event_coming(&f.m));
    esk_mach_idle(&f.m, hz / 1000u - 100u);
    CHECK_EQ(rd(&f, ST_INT_RAW), 0u);
    esk_mach_idle(&f.m, 200u);
    CHECK_EQ(rd(&f, ST_INT_RAW), 1u);
    CHECK_EQ(rd(&f, ST_INT_ST), 1u);
    CHECK(rd(&f, INTR_STATUS_0 + 4u) & (1u << (37 - 32)));
    wr(&f, ST_INT_CLR, 1u);
    CHECK_EQ(rd(&f, ST_INT_ST), 0u);
    CHECK(!(rd(&f, INTR_STATUS_0 + 4u) & (1u << (37 - 32))));
    esk_mach_idle(&f.m, hz / 100u);
    CHECK_EQ(rd(&f, ST_INT_RAW), 0u); /* one alarm in target mode */
    /* a target already passed fires at once */
    wr(&f, ST_T0_LO, 5u);
    wr(&f, ST_COMP0_LOAD, 1u);
    CHECK_EQ(rd(&f, ST_INT_RAW), 1u);
    wr(&f, ST_INT_CLR, 1u);
    /* UNIT1 loaded with 2^32 - 8000, counting; COMP1 every 4000 counts */
    wr(&f, ST_UNIT1_LDHI, 0u);
    wr(&f, ST_UNIT1_LDLO, 0xFFFFFFFFu - 7999u);
    wr(&f, ST_UNIT1_LOAD, 1u);
    wr(&f, ST_CONF, rd(&f, ST_CONF) | 1u << 29);
    wr(&f, ST_T1_CONF, 1u << 31 | 4000u);
    wr(&f, ST_COMP1_LOAD, 1u);
    wr(&f, ST_T1_CONF, 1u << 31 | 1u << 30 | 4000u);
    wr(&f, ST_CONF, rd(&f, ST_CONF) | 1u << 23);
    esk_mach_idle(&f.m, hz / 4000u + 10u); /* 250 us: 4000 counts */
    CHECK_EQ(rd(&f, ST_INT_RAW) & 2u, 2u);
    wr(&f, ST_INT_CLR, 2u);
    esk_mach_idle(&f.m, hz / 4000u);
    CHECK_EQ(rd(&f, ST_INT_RAW) & 2u, 2u);
    wr(&f, ST_UNIT1_OP, 1u << 30);
    CHECK_EQ(rd(&f, ST_VAL1_HI), 1u); /* past 2^32 */
    CHECK(rd(&f, ST_VAL1_LO) < 100u);
    c3_close(&f);
}

#define TG0           0x6001F000u
#define TG_T0CONFIG   (TG0 + 0x00u)
#define TG_T0LO       (TG0 + 0x04u)
#define TG_T0UPDATE   (TG0 + 0x0Cu)
#define TG_T0ALARMLO  (TG0 + 0x10u)
#define TG_T0LOADLO   (TG0 + 0x18u)
#define TG_T0LOAD     (TG0 + 0x20u)
#define TG_WDTCONFIG0 (TG0 + 0x48u)
#define TG_WDTCONFIG1 (TG0 + 0x4Cu)
#define TG_WDTCONFIG2 (TG0 + 0x50u)
#define TG_WDTFEED    (TG0 + 0x60u)
#define TG_WPROTECT   (TG0 + 0x64u)
#define TG_CALI       (TG0 + 0x68u)
#define TG_CALI1      (TG0 + 0x6Cu)
#define TG_CALI2      (TG0 + 0x80u)
#define TG_INT_ENA    (TG0 + 0x70u)
#define TG_INT_RAW    (TG0 + 0x74u)
#define TG_INT_CLR    (TG0 + 0x7Cu)

static u32 tg_value(c3 *f)
{
    wr(f, TG_T0UPDATE, 1u);
    CHECK_EQ(rd(f, TG_T0UPDATE), 0u);
    return rd(f, TG_T0LO);
}

static void test_timg_timer(void)
{
    /* T0 on XTAL_CLK / 40: 1 MHz; an alarm 1000 counts on with auto
     * reload to 0, again and again; the matrix source 32 */
    c3 f;
    u32 a;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    wr(&f, TG_WPROTECT, 0x50D83AA1u); /* the flash boot watchdog off */
    wr(&f, TG_WDTCONFIG0, 0u);
    wr(&f, TG_WDTCONFIG0, 1u << 22);
    wr(&f, TG_T0CONFIG, 1u << 30 | 40u << 13 | 1u << 9); /* up, /40, XTAL */
    wr(&f, TG_T0LOADLO, 0u);
    wr(&f, TG_T0LOAD, 1u);
    wr(&f, TG_T0CONFIG, rd(&f, TG_T0CONFIG) | 1u << 31);
    esk_mach_idle(&f.m, 20000u); /* 1 ms at 20 MHz */
    a = tg_value(&f);
    CHECK_EQ(a, 1000u);
    wr(&f, TG_T0ALARMLO, 2000u);
    wr(&f, TG_INT_ENA, 1u);
    wr(&f, TG_T0CONFIG, rd(&f, TG_T0CONFIG) | 1u << 29 | 1u << 10);
    esk_mach_idle(&f.m, 19980u);
    CHECK_EQ(rd(&f, TG_INT_RAW) & 1u, 0u);
    esk_mach_idle(&f.m, 40u);
    CHECK_EQ(rd(&f, TG_INT_RAW) & 1u, 1u);
    CHECK(rd(&f, INTR_STATUS_0 + 4u) & 1u); /* source 32 */
    CHECK_EQ(rd(&f, TG_T0CONFIG) & (1u << 10), 0u); /* ALARM_EN cleared */
    CHECK(tg_value(&f) < 10u);                     /* reloaded */
    wr(&f, TG_INT_CLR, 1u);
    CHECK(!(rd(&f, INTR_STATUS_0 + 4u) & 1u));
    /* an alarm already behind fires when enabled */
    wr(&f, TG_T0ALARMLO, 1u);
    esk_mach_idle(&f.m, 2000u);
    wr(&f, TG_T0CONFIG, (rd(&f, TG_T0CONFIG) & ~(1u << 29)) | 1u << 10);
    CHECK_EQ(rd(&f, TG_INT_RAW) & 1u, 1u);
    c3_close(&f);
}

static void test_timg_wdt(void)
{
    /* the write key; the stages taking effect at WDT_CONF_UPDATE_EN; a
     * stage-0 interrupt after 1000 ticks of APB_CLK / 20 (1 ms), the feed;
     * a stage-1 CPU reset */
    c3 f;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    wr(&f, TG_WPROTECT, 0u);
    wr(&f, TG_WDTCONFIG0, 0u); /* protected: ignored */
    CHECK(rd(&f, TG_WDTCONFIG0) & (1u << 14));
    wr(&f, TG_WPROTECT, 0x50D83AA1u);
    wr(&f, TG_WDTCONFIG1, 20u << 16);
    wr(&f, TG_WDTCONFIG2, 1000u);
    wr(&f, TG_WDTCONFIG2 + 4u, 1000u);
    wr(&f, TG_WDTCONFIG0, 1u << 31 | 1u << 29 | 2u << 27); /* stage 0 int, 1 CPU reset */
    esk_mach_idle(&f.m, 20000u + 100u);
    CHECK_EQ(rd(&f, TG_INT_RAW) & 2u, 0u); /* not updated yet: still flash boot's */
    wr(&f, TG_WDTCONFIG0, 1u << 31 | 1u << 29 | 2u << 27 | 1u << 22);
    wr(&f, TG_WDTFEED, 1u);
    esk_mach_idle(&f.m, 19900u);
    CHECK_EQ(rd(&f, TG_INT_RAW) & 2u, 0u);
    esk_mach_idle(&f.m, 200u);
    CHECK_EQ(rd(&f, TG_INT_RAW) & 2u, 2u);
    wr(&f, TG_INT_CLR, 2u);
    CHECK(!f.m.reset_req);
    esk_mach_idle(&f.m, 20100u);
    CHECK(f.m.reset_req && f.m.reset_cpu_only); /* 0x0B, MWDT0 CPU reset */
    c3_close(&f);
}

static void test_rtc_calibration(void)
{
    /* 1024 cycles of RC_SLOW_CLK (136 kHz) are 301 176 of XTAL_CLK, ready
     * after 7.5 ms; XTAL32K_CLK without a crystal times out */
    c3 f;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    wr(&f, TG_CALI, 1024u << 16);
    wr(&f, TG_CALI, 1024u << 16 | 1u << 31);
    CHECK_EQ(rd(&f, TG_CALI) & (1u << 15), 0u);
    esk_mach_idle(&f.m, 20000000u / 1000u * 7u);
    CHECK_EQ(rd(&f, TG_CALI) & (1u << 15), 0u);
    esk_mach_idle(&f.m, 20000000u / 1000u);
    CHECK(rd(&f, TG_CALI) & (1u << 15));
    CHECK_EQ(rd(&f, TG_CALI1) >> 7, 301176u);
    wr(&f, TG_CALI, 2u << 13 | 1024u << 16);
    wr(&f, TG_CALI, 2u << 13 | 1024u << 16 | 1u << 31);
    esk_mach_idle(&f.m, 20000000u); /* 1 s: past 0x1ffffff XTAL_CLK cycles */
    CHECK_EQ(rd(&f, TG_CALI) & (1u << 15), 0u);
    CHECK_EQ(rd(&f, TG_CALI2) & 1u, 1u);
    c3_close(&f);
}

#define EFUSE 0x60008800u

static void test_efuse(void)
{
    /* the factory bits as esptool decodes them; a program command ORs
     * PGM_DATA into a block, which a read command loads */
    c3 f;
    u32 w3;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    CHECK_EQ(rd(&f, EFUSE + 0x44u), 0xA1456D62u); /* MAC 7C:DF:A1:45:6D:62 */
    CHECK_EQ(rd(&f, EFUSE + 0x48u) & 0xffffu, 0x7CDFu);
    w3 = rd(&f, EFUSE + 0x50u);
    CHECK_EQ((w3 >> 21) & 7u, 0u); /* PKG_VERSION: QFN32 */
    CHECK_EQ((w3 >> 18) & 7u, 3u); /* WAFER_VERSION_MINOR_LO: v0.3 */
    CHECK_EQ((w3 >> 27) & 7u, 1u); /* FLASH_CAP: 4 MB */
    CHECK_EQ(rd(&f, EFUSE + 0x54u) & 7u, 1u); /* FLASH_VENDOR: XMC */
    wr(&f, EFUSE + 0x00u, 0x00000081u);       /* PGM_DATA0 */
    wr(&f, EFUSE + 0x1CCu, 0x5A5Au);
    wr(&f, EFUSE + 0x1D4u, 3u << 2 | 2u); /* BLOCK3 (USR_DATA) */
    CHECK_EQ(rd(&f, EFUSE + 0x1D4u), 0u);
    CHECK_EQ(rd(&f, EFUSE + 0x7Cu), 0u); /* not read yet */
    wr(&f, EFUSE + 0x1CCu, 0x5AA5u);
    wr(&f, EFUSE + 0x1D4u, 1u);
    CHECK_EQ(rd(&f, EFUSE + 0x7Cu), 0x81u);
    CHECK_EQ(rd(&f, EFUSE + 0x1D8u), 3u); /* READ_DONE, PGM_DONE */
    c3_close(&f);
}

/* ---- GPIO --------------------------------------------------------------------- */

#define GPIO          0x60004000u
#define G_OUT_W1TS    (GPIO + 0x08u)
#define G_OUT_W1TC    (GPIO + 0x0Cu)
#define G_EN_W1TS     (GPIO + 0x24u)
#define G_IN          (GPIO + 0x3Cu)
#define G_STATUS      (GPIO + 0x44u)
#define G_STATUS_W1TC (GPIO + 0x4Cu)
#define G_PCPU_INT    (GPIO + 0x5Cu)
#define G_PIN(n)      (GPIO + 0x74u + 4u * (n))
#define G_FIN(y)      (GPIO + 0x154u + 4u * (y))
#define G_FOUT(n)     (GPIO + 0x554u + 4u * (n))
#define IOMUX(n)      (0x60009000u + 0x04u + 4u * (n))

static esk_mach_pin pins_out[32];
static int pins_seen[32], pins_ext[32];

static void h_pin_cfg(void *ctx, int port, int bit, const esk_mach_pin *cfg)
{
    (void)ctx;
    pins_out[16 * port + bit] = *cfg;
    pins_seen[16 * port + bit] = 1;
}

static int h_pin_in(void *ctx, int port, int bit, int *level)
{
    int n = 16 * port + bit;
    (void)ctx;
    if (pins_ext[n] < 0)
        return 0;
    *level = pins_ext[n];
    return 1;
}

static int gpio_open(c3 *f)
{
    int i;
    for (i = 0; i < 32; i++)
        pins_ext[i] = -1;
    memset(pins_seen, 0, sizeof pins_seen);
    if (!c3_open(f, NULL, 0, NULL, 0))
        return 0;
    f->m.host.pin_cfg = h_pin_cfg;
    f->m.host.pin_in = h_pin_in;
    esk_mach_reset(&f->m);
    return 1;
}

static void test_gpio_reset_and_output(void)
{
    /* out of reset: GPIO21 is U0TXD, driven high; GPIO9 and GPIO20 pulled
     * up; GPIO0 and GPIO18 with their inputs off. Then GPIO5 as a simple
     * output, and open-drain */
    c3 f;
    int port = -1, bit = -1;
    if (!gpio_open(&f))
        return;
    CHECK(pins_seen[21]);
    CHECK_EQ(pins_out[21].drive, 1);
    CHECK(pins_out[21].af);
    CHECK_EQ(pins_out[9].pull, 1);
    CHECK_EQ(pins_out[9].drive, -1);
    CHECK_EQ(pins_out[20].pull, 1);
    CHECK(pins_out[0].analog);
    CHECK(rd(&f, G_IN) & (1u << 9));
    CHECK(rd(&f, G_IN) & (1u << 20));
    CHECK(!(rd(&f, G_IN) & (1u << 0))); /* IE off */
    CHECK_EQ(rd(&f, GPIO + 0x38u), 8u); /* STRAP: GPIO9 high, SPI boot */
    CHECK_EQ(esk_esp32c3_uart_pin(&f.m, "UART0", 1, &port, &bit), 0);
    CHECK_EQ(16 * port + bit, 21);
    CHECK_EQ(esk_esp32c3_uart_pin(&f.m, "UART0", 0, &port, &bit), 0);
    CHECK_EQ(16 * port + bit, 20);
    wr(&f, IOMUX(5), 1u << 12 | 1u << 9); /* MCU_SEL 1 (GPIO), IE */
    wr(&f, G_EN_W1TS, 1u << 5);
    CHECK_EQ(pins_out[5].drive, 0);
    wr(&f, G_OUT_W1TS, 1u << 5);
    CHECK_EQ(pins_out[5].drive, 1);
    CHECK(rd(&f, G_IN) & (1u << 5));
    wr(&f, G_PIN(5), 1u << 2); /* PAD_DRIVER: open drain */
    CHECK(pins_out[5].open_drain);
    wr(&f, G_OUT_W1TC, 1u << 5);
    CHECK_EQ(pins_out[5].drive, 0);
    c3_close(&f);
}

static void test_gpio_input_interrupt(void)
{
    /* GPIO4 an input driven from outside; a rising-edge interrupt, its
     * status and the matrix source 16; W1TC clears it */
    c3 f;
    if (!gpio_open(&f))
        return;
    wr(&f, IOMUX(4), 1u << 12 | 1u << 9);
    pins_ext[4] = 0;
    esk_esp32c3_pins_changed(&f.m);
    CHECK(!(rd(&f, G_IN) & (1u << 4)));
    wr(&f, G_PIN(4), 1u << 7 | 1u << 13); /* rising edge, the CPU's */
    pins_ext[4] = 1;
    esk_esp32c3_pins_changed(&f.m);
    CHECK(rd(&f, G_IN) & (1u << 4));
    CHECK_EQ(rd(&f, G_STATUS), 1u << 4);
    CHECK_EQ(rd(&f, G_PCPU_INT), 1u << 4);
    CHECK(rd(&f, INTR_STATUS_0) & (1u << 16));
    wr(&f, G_STATUS_W1TC, 1u << 4);
    CHECK_EQ(rd(&f, G_PCPU_INT), 0u);
    CHECK(!(rd(&f, INTR_STATUS_0) & (1u << 16)));
    pins_ext[4] = 0; /* a fall: not this interrupt */
    esk_esp32c3_pins_changed(&f.m);
    CHECK_EQ(rd(&f, G_STATUS), 0u);
    wr(&f, G_PIN(4), 4u << 7 | 1u << 13); /* low level: while it lasts */
    CHECK_EQ(rd(&f, G_STATUS), 1u << 4);
    wr(&f, G_STATUS_W1TC, 1u << 4);
    CHECK_EQ(rd(&f, G_STATUS), 1u << 4);
    c3_close(&f);
}

static void test_gpio_matrix_uart(void)
{
    /* UART1's TXD out on GPIO7 and its RXD in from GPIO8, through the
     * matrix (signals 9); UART0's RXD moved off GPIO20 by IO MUX */
    c3 f;
    int port = -1, bit = -1;
    if (!gpio_open(&f))
        return;
    CHECK_EQ(esk_esp32c3_uart_pin(&f.m, "UART1", 1, &port, &bit), -1);
    wr(&f, IOMUX(7), 1u << 12);
    wr(&f, G_FOUT(7), 9u);
    CHECK_EQ(esk_esp32c3_uart_pin(&f.m, "UART1", 1, &port, &bit), 0);
    CHECK_EQ(16 * port + bit, 7);
    CHECK_EQ(pins_out[7].drive, 1);
    CHECK(pins_out[7].af);
    CHECK(!pins_out[8].af);
    wr(&f, G_FIN(9), 1u << 6 | 8u);
    CHECK_EQ(esk_esp32c3_uart_pin(&f.m, "UART1", 0, &port, &bit), 0);
    CHECK_EQ(16 * port + bit, 8);
    /* the RX pin is the UART's to the runtime, which then reads its frames
     * (ESP-IDF connects TXD first, so nothing else would tell it) */
    CHECK(pins_out[8].af);
    wr(&f, G_FIN(9), 1u << 6 | 0x1Fu);
    CHECK(!pins_out[8].af);
    wr(&f, G_FIN(9), 1u << 6 | 8u);
    wr(&f, IOMUX(20), 1u << 12 | 1u << 9);
    CHECK_EQ(esk_esp32c3_uart_pin(&f.m, "UART0", 0, &port, &bit), -1);
    c3_close(&f);
}

static void test_counter_user_csrs(void)
{
    /* the ROM starts the counter through 0x800 and 0x801 (cycles,
     * wrapping); ESP-IDF reads it as mpccr */
    const u32 prog[] = { addi(5, 0, 1), csrrw(0, 0x800, 5), csrrw(0, 0x801, 5), csrrw(0, 0x802, 0),
                         csrrs(10, 0x7e0, 0), csrrs(11, 0x7e1, 0), addi(6, 0, 1), csrrs(12, 0x7e2, 0),
                         JAL_SELF };
    c3 f;
    if (!c3_open(&f, prog, sizeof prog / sizeof prog[0], NULL, 0))
        return;
    esk_mach_run(&f.m, 0xffffff, 8);
    CHECK_EQ(x(&f, 10), 1u);
    CHECK_EQ(x(&f, 11), 1u);
    CHECK_EQ(x(&f, 12), 4u); /* the write, two reads and the addi */
    c3_close(&f);
}

static void test_rtc_calibration_periodic(void)
{
    /* out of reset the timer group counts one RC_FAST_DIV_CLK cycle (17.5
     * MHz / 256) again and again: 585 XTAL_CLK cycles, CYCLING_DATA_VLD and
     * not RDY; a threshold of 1 stops it with a timeout, as ESP-IDF does */
    c3 f;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    CHECK_EQ(rd(&f, TG_CALI), 0x00013000u);
    esk_mach_idle(&f.m, 20000u); /* 1 ms */
    CHECK_EQ(rd(&f, TG_CALI1), 585u << 7 | 1u);
    CHECK_EQ(rd(&f, TG_CALI) & (1u << 15), 0u);
    CHECK_EQ(rd(&f, TG_CALI2) & 1u, 0u);
    wr(&f, TG_CALI2, 1u << 7);
    esk_mach_idle(&f.m, 20000u);
    CHECK_EQ(rd(&f, TG_CALI2) & 1u, 1u);
    /* a one-shot after it: RDY, the data not marked periodic */
    wr(&f, TG_CALI2, 0x1ffffffu << 7);
    wr(&f, TG_CALI, 1u << 13 | 1u << 16);
    wr(&f, TG_CALI, 1u << 13 | 1u << 16 | 1u << 31);
    CHECK_EQ(rd(&f, TG_CALI2) & 1u, 0u);
    esk_mach_idle(&f.m, 20000u);
    CHECK(rd(&f, TG_CALI) & (1u << 15));
    CHECK_EQ(rd(&f, TG_CALI1), 585u << 7);
    c3_close(&f);
}

#define REGI2C 0x6000E000u

static void test_regi2c(void)
{
    /* the ROM's rom_i2c_writeReg and readReg: block, register, data, write
     * flag; the byte read back in bits 23:16, busy (bit 25) clear */
    c3 f;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    wr(&f, REGI2C, 0x66u | 4u << 8 | 0x5Au << 16 | 0x05000000u);
    CHECK_EQ(rd(&f, REGI2C) & (1u << 25), 0u);
    wr(&f, REGI2C, 0x66u | 4u << 8 | 0x04000000u);
    CHECK_EQ((rd(&f, REGI2C) >> 16) & 0xffu, 0x5Au);
    wr(&f, REGI2C, 0x6Du | 4u << 8 | 0x04000000u);
    CHECK_EQ((rd(&f, REGI2C) >> 16) & 0xffu, 0u); /* another block's */
    /* the low-power bias block's o-code calibration: IR_RESETB 0 then 1 */
    wr(&f, REGI2C, 0x61u | 0u << 8 | 0x00u << 16 | 0x05000000u);
    wr(&f, REGI2C, 0x61u | 3u << 8 | 0x04000000u);
    CHECK_EQ((rd(&f, REGI2C) >> 16) & 0x09u, 0u);
    wr(&f, REGI2C, 0x61u | 0u << 8 | 0x01u << 16 | 0x05000000u);
    wr(&f, REGI2C, 0x61u | 3u << 8 | 0x04000000u);
    CHECK_EQ((rd(&f, REGI2C) >> 16) & 0x09u, 0x09u);
    wr(&f, REGI2C + 0x44u, 0x12345678u); /* ANA_CONFIG */
    CHECK_EQ(rd(&f, REGI2C + 0x44u), 0x12345678u);
    /* a system reset keeps the analog registers; a power-on clears them */
    f.m.reset_cause = ESK_RESET_WATCHDOG;
    esk_mach_reset(&f.m);
    CHECK_EQ(rd(&f, REGI2C + 0x44u), 0x12345678u);
    f.m.reset_cause = ESK_RESET_POWER;
    esk_mach_reset(&f.m);
    CHECK_EQ(rd(&f, REGI2C + 0x44u), 0u);
    c3_close(&f);
}

static void test_rng_and_radio(void)
{
    /* RND_DATA: a new number at every read, the same sequence every run;
     * the radio's blocks keep what is written */
    c3 f, g;
    u32 a, b;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    a = rd(&f, 0x600260B0u);
    b = rd(&f, 0x600260B0u);
    CHECK(a != b);
    CHECK(a != 0u && b != 0u);
    if (c3_open(&g, NULL, 0, NULL, 0)) {
        CHECK_EQ(rd(&g, 0x600260B0u), a);
        c3_close(&g);
    }
    wr(&f, 0x6001CCD4u, 0xA5A50000u); /* NRXPD_CTRL */
    wr(&f, 0x60006090u, 0x80u);       /* FE_GEN_CTRL */
    wr(&f, 0x600050F0u, 0x100u);      /* FE2_TX_INTERP_CTRL */
    CHECK_EQ(rd(&f, 0x6001CCD4u), 0xA5A50000u);
    CHECK_EQ(rd(&f, 0x60006090u), 0x80u);
    CHECK_EQ(rd(&f, 0x600050F0u), 0x100u);
    c3_close(&f);
}

/* the Wi-Fi MAC (wifi.c): what the host's air saw of a transmitted frame */
static struct {
    int n;
    u8 frame[64];
    u32 len, mhz, kbps, token;
    int kind, dbm;
    u64 airtime;
} wtx;

static int wifi_radio_tx(void *ctx, const u8 *frame, u32 len, u32 mhz, int dbm, u32 kbps, u64 airtime, int kind,
                         u32 token)
{
    (void)ctx;
    wtx.n++;
    wtx.dbm = dbm;
    wtx.airtime = airtime;
    wtx.len = len;
    memcpy(wtx.frame, frame, len < sizeof wtx.frame ? len : sizeof wtx.frame);
    wtx.mhz = mhz;
    wtx.kbps = kbps;
    wtx.kind = kind;
    wtx.token = token;
    return 0;
}

static void test_wifi_mac(void)
{
    /* libpp's way with the MAC (c3-radios.md step 3): a frame queued on
     * a transmit queue leaves on the RF PLL's channel at its PLCP rate and
     * completes with its outcome; a frame for the station's address fills
     * the receive list's next descriptor with ESP-IDF's rx_ctrl header */
    enum { MAC = 0x60033000u, D_TX = 0x3FC90000u, B_TX = 0x3FC90100u, D1 = 0x3FC90200u, D2 = 0x3FC90210u,
           B1 = 0x3FC91000u, B2 = 0x3FC92000u };
    static const u8 me[6] = { 0x7c, 0xdf, 0xa1, 0x45, 0x6d, 0x62 };
    c3 f;
    u8 fr[30];
    u32 i, w0, us;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    memset(&wtx, 0, sizeof wtx);
    f.m.host.radio_tx = wifi_radio_tx;
    CHECK(f.m.radio.rx != NULL && (f.m.radio.kinds & ESK_RADIO_WIFI));
    wr(&f, REGI2C + 0x150u, 37u << 20); /* the RF PLL at 2437 MHz, channel 6 */
    wr(&f, MAC + 0xC34u, 0x19A879E0u);  /* hal_init's interrupt enables */
    /* transmit: a 24-byte probe request (28 with its FCS) on queue 0 */
    for (i = 0; i < 24; i++)
        CHECK_EQ(esk_bus_debug_write(&f.m.bus, B_TX + i, 1, i < 4 ? 0x40u >> (8 * i) : 0xFFu), 0);
    wr(&f, D_TX, 0x80000000u | 0x40000000u | 28u << 12 | 64u);
    wr(&f, D_TX + 4u, B_TX);
    wr(&f, D_TX + 8u, 0);
    wr(&f, MAC + 0x12F8u, 28u); /* PLCP1: the length, rate 0 (1 Mbit/s) */
    wr(&f, MAC + 0xD08u, 0xC0000000u | (D_TX & 0xFFFFFu));
    CHECK_EQ(wtx.n, 1);
    CHECK_EQ(wtx.len, 24);
    CHECK_EQ(wtx.mhz, 2437);
    CHECK_EQ(wtx.kbps, 1000);
    CHECK_EQ(wtx.kind, ESK_RADIO_WIFI);
    CHECK_EQ(wtx.frame[0], 0x40);
    CHECK_EQ(wtx.token & 7u, 0);
    CHECK(rd(&f, MAC + 0xD08u) & 0xC0000000u); /* still sending */
    CHECK_EQ(rd(&f, MAC + 0xCB0u), 0);
    /* queue 1 waits for the radio */
    wr(&f, MAC + 0x12F8u - 76u, 28u | 11u << 12); /* 6 Mbit/s */
    wr(&f, MAC + 0xD00u, 0xC0000000u | (D_TX & 0xFFFFFu));
    CHECK_EQ(wtx.n, 1);
    f.m.radio.tx_done(f.m.radio.ctx, wtx.token, 0); /* a group frame: sent */
    CHECK_EQ(rd(&f, MAC + 0xD08u) & 0xC0000000u, 0);
    CHECK_EQ((rd(&f, MAC + 0x1320u) >> 12) & 15u, 0);
    CHECK_EQ(rd(&f, MAC + 0xCB0u), 1u);
    CHECK(rd(&f, MAC + 0xC3Cu) & (1u << 7));
    CHECK(rd(&f, INTR_STATUS_0) & 1u); /* source 0, WIFI_MAC */
    CHECK_EQ(wtx.n, 2);                /* then queue 1's */
    CHECK_EQ(wtx.kbps, 6000);
    CHECK_EQ(wtx.token & 7u, 1);
    f.m.radio.tx_done(f.m.radio.ctx, wtx.token - 1u, 1); /* not its token: ignored */
    CHECK_EQ(rd(&f, MAC + 0xCB0u), 1u);
    wr(&f, MAC + 0xCACu, 1u); /* hal_mac_clr_txq_state */
    CHECK_EQ(rd(&f, MAC + 0xCB0u), 0);
    wr(&f, MAC + 0xC40u, 1u << 7);
    CHECK_EQ(rd(&f, MAC + 0xC3Cu) & (1u << 7), 0);
    CHECK_EQ(rd(&f, INTR_STATUS_0) & 1u, 0);
    f.m.radio.tx_done(f.m.radio.ctx, wtx.token, 1);
    CHECK_EQ((rd(&f, MAC + 0x1320u - 76u) >> 12) & 15u, 0);
    CHECK_EQ(rd(&f, MAC + 0xCB0u), 2u);
    /* a unicast frame unanswered: no ACK (5) */
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, B_TX + 4u, 1, 0x02u), 0);
    wr(&f, MAC + 0xD08u, 0xC0000000u | (D_TX & 0xFFFFFu));
    CHECK_EQ(wtx.n, 3);
    f.m.radio.tx_done(f.m.radio.ctx, wtx.token, 0);
    CHECK_EQ((rd(&f, MAC + 0x1320u) >> 12) & 15u, 5u);
    CHECK_EQ(rd(&f, MAC + 0xCB0u), 3u);
    /* receive: off until enabled; the station's address and mask */
    memset(fr, 0, sizeof fr);
    fr[0] = 0x50;
    memcpy(fr + 4, me, 6);
    for (i = 24; i < sizeof fr; i++)
        fr[i] = (u8)i;
    CHECK_EQ(f.m.radio.tuned(f.m.radio.ctx, ESK_RADIO_WIFI), 0);
    wr(&f, MAC + 0x40u, 0x45a1df7cu);
    wr(&f, MAC + 0x44u, 0x626du);
    wr(&f, MAC + 0x60u, 0xFFFFFFFFu);
    wr(&f, MAC + 0x64u, 0x1FFFFu);
    wr(&f, D1, 0x80000000u | 1600u);
    wr(&f, D1 + 4u, B1);
    wr(&f, D1 + 8u, D2);
    wr(&f, D2, 0x80000000u | 1600u);
    wr(&f, D2 + 4u, B2);
    wr(&f, D2 + 8u, D1);
    wr(&f, MAC + 0xC64u, 0x3FC00000u);
    wr(&f, MAC + 0x88u, D1);
    wr(&f, MAC + 0x84u, 0x80000000u);
    CHECK_EQ(f.m.radio.tuned(f.m.radio.ctx, ESK_RADIO_WIFI), 2437);
    CHECK_EQ(f.m.radio.tuned(f.m.radio.ctx, ESK_RADIO_BLE), 0);
    CHECK_EQ(rd(&f, MAC + 0x8Cu), D1);
    CHECK_EQ(f.m.radio.rx(f.m.radio.ctx, fr, sizeof fr, 2437u, -42, 1000u, ESK_RADIO_WIFI), 1);
    w0 = rd(&f, D1);
    CHECK_EQ(w0 >> 30, 1u); /* the driver's, eof */
    CHECK_EQ((w0 >> 12) & 0xFFFu, 48u + 30u + 4u);
    CHECK_EQ(w0 & 0xFFFu, 1600u);
    CHECK_EQ(rd(&f, B1) & 0xFFu, (u32)(u8)-42);
    CHECK(rd(&f, B1) & (1u << 28)); /* the station interface took it */
    CHECK_EQ(rd(&f, B1 + 8u) >> 16 & 15u, 6u);
    CHECK_EQ(rd(&f, B1 + 44u) & 0xFFFu, 34u);
    CHECK_EQ(rd(&f, B1 + 44u) >> 24, 0);
    CHECK_EQ(rd(&f, B1 + 48u) & 0xFFu, 0x50u);
    CHECK_EQ(rd(&f, B1 + 48u + 24u), 0x1B1A1918u);
    CHECK_EQ(rd(&f, MAC + 0x90u), D1 & 0xFFFFFu);
    CHECK_EQ(rd(&f, MAC + 0x8Cu), D2);
    CHECK(rd(&f, MAC + 0xC3Cu) & (1u << 24));
    CHECK(rd(&f, INTR_STATUS_0) & 1u);
    /* another station's frame is not taken, nor answered */
    fr[9] = 0x63;
    CHECK_EQ(f.m.radio.rx(f.m.radio.ctx, fr, sizeof fr, 2437u, -42, 1000u, ESK_RADIO_WIFI), 0);
    CHECK_EQ(rd(&f, MAC + 0x8Cu), D2);
    /* a group frame is taken, not answered; then the list has run out */
    fr[4] = 0xFF;
    CHECK_EQ(f.m.radio.rx(f.m.radio.ctx, fr, sizeof fr, 2437u, -42, 1000u, ESK_RADIO_WIFI), 0);
    CHECK_EQ(rd(&f, D2) >> 30, 1u);
    CHECK_EQ(rd(&f, MAC + 0x90u), D2 & 0xFFFFFu);
    CHECK_EQ(rd(&f, MAC + 0x8Cu), 0); /* D1 is still the driver's */
    CHECK_EQ(f.m.radio.rx(f.m.radio.ctx, fr, sizeof fr, 2437u, -42, 1000u, ESK_RADIO_WIFI), 0);
    /* the driver gives D1 back and reloads: the MAC goes on there */
    wr(&f, D1, 0x80000000u | 1600u);
    wr(&f, MAC + 0x84u, 0x80000001u);
    CHECK_EQ(rd(&f, MAC + 0x84u), 0x80000000u);
    CHECK_EQ(rd(&f, MAC + 0x8Cu), D1);
    /* the MAC's time: microseconds, latched for hal_get_tsf_time */
    us = rd(&f, MAC + 0x2000u);
    esk_mach_idle(&f.m, esk_mach_core_hz(&f.m) / 1000u);
    CHECK_NEAR((double)(rd(&f, MAC + 0x2000u) - us), 1000.0, 1.0);
    wr(&f, MAC + 0x200Cu, 1u);
    CHECK_NEAR((double)rd(&f, MAC + 0x2018u), (double)rd(&f, MAC + 0x2000u), 1.0);
    CHECK_EQ(rd(&f, MAC + 0x201Cu), 0);
    c3_close(&f);
}

/* the Bluetooth LE core (ble.c): its exchange memory, mapped at EM_RAM */
#define BLE_C3 0x60031000u
#define EM_RAM 0x3FCA0000u

static void em16(c3 *f, u32 o, u32 v)
{
    CHECK_EQ(esk_bus_debug_write(&f->m.bus, EM_RAM + o, 2, v), 0);
}

static u32 em16r(c3 *f, u32 o)
{
    u32 v = 0;
    CHECK_EQ(esk_bus_debug_read(&f->m.bus, EM_RAM + o, 2, &v), 0);
    return v;
}

/* the exchange memory's first 8 KB contiguous at EM_RAM: the mapping
 * registers of its 1 KB blocks (0, 1-2, 3, 4, 5-6, 7) as the controller sets
 * them (r_emi_*) */
static void em_map(c3 *f)
{
    static const u32 start[6] = { 0x000u, 0x400u, 0xC00u, 0x1000u, 0x1400u, 0x1C00u };
    u32 i;
    for (i = 0; i < 6u; i++)
        wr(f, BLE_C3 + 0x204u + 4u * i, (start[i] / 4u) << 18 | ((EM_RAM + start[i]) & 0xFFFFCu) >> 2);
}

/* microseconds until the radio has sent `n` frames */
static u32 ble_us_until(c3 *f, int n)
{
    u32 us = 0;
    while (wtx.n < n && us < 100000u) {
        esk_mach_idle(&f->m, esk_mach_core_hz(&f->m) / 1000000u);
        us++;
    }
    return us;
}

static void test_ble(void)
{
    /* the controller's way with the core (c3-radios.md step 6): the IP's
     * version, the native clock sampled, a half-slot timer's interrupt with
     * and without the FIFO, and an advertising event started from the
     * exchange table: its PDU on 37, 38 and 39 at +9 dBm, then its end */
    static const u8 ad[9] = { 2, 1, 6, 5, 9, 'b', 'e', 'n', 'c' };
    c3 f;
    u32 i, slot, w, us;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    memset(&wtx, 0, sizeof wtx);
    f.m.host.radio_tx = wifi_radio_tx;
    CHECK(f.m.radio.kinds & ESK_RADIO_BLE);
    CHECK_EQ(f.m.radio.tuned(f.m.radio.ctx, ESK_RADIO_BLE), 0);
    CHECK_EQ(rd(&f, BLE_C3 + 0x04u), 0x09001B00u);
    wr(&f, BLE_C3, 0xE0000001u); /* the soft resets: done at once */
    CHECK_EQ(rd(&f, BLE_C3), 1u);
    /* 1 ms: 2000 half-microseconds, 3 half-slots and 125 into the fourth */
    esk_mach_idle(&f.m, esk_mach_core_hz(&f.m) / 1000u);
    wr(&f, BLE_C3 + 0x1Cu, 0x80000000u);
    CHECK_EQ(rd(&f, BLE_C3 + 0x1Cu), 3u);
    CHECK_EQ(rd(&f, BLE_C3 + 0x20u), 499u);
    /* the half-slot timer at 10 (3125 us) */
    wr(&f, BLE_C3 + 0x0Cu, 1u << 10 | 1u << 5);
    wr(&f, BLE_C3 + 0xE8u, 10u);
    esk_mach_idle(&f.m, esk_mach_core_hz(&f.m) / 1000u);
    CHECK_EQ(rd(&f, BLE_C3 + 0x10u), 0);
    esk_mach_idle(&f.m, esk_mach_core_hz(&f.m) / 800u);
    CHECK_EQ(rd(&f, BLE_C3 + 0x10u), 1u << 10);
    CHECK(rd(&f, INTR_STATUS_0) & 1u << 8); /* source 8, RWBLE */
    wr(&f, BLE_C3 + 0x18u, 1u << 10);
    CHECK_EQ(rd(&f, BLE_C3 + 0x10u), 0);
    CHECK_EQ(rd(&f, INTR_STATUS_0) & 1u << 8, 0);
    /* with the FIFO: the entry until it is taken */
    wr(&f, BLE_C3 + 0x2D8u, 0x80000000u);
    wr(&f, BLE_C3 + 0xE8u, 12u);
    esk_mach_idle(&f.m, esk_mach_core_hz(&f.m) / 1000u);
    w = rd(&f, BLE_C3 + 0x2D8u);
    CHECK_EQ((w >> 5) & 31u, 1u);
    CHECK_EQ((w >> 10) & 0x1FFFFFu, 1u << 10);
    CHECK(rd(&f, INTR_STATUS_0) & 1u << 8);
    wr(&f, BLE_C3 + 0x18u, 1u << 10);
    CHECK(rd(&f, INTR_STATUS_0) & 1u << 8); /* still in the FIFO */
    wr(&f, BLE_C3 + 0x2D8u, 0x80000001u);
    CHECK_EQ((rd(&f, BLE_C3 + 0x2D8u) >> 5) & 31u, 0);
    CHECK_EQ(rd(&f, INTR_STATUS_0) & 1u << 8, 0);
    /* the exchange memory from EM_RAM; an advertising event: the control
     * structure at 0x400, its descriptor at 0x500, its data at 0x600 */
    em_map(&f);
    em16(&f, 0x400u, 4u);
    for (i = 0; i < 6u; i += 2)
        em16(&f, 0x406u + i, (u32)(0x11u * (i + 1u)) | (u32)(0x11u * (i + 2u)) << 8);
    em16(&f, 0x416u, 7u);
    em16(&f, 0x41Cu, 0x500u);
    em16(&f, 0x502u, 0u | (6u + sizeof ad) << 8); /* ADV_IND, public */
    em16(&f, 0x504u, 0x600u);
    for (i = 0; i < sizeof ad; i++)
        CHECK_EQ(esk_bus_debug_write(&f.m.bus, EM_RAM + 0x600u + i, 1, ad[i]), 0);
    wr(&f, BLE_C3 + 0x1Cu, 0x80000000u);
    slot = rd(&f, BLE_C3 + 0x1Cu) + 4u; /* 1.25 ms on */
    em16(&f, 0, 0);
    em16(&f, 2, slot);
    em16(&f, 4, 0);
    em16(&f, 6, 624u);
    em16(&f, 8, 0x400u / 2u);
    wr(&f, BLE_C3 + 0x100u, 0x80000000u);
    CHECK_EQ(rd(&f, BLE_C3 + 0x100u) & 0x80000000u, 0);
    us = ble_us_until(&f, 1); /* at the start of half-slot `slot` */
    CHECK(us >= 937u && us <= 1251u);
    CHECK_EQ((em16r(&f, 0) >> 3) & 7u, 2u); /* under way */
    CHECK_EQ(wtx.kind, ESK_RADIO_BLE);
    CHECK_EQ(wtx.mhz, 2402);
    CHECK_EQ(wtx.kbps, 1000);
    CHECK_EQ(wtx.dbm, 9);
    CHECK_EQ(wtx.token, 0x80000000u);
    CHECK_EQ(wtx.len, 8u + sizeof ad);
    CHECK_EQ(wtx.frame[0], 0);
    CHECK_EQ(wtx.frame[1], 6u + sizeof ad);
    CHECK_EQ(wtx.frame[2], 0x11);
    CHECK_EQ(wtx.frame[7], 0x66);
    CHECK(!memcmp(wtx.frame + 8, ad, sizeof ad));
    CHECK_EQ(wtx.airtime, (u64)(1u + 4u + 8u + sizeof ad + 3u) * 8u * ES_PS_PER_US);
    /* the next channels each after the PDU, T_IFS and a SCAN_REQ's time:
     * 152 + 150 + 176 us */
    us = ble_us_until(&f, 2);
    CHECK(us >= 520u && us <= 530u);
    CHECK_EQ(wtx.mhz, 2426);
    us = ble_us_until(&f, 3);
    CHECK(us >= 520u && us <= 530u);
    CHECK_EQ(wtx.mhz, 2480);
    CHECK_EQ(rd(&f, INTR_STATUS_0) & 1u << 8, 0);
    esk_mach_idle(&f.m, esk_mach_core_hz(&f.m) / 1000u);
    CHECK_EQ(wtx.n, 3);
    CHECK_EQ((em16r(&f, 0) >> 3) & 7u, 3u); /* over */
    w = rd(&f, BLE_C3 + 0x2D8u);
    CHECK_EQ((w >> 5) & 31u, 1u);
    CHECK_EQ((w >> 10) & 0x1FFFFFu, 1u << 5);
    CHECK(rd(&f, INTR_STATUS_0) & 1u << 8);
    /* one channel in the map: one PDU */
    em16(&f, 0x416u, 2u);
    em16(&f, 0, 0);
    wr(&f, BLE_C3 + 0x1Cu, 0x80000000u);
    em16(&f, 2, rd(&f, BLE_C3 + 0x1Cu) + 2u);
    wr(&f, BLE_C3 + 0x100u, 0x80000000u);
    esk_mach_idle(&f.m, esk_mach_core_hz(&f.m) / 200u);
    CHECK_EQ(wtx.n, 4);
    CHECK_EQ(wtx.mhz, 2426);
    CHECK_EQ((em16r(&f, 0) >> 3) & 7u, 3u);
    CHECK_EQ((rd(&f, BLE_C3 + 0x2D8u) >> 5) & 31u, 2u);
    c3_close(&f);
}

/* `us` microseconds a microsecond at a time, as a running program sees the
 * core's steps one after another */
static void ble_idle_us(c3 *f, u32 us)
{
    while (us--)
        esk_mach_idle(&f->m, esk_mach_core_hz(&f->m) / 1000000u);
}

/* a connection event as the slave (ble.c): the test plays the master */
static void test_ble_con(void)
{
    static const u8 hello[5] = { 'h', 'e', 'l', 'l', 'o' };
    u8 m[8];
    c3 f;
    u32 i, slot, us;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    memset(&wtx, 0, sizeof wtx);
    f.m.host.radio_tx = wifi_radio_tx;
    em_map(&f);
    wr(&f, BLE_C3 + 0x0Cu, 1u << 1 | 1u << 2 | 1u << 5);
    wr(&f, BLE_C3 + 0x2D8u, 0x80000000u);
    /* link 1's control structure: format 3, the last unmapped channel 0 and
     * the hop 7 (channel 7: 2418 MHz), all 37 channels, a 200 us window, its
     * transmit descriptor; event counter 0 */
    em16(&f, 0x45Au, 3u);
    em16(&f, 0x45Au + 0x16u, 0x8700u);
    em16(&f, 0x45Au + 0x1Au, 100u);
    em16(&f, 0x45Au + 0x1Cu, 0x147Eu);
    for (i = 0; i < 4u; i++)
        CHECK_EQ(esk_bus_debug_write(&f.m.bus, EM_RAM + 0x45Au + 0x22u + i, 1, 0xFFu), 0);
    CHECK_EQ(esk_bus_debug_write(&f.m.bus, EM_RAM + 0x45Au + 0x26u, 1, 0x1Fu), 0);
    em16(&f, 0x45Au + 0x50u, 0);
    /* its data: "hello" in an L2CAP start fragment */
    em16(&f, 0x147Eu, 0);
    em16(&f, 0x147Eu + 2u, 2u | 5u << 8);
    em16(&f, 0x147Eu + 4u, 0x1C00u);
    for (i = 0; i < sizeof hello; i++)
        CHECK_EQ(esk_bus_debug_write(&f.m.bus, EM_RAM + 0x1C00u + i, 1, hello[i]), 0);
    /* two receive descriptors from 0x1000 */
    em16(&f, 0x1000u, 0x1014u);
    em16(&f, 0x1000u + 18u, 0x1800u);
    em16(&f, 0x1014u, 0x1000u);
    em16(&f, 0x1014u + 18u, 0x1840u);
    wr(&f, BLE_C3 + 0x24u, 0x1000u);
    /* the event at a half-slot's start */
    wr(&f, BLE_C3 + 0x1Cu, 0x80000000u);
    slot = rd(&f, BLE_C3 + 0x1Cu) + 4u;
    em16(&f, 0x10u, 0);
    em16(&f, 0x12u, slot);
    em16(&f, 0x14u, 0);
    em16(&f, 0x16u, 624u);
    em16(&f, 0x18u, 0x45Au / 2u);
    wr(&f, BLE_C3 + 0x100u, 0x80000001u);
    CHECK_EQ(f.m.radio.tuned(f.m.radio.ctx, ESK_RADIO_BLE), 0);
    for (us = 0; us < 2000u && !f.m.radio.tuned(f.m.radio.ctx, ESK_RADIO_BLE); us++)
        esk_mach_idle(&f.m, esk_mach_core_hz(&f.m) / 1000000u);
    CHECK(us >= 937u && us <= 1251u);
    CHECK_EQ(f.m.radio.tuned(f.m.radio.ctx, ESK_RADIO_BLE), 2418);
    CHECK_EQ((em16r(&f, 0x10u) >> 3) & 7u, 2u);
    /* the master's packet: LLID 2, SN 0, NESN 0, "abc" */
    ble_idle_us(&f, 20u); /* 20 us into the window */
    m[0] = 0x02;
    m[1] = 3;
    m[2] = 'a';
    m[3] = 'b';
    m[4] = 'c';
    esk_mach_outside(&f.m); /* as the air delivers it (mcu.c) */
    CHECK_EQ(f.m.radio.rx(f.m.radio.ctx, m, 5, 2418u, -40, 1000u, ESK_RADIO_BLE), 0);
    CHECK_EQ(f.m.radio.tuned(f.m.radio.ctx, ESK_RADIO_BLE), 0); /* taking it */
    ble_idle_us(&f, 64u); /* its end: 64 us on */
    CHECK(em16r(&f, 0x1000u) & 0x8000u);                       /* filled */
    CHECK_EQ(em16r(&f, 0x1002u), 0);                            /* status: new */
    CHECK_EQ(em16r(&f, 0x1004u), 0x0302u);
    CHECK_EQ(em16r(&f, 0x100Cu) >> 11, 1u);                     /* link 1 */
    CHECK_EQ(em16r(&f, 0x1800u), 'a' | 'b' << 8);
    CHECK_EQ(rd(&f, BLE_C3 + 0x24u), 0x1014u);
    CHECK_EQ((rd(&f, BLE_C3 + 0x2D8u) >> 10) & 0x1FFFFFu, 1u << 2); /* received */
    wr(&f, BLE_C3 + 0x2D8u, 0x80000001u);
    /* the answer T_IFS later: "hello", SN 0, NESN 1 */
    us = ble_us_until(&f, 1);
    CHECK(us >= 140u && us <= 160u);
    CHECK_EQ(wtx.mhz, 2418);
    CHECK_EQ(wtx.kind, ESK_RADIO_BLE);
    CHECK_EQ(wtx.frame[0], 0x06);
    CHECK_EQ(wtx.frame[1], 5);
    CHECK(!memcmp(wtx.frame + 2, hello, sizeof hello));
    CHECK_EQ(em16r(&f, 0x147Eu) & 0x8000u, 0); /* not acknowledged yet */
    ble_idle_us(&f, 200u);
    CHECK_EQ((em16r(&f, 0x10u) >> 3) & 7u, 3u); /* over: neither has more */
    CHECK_EQ((rd(&f, BLE_C3 + 0x2D8u) >> 10) & 0x1FFFFFu, 1u << 5);
    wr(&f, BLE_C3 + 0x2D8u, 0x80000001u);
    /* the next event, on channel 14 (2434 MHz): the master acknowledges
     * (NESN 1) with an empty packet, SN 1 */
    em16(&f, 0x45Au + 0x16u, 0x8707u);
    em16(&f, 0x45Au + 0x50u, 1u);
    wr(&f, BLE_C3 + 0x1Cu, 0x80000000u);
    slot = rd(&f, BLE_C3 + 0x1Cu) + 4u;
    em16(&f, 0x20u, 0);
    em16(&f, 0x22u, slot);
    em16(&f, 0x24u, 0);
    em16(&f, 0x26u, 624u);
    em16(&f, 0x28u, 0x45Au / 2u);
    wr(&f, BLE_C3 + 0x100u, 0x80000002u);
    for (us = 0; us < 2000u && !f.m.radio.tuned(f.m.radio.ctx, ESK_RADIO_BLE); us++)
        esk_mach_idle(&f.m, esk_mach_core_hz(&f.m) / 1000000u);
    CHECK_EQ(f.m.radio.tuned(f.m.radio.ctx, ESK_RADIO_BLE), 2434);
    m[0] = 0x01 | 1u << 2 | 1u << 3;
    m[1] = 0;
    esk_mach_outside(&f.m);
    CHECK_EQ(f.m.radio.rx(f.m.radio.ctx, m, 2, 2434u, -40, 1000u, ESK_RADIO_BLE), 0);
    us = ble_us_until(&f, 2);
    CHECK(em16r(&f, 0x147Eu) & 0x8000u); /* acknowledged: done */
    CHECK_EQ(em16r(&f, 0x45Au + 0x1Cu), 0);
    CHECK_EQ(wtx.frame[0], 0x01 | 1u << 3); /* empty, SN 1, NESN 0 */
    CHECK_EQ(wtx.frame[1], 0);
    CHECK_EQ((rd(&f, BLE_C3 + 0x2D8u) >> 5) & 31u, 2u); /* received, then sent after the acknowledgement */
    CHECK_EQ((rd(&f, BLE_C3 + 0x2D8u) >> 10) & 0x1FFFFFu, 1u << 2);
    wr(&f, BLE_C3 + 0x2D8u, 0x80000001u);
    CHECK_EQ((rd(&f, BLE_C3 + 0x2D8u) >> 10) & 0x1FFFFFu, 1u << 1);
    CHECK_EQ(em16r(&f, 0x1014u + 2u), 0);
    /* a third event: the master resends (SN 1 again): taken as already
     * received */
    ble_idle_us(&f, 500u);
    em16(&f, 0x45Au + 0x16u, 0x870Eu);
    em16(&f, 0x45Au + 0x50u, 2u);
    wr(&f, BLE_C3 + 0x1Cu, 0x80000000u);
    slot = rd(&f, BLE_C3 + 0x1Cu) + 4u;
    em16(&f, 0x30u, 0);
    em16(&f, 0x32u, slot);
    em16(&f, 0x34u, 0);
    em16(&f, 0x36u, 624u);
    em16(&f, 0x38u, 0x45Au / 2u);
    wr(&f, BLE_C3 + 0x100u, 0x80000003u);
    for (us = 0; us < 2000u && !f.m.radio.tuned(f.m.radio.ctx, ESK_RADIO_BLE); us++)
        esk_mach_idle(&f.m, esk_mach_core_hz(&f.m) / 1000000u);
    CHECK_EQ(f.m.radio.tuned(f.m.radio.ctx, ESK_RADIO_BLE), 2448);
    esk_mach_outside(&f.m);
    CHECK_EQ(f.m.radio.rx(f.m.radio.ctx, m, 2, 2448u, -40, 1000u, ESK_RADIO_BLE), 0);
    ble_idle_us(&f, 100u);
    CHECK_EQ(em16r(&f, 0x1000u + 2u), 1u << 6);
    /* answered, then the event ends */
    ble_idle_us(&f, 1000u);
    CHECK_EQ((em16r(&f, 0x30u) >> 3) & 7u, 3u);
    c3_close(&f);
}

#define GDMA 0x6003F000u

/* a GDMA channel 0 list from `out` (peripheral `peri`'s out link), and an
 * in list into `in` when given */
static void gdma_link(c3 *f, u32 peri, u32 out, u32 in)
{
    wr(f, SYSTEM + 0x14u, rd(f, SYSTEM + 0x14u) | 1u << 6); /* DMA */
    wr(f, SYSTEM + 0x1Cu, rd(f, SYSTEM + 0x1Cu) & ~(1u << 6));
    wr(f, GDMA + 0xD0u, 1u); /* OUT_RST */
    wr(f, GDMA + 0xD0u, 0u);
    wr(f, GDMA + 0x100u, peri);
    wr(f, GDMA + 0xE0u, (out & 0xfffffu) | 1u << 21);
    if (in) {
        wr(f, GDMA + 0x70u, 1u); /* IN_RST */
        wr(f, GDMA + 0x70u, 0u);
        wr(f, GDMA + 0xA0u, peri);
        wr(f, GDMA + 0x80u, (in & 0xfffffu) | 1u << 22);
    }
}

static void put_mem(c3 *f, u32 at, const u8 *b, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++)
        CHECK_EQ(esk_bus_debug_write(&f->m.bus, at + i, 1, b[i]), 0);
}

static void get_mem(c3 *f, u32 at, u8 *b, u32 n)
{
    u32 i, v = 0;
    for (i = 0; i < n; i++) {
        CHECK_EQ(esk_bus_debug_read(&f->m.bus, at + i, 1, &v), 0);
        b[i] = (u8)v;
    }
}

/* a descriptor at `d` for `n` bytes at `buf`: owner, suc_eof */
static void desc(c3 *f, u32 d, u32 buf, u32 n)
{
    wr(f, d, 1u << 31 | 1u << 30 | n << 12 | n);
    wr(f, d + 4u, buf);
    wr(f, d + 8u, 0u);
}

static void test_sha_dma(void)
{
    /* DMA-SHA: FIPS 180-4's one-block "abc" by DMA_START, and its two-block
     * message by DMA_START then DMA_CONTINUE, the blocks from the GDMA;
     * the interrupt, source 52 */
    static const u8 d1[20] = { 0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                              0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d };
    static const u8 d2[20] = { 0x84, 0x98, 0x3e, 0x44, 0x1c, 0x3b, 0xd2, 0x6e, 0xba, 0xae,
                              0x4a, 0xa1, 0xf9, 0x51, 0x29, 0xe5, 0xe5, 0x46, 0x70, 0xf1 };
    const char *m2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    u8 blk[128], out[20];
    c3 f;
    int i;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    wr(&f, SYSTEM + 0x14u, rd(&f, SYSTEM + 0x14u) | 1u << 2); /* CRYPTO_SHA */
    memset(blk, 0, sizeof blk);
    memcpy(blk, "abc", 3);
    blk[3] = 0x80;
    blk[63] = 24;
    put_mem(&f, 0x3FC90000u, blk, 64);
    desc(&f, 0x3FC90200u, 0x3FC90000u, 64);
    gdma_link(&f, 7, 0x3FC90200u, 0);
    wr(&f, SHA + 0x00u, 0);  /* SHA-1 */
    wr(&f, SHA + 0x0Cu, 1);  /* DMA_BLOCK_NUM */
    wr(&f, SHA + 0x28u, 1);  /* IRQ_ENA */
    wr(&f, SHA + 0x1Cu, 1);  /* DMA_START */
    for (i = 0; i < 5; i++) {
        u32 w = rd(&f, SHA + 0x40u + 4u * (u32)i);
        out[4 * i] = (u8)w;
        out[4 * i + 1] = (u8)(w >> 8);
        out[4 * i + 2] = (u8)(w >> 16);
        out[4 * i + 3] = (u8)(w >> 24);
    }
    CHECK(!memcmp(out, d1, 20));
    CHECK(rd(&f, INTR_STATUS_0 + 4u) & (1u << (52 - 32)));
    wr(&f, SHA + 0x24u, 1); /* CLEAR_IRQ */
    CHECK(!(rd(&f, INTR_STATUS_0 + 4u) & (1u << (52 - 32))));
    /* two blocks, one call each */
    memset(blk, 0, sizeof blk);
    memcpy(blk, m2, 56);
    blk[56] = 0x80;
    blk[126] = 0x01;
    blk[127] = 0xC0; /* 448 bits */
    put_mem(&f, 0x3FC90000u, blk, 128);
    desc(&f, 0x3FC90200u, 0x3FC90000u, 64);
    gdma_link(&f, 7, 0x3FC90200u, 0);
    wr(&f, SHA + 0x1Cu, 1);
    desc(&f, 0x3FC90200u, 0x3FC90040u, 64);
    gdma_link(&f, 7, 0x3FC90200u, 0);
    wr(&f, SHA + 0x20u, 1); /* DMA_CONTINUE */
    for (i = 0; i < 5; i++) {
        u32 w = rd(&f, SHA + 0x40u + 4u * (u32)i);
        out[4 * i] = (u8)w;
        out[4 * i + 1] = (u8)(w >> 8);
        out[4 * i + 2] = (u8)(w >> 16);
        out[4 * i + 3] = (u8)(w >> 24);
    }
    CHECK(!memcmp(out, d2, 20));
    c3_close(&f);
}

#define AES_C3 0x6003A000u

static void aes_words(c3 *f, u32 at, const u8 *b, int n)
{
    int i;
    for (i = 0; i < n; i++)
        wr(f, at + 4u * (u32)i, (u32)b[4 * i] | (u32)b[4 * i + 1] << 8 | (u32)b[4 * i + 2] << 16 |
                                    (u32)b[4 * i + 3] << 24);
}

static int aes_is(c3 *f, u32 at, const char *hexs)
{
    int i;
    for (i = 0; i < 16; i++) {
        unsigned v = 0;
        sscanf(hexs + 2 * i, "%2x", &v);
        if (((rd(f, at + 4u * (u32)(i / 4)) >> (8 * (i % 4))) & 0xFFu) != v)
            return 0;
    }
    return 1;
}

static void unhex(const char *h, u8 *out)
{
    size_t i;
    for (i = 0; h[2 * i] && h[2 * i + 1]; i++) {
        unsigned v = 0;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (u8)v;
    }
}

static void test_aes(void)
{
    /* Typical AES against FIPS 197 C.1 and C.3; DMA-AES's CBC and CTR
     * against SP 800-38A F.2.1 and F.5.1, the IV left for the next call,
     * DONE, DMA_EXIT and the interrupt (source 51) */
    u8 b[64];
    c3 f;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    wr(&f, SYSTEM + 0x14u, rd(&f, SYSTEM + 0x14u) | 1u << 1); /* CRYPTO_AES */
    unhex("000102030405060708090a0b0c0d0e0f", b);
    aes_words(&f, AES_C3, b, 4);
    unhex("00112233445566778899aabbccddeeff", b);
    aes_words(&f, AES_C3 + 0x20u, b, 4);
    wr(&f, AES_C3 + 0x40u, 0); /* AES-128 encryption */
    wr(&f, AES_C3 + 0x48u, 1);
    CHECK(aes_is(&f, AES_C3 + 0x30u, "69c4e0d86a7b0430d8cdb78070b4c55a"));
    CHECK_EQ(rd(&f, AES_C3 + 0x4Cu), 0); /* idle */
    unhex("69c4e0d86a7b0430d8cdb78070b4c55a", b);
    aes_words(&f, AES_C3 + 0x20u, b, 4);
    wr(&f, AES_C3 + 0x40u, 4); /* AES-128 decryption */
    wr(&f, AES_C3 + 0x48u, 1);
    CHECK(aes_is(&f, AES_C3 + 0x30u, "00112233445566778899aabbccddeeff"));
    unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", b);
    aes_words(&f, AES_C3, b, 8);
    unhex("00112233445566778899aabbccddeeff", b);
    aes_words(&f, AES_C3 + 0x20u, b, 4);
    wr(&f, AES_C3 + 0x40u, 2); /* AES-256 encryption */
    wr(&f, AES_C3 + 0x48u, 1);
    CHECK(aes_is(&f, AES_C3 + 0x30u, "8ea2b7ca516745bfeafc49904b496089"));
    /* DMA-AES, CBC */
    unhex("2b7e151628aed2a6abf7158809cf4f3c", b);
    aes_words(&f, AES_C3, b, 4);
    unhex("000102030405060708090a0b0c0d0e0f", b);
    aes_words(&f, AES_C3 + 0x50u, b, 4);
    unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51", b);
    put_mem(&f, 0x3FC90000u, b, 32);
    desc(&f, 0x3FC90200u, 0x3FC90000u, 32);
    wr(&f, 0x3FC90210u, 1u << 31 | 64u);
    wr(&f, 0x3FC90214u, 0x3FC90100u);
    wr(&f, 0x3FC90218u, 0u);
    gdma_link(&f, 6, 0x3FC90200u, 0x3FC90210u);
    wr(&f, AES_C3 + 0x40u, 0);
    wr(&f, AES_C3 + 0x94u, 1); /* CBC */
    wr(&f, AES_C3 + 0x98u, 2); /* two blocks */
    wr(&f, AES_C3 + 0x90u, 1); /* DMA_ENABLE */
    wr(&f, AES_C3 + 0xB0u, 1); /* INT_ENA */
    wr(&f, AES_C3 + 0x48u, 1);
    CHECK_EQ(rd(&f, AES_C3 + 0x4Cu), 2); /* DONE */
    CHECK(rd(&f, INTR_STATUS_0 + 4u) & (1u << (51 - 32)));
    get_mem(&f, 0x3FC90100u, b, 32);
    {
        u8 want[32];
        unhex("7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2", want);
        CHECK(!memcmp(b, want, 32));
    }
    CHECK(aes_is(&f, AES_C3 + 0x50u, "5086cb9b507219ee95db113a917678b2")); /* the next IV */
    CHECK_EQ(rd(&f, 0x3FC90210u), 1u << 30 | 32u << 12 | 64u); /* the in list's EOF */
    wr(&f, AES_C3 + 0xB8u, 0); /* DMA_EXIT */
    CHECK_EQ(rd(&f, AES_C3 + 0x4Cu), 0);
    wr(&f, AES_C3 + 0xACu, 1); /* INT_CLEAR */
    CHECK(!(rd(&f, INTR_STATUS_0 + 4u) & (1u << (51 - 32))));
    /* CTR, the counter's last 32 bits counting */
    unhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", b);
    aes_words(&f, AES_C3 + 0x50u, b, 4);
    unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51", b);
    put_mem(&f, 0x3FC90000u, b, 32);
    desc(&f, 0x3FC90200u, 0x3FC90000u, 32);
    wr(&f, 0x3FC90210u, 1u << 31 | 64u);
    gdma_link(&f, 6, 0x3FC90200u, 0x3FC90210u);
    wr(&f, AES_C3 + 0x94u, 3); /* CTR */
    wr(&f, AES_C3 + 0x9Cu, 0); /* INC_SEL: 32 bits */
    wr(&f, AES_C3 + 0x48u, 1);
    get_mem(&f, 0x3FC90100u, b, 32);
    {
        u8 want[32];
        unhex("874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff", want);
        CHECK(!memcmp(b, want, 32));
    }
    CHECK(aes_is(&f, AES_C3 + 0x50u, "f0f1f2f3f4f5f6f7f8f9fafbfcfdff01"));
    c3_close(&f);
}

static void test_wifi_ccmp(void)
{
    /* the station's keys in the table (as hal_crypto_set_key_entry leaves
     * them: the pairwise key at entry 4, its kind 0): a protected frame
     * queued with the PLCP word's encryption leaves under CCMP, its PN the
     * driver's; a protected frame from the access point arrives decrypted
     * in place, its CCMP header and MIC kept; one under another key is
     * answered and dropped */
    enum { MAC = 0x60033000u, D_TX = 0x3FC90000u, B_TX = 0x3FC90100u, D1 = 0x3FC90200u, B1 = 0x3FC91000u };
    static const u8 me[6] = { 0x7c, 0xdf, 0xa1, 0x45, 0x6d, 0x62 }, ap[6] = { 0x02, 0xe5, 0, 0, 0, 0x02 };
    u8 tk[16], fr[80], plain[64], ct[96];
    uint64_t pn = 0;
    long n;
    size_t cl;
    c3 f;
    u32 i;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    memset(&wtx, 0, sizeof wtx);
    f.m.host.radio_tx = wifi_radio_tx;
    wr(&f, REGI2C + 0x150u, 37u << 20);
    for (i = 0; i < 16; i++)
        tk[i] = (u8)(0xA0u + i);
    wr(&f, MAC + 0x1400u + 160u, 0x0000e502u); /* entry 4: the access point's MAC */
    wr(&f, MAC + 0x1404u + 160u, 0x086c0200u); /* CCMP, pairwise */
    for (i = 0; i < 4; i++)
        wr(&f, MAC + 0x1408u + 160u + 4u * i,
           (u32)tk[4 * i] | (u32)tk[4 * i + 1] << 8 | (u32)tk[4 * i + 2] << 16 | (u32)tk[4 * i + 3] << 24);
    wr(&f, MAC + 0x814u, 1u << 4);
    wr(&f, MAC + 0x800u, 0x30103u); /* hal_crypto_enable */
    /* transmit: header, CCMP header (PN 3, key ID 0), 20 bytes, the MIC's 8 */
    memset(fr, 0, sizeof fr);
    fr[0] = 0x08;
    fr[1] = 0x41; /* to DS, Protected */
    memcpy(fr + 4, ap, 6);
    memcpy(fr + 10, me, 6);
    memset(fr + 16, 0xFF, 6);
    fr[24] = 3;
    fr[27] = 0x20;
    for (i = 0; i < 20; i++)
        fr[32 + i] = (u8)(i + 1);
    put_mem(&f, B_TX, fr, 60);
    wr(&f, D_TX, 0x80000000u | 0x40000000u | 64u << 12 | 64u);
    wr(&f, D_TX + 4u, B_TX);
    wr(&f, D_TX + 8u, 0);
    wr(&f, MAC + 0x12F8u, 64u | 4u << 17); /* the length; encryption */
    wr(&f, MAC + 0xD08u, 0xC0000000u | (D_TX & 0xFFFFFu));
    CHECK_EQ(wtx.n, 1);
    CHECK_EQ(wtx.len, 60);
    n = esk_ccmp_decrypt(tk, wtx.frame, 24, wtx.frame + 24, 36, plain, &pn);
    CHECK_EQ(n, 20);
    CHECK_EQ(pn, 3);
    CHECK(!memcmp(plain, fr + 32, 20));
    /* receive: from the access point, PN 7 */
    wr(&f, MAC + 0x40u, 0x45a1df7cu);
    wr(&f, MAC + 0x44u, 0x626du);
    wr(&f, MAC + 0x60u, 0xFFFFFFFFu);
    wr(&f, MAC + 0x64u, 0x1FFFFu);
    wr(&f, D1, 0x80000000u | 1600u);
    wr(&f, D1 + 4u, B1);
    wr(&f, D1 + 8u, 0);
    wr(&f, MAC + 0xC64u, 0x3FC00000u);
    wr(&f, MAC + 0x88u, D1);
    wr(&f, MAC + 0x84u, 0x80000000u);
    memset(fr, 0, 24);
    fr[0] = 0x08;
    fr[1] = 0x42; /* from DS, Protected */
    memcpy(fr + 4, me, 6);
    memcpy(fr + 10, ap, 6);
    memcpy(fr + 16, ap, 6);
    cl = esk_ccmp_encrypt(tk, fr, 24, 7, 0, plain, 20, ct);
    memcpy(fr + 24, ct, cl);
    tk[0] ^= 1; /* first under another key: answered, dropped */
    {
        u8 other[80];
        memcpy(other, fr, 24);
        esk_ccmp_encrypt(tk, fr, 24, 8, 0, plain, 20, other + 24);
        CHECK_EQ(f.m.radio.rx(f.m.radio.ctx, other, (u32)(24 + cl), 2437u, -40, 54000u, ESK_RADIO_WIFI), 1);
        CHECK_EQ(rd(&f, D1) >> 31, 1u); /* still the hardware's */
    }
    CHECK_EQ(f.m.radio.rx(f.m.radio.ctx, fr, (u32)(24 + cl), 2437u, -40, 54000u, ESK_RADIO_WIFI), 1);
    CHECK_EQ(rd(&f, D1) >> 30, 1u);
    CHECK_EQ((rd(&f, D1) >> 12) & 0xFFFu, 48u + 24u + 36u + 4u);
    get_mem(&f, B1 + 48u, fr, 60);
    CHECK(fr[24] == 7 && fr[27] == 0x20); /* the CCMP header kept */
    CHECK(!memcmp(fr + 32, plain, 20));   /* the payload in the clear */
    c3_close(&f);
}

static void test_radio_calibrations(void)
{
    /* the PHY's calibrations answer as an ideal radio (c3-radios.md step 2):
     * the BBPLL calibrated and locked, the RF PLL's calibration ended, the
     * power detector's reading, the DC and IQ estimates done, the RF PLL
     * finding the code it was given; the baseband, AGC, gain table and MAC
     * pages keep what is written; the MAC acknowledges hal_init */
    c3 f;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    wr(&f, REGI2C, 0x66u | 8u << 8 | 0x04000000u);
    CHECK_EQ((rd(&f, REGI2C) >> 16) & 0xffu, 0xC8u); /* OR_LOCK, OR_CAL_END, CAP 8 */
    wr(&f, REGI2C, 0x62u | 7u << 8 | 0x04000000u);
    CHECK_EQ((rd(&f, REGI2C) >> 16) & 0x02u, 0x02u);
    wr(&f, REGI2C + 0x5Cu, 0x0080016Au); /* the power detector: bit 19 clear */
    CHECK_EQ((rd(&f, REGI2C + 0x50u) >> 24) & 7u, 0u);
    wr(&f, REGI2C + 0x5Cu, 0x0088016Au); /* then set: the reading is done */
    CHECK_EQ((rd(&f, REGI2C + 0x50u) >> 24) & 7u, 7u);
    CHECK_EQ(rd(&f, REGI2C + 0x5Cu), 0x0088016Au);
    CHECK(rd(&f, REGI2C + 0x4Cu) & (1u << 24)); /* the DC estimate */
    wr(&f, REGI2C + 0x150u, 0x2Au << 20);       /* the RF PLL's code */
    wr(&f, REGI2C + 0x168u, 0xFFFFFFFFu);
    CHECK_EQ(rd(&f, REGI2C + 0x168u) >> 31, 0u); /* never busy */
    CHECK_EQ((rd(&f, REGI2C + 0x170u) >> 17) & 0x7Fu, 0x2Au);
    wr(&f, REGI2C + 0x130u, 0x13572468u); /* another analog control */
    CHECK_EQ(rd(&f, REGI2C + 0x130u), 0x13572468u);
    CHECK(rd(&f, 0x60006174u) & (1u << 16)); /* FE's IQ estimate */
    wr(&f, 0x6001D054u, 0x5u);               /* BB's BBPD_CTRL */
    wr(&f, 0x6001D03Cu, 0x3C3C3C3Cu);        /* a baseband register the SVD leaves out */
    wr(&f, 0x6001C02Cu, 0x32004E00u);        /* the AGC */
    wr(&f, 0x60011848u, 0x00A00000u);        /* the receive gain table */
    CHECK_EQ(rd(&f, 0x6001D054u), 0x5u);
    CHECK_EQ(rd(&f, 0x6001D03Cu), 0x3C3C3C3Cu);
    CHECK_EQ(rd(&f, 0x6001C02Cu), 0x32004E00u);
    CHECK_EQ(rd(&f, 0x60011848u), 0x00A00000u);
    CHECK_EQ(rd(&f, 0x60033D14u) & 1u, 0u); /* the MAC: hal_init's request */
    wr(&f, 0x60033D14u, 2u);
    CHECK_EQ(rd(&f, 0x60033D14u), 3u);
    wr(&f, 0x60035028u, 0x11u);
    CHECK_EQ(rd(&f, 0x60035028u), 0x11u);
    c3_close(&f);
}

static void test_cpu_reset_clears_pms(void)
{
    /* a software CPU reset (RTC_SW_CPU_RST) keeps the devices but clears the
     * permission control's locks, as ESP-IDF's esp_restart relies on */
    c3 f;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    wr(&f, 0x600C1000u, 1u); /* SENSITIVE ROM_TABLE_LOCK */
    wr(&f, REGI2C + 0x44u, 5u);
    wr(&f, 0x6001CCD4u, 7u);
    CHECK_EQ(rd(&f, 0x600C1000u), 1u);
    esk_esp32c3_reset(&f.m, 0x0C);
    CHECK(f.m.reset_cpu_only);
    CHECK_EQ(rd(&f, 0x600C1000u), 0u);
    CHECK_EQ(rd(&f, REGI2C + 0x44u), 5u);
    CHECK_EQ(rd(&f, 0x6001CCD4u), 7u);
    c3_close(&f);
}

static void test_peripheral_reset_keeps_rate(void)
{
    /* a peripheral's reset through SYSTEM (ESP-IDF resets SYSTIMER and the
     * timer groups as it starts) leaves its counters at their rate */
    c3 f;
    u32 a;
    int i;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    for (i = 0; i < 2; i++) {
        wr(&f, PERIP_RST_EN0, rd(&f, PERIP_RST_EN0) | 1u << 29 | 1u << 13);
        wr(&f, PERIP_RST_EN0, rd(&f, PERIP_RST_EN0) & ~(1u << 29 | 1u << 13));
    }
    a = st_unit0(&f);
    esk_mach_idle(&f.m, 20000u); /* 1 ms at 20 MHz */
    CHECK_EQ(st_unit0(&f) - a, 16000u);
    wr(&f, TG_WPROTECT, 0x50D83AA1u);
    wr(&f, TG_WDTCONFIG0, 0u);
    wr(&f, TG_T0CONFIG, 1u << 30 | 40u << 13 | 1u << 9);
    wr(&f, TG_T0LOADLO, 0u);
    wr(&f, TG_T0LOAD, 1u);
    wr(&f, TG_T0CONFIG, rd(&f, TG_T0CONFIG) | 1u << 31);
    esk_mach_idle(&f.m, 20000u);
    CHECK_EQ(tg_value(&f), 1000u);
    c3_close(&f);
}

#define RMT 0x60016000u
#define RMT_CONF0(n) (RMT + 0x10u + 4u * (n))
#define RMT_RX_CONF0(m) (RMT + 0x18u + 8u * (m))
#define RMT_RX_CONF1(m) (RMT + 0x1Cu + 8u * (m))
#define RMT_RX_STATUS(m) (RMT + 0x30u + 4u * (m))
#define RMT_INT_RAW (RMT + 0x38u)
#define RMT_INT_ENA (RMT + 0x40u)
#define RMT_INT_CLR (RMT + 0x44u)
#define RMT_DUTY(n) (RMT + 0x48u + 4u * (n))
#define RMT_TX_LIM(n) (RMT + 0x58u + 4u * (n))
#define RMT_RX_LIM(m) (RMT + 0x60u + 4u * (m))
#define RMT_SYS (RMT + 0x68u)
#define RMT_RAM(i) (RMT + 0x400u + 4u * (u32)(i))

static u64 edge_t[256];
static int edge_lv[256], nedge, edge_pin;
static struct esk_mach *edge_m;

static void h_edges(void *ctx, int port, int bit, const esk_mach_pin *cfg)
{
    int n = 16 * port + bit;
    h_pin_cfg(ctx, port, bit, cfg);
    if (n != edge_pin || nedge >= 256 || cfg->drive < 0)
        return;
    if (nedge && edge_lv[nedge - 1] == cfg->drive)
        return;
    edge_t[nedge] = edge_m->cycles - edge_m->output_lag;
    edge_lv[nedge++] = cfg->drive;
}

/* the RMT clocked from XTAL_CLK, 40 MHz, its sclk undivided: a channel's
 * DIV_CNT of 4 makes 100 ns ticks, 2 cycles of the CPU's 20 MHz */
static int rmt_open(c3 *f)
{
    if (!gpio_open(f))
        return 0;
    f->m.host.pin_cfg = h_edges;
    edge_m = &f->m;
    edge_pin = 8;
    nedge = 0;
    wr(f, SYSTEM + 0x10u, rd(f, SYSTEM + 0x10u) | 1u << 9); /* PERIP_CLK_EN0's RMT */
    wr(f, SYSTEM + 0x18u, rd(f, SYSTEM + 0x18u) & ~(1u << 9));
    wr(f, RMT_SYS, 3u << 24 | 1u << 26 | 1u); /* XTAL, active, direct access */
    wr(f, GPIO + 0x574u, 51u);                /* GPIO8 out: rmt_sig_out0 */
    wr(f, G_EN_W1TS, 1u << 8);
    CHECK_EQ(edge_lv[nedge - 1], 0); /* the idle output, low */
    nedge = 0;
    return 1;
}

static u32 sym(int l0, u32 p0, int l1, u32 p1) { return (u32)l0 << 15 | p0 | ((u32)l1 << 15 | p1) << 16; }

static void test_rmt_tx_rx(void)
{
    /* channel 0 sends high 3, low 5, high 2 ticks and an end marker on
     * GPIO8; channel 2 receives the pin back through the matrix */
    c3 f;
    u64 t0;
    if (!rmt_open(&f))
        return;
    wr(&f, GPIO + 0x220u, 1u << 6 | 8u); /* rmt_sig_in0 from GPIO8 */
    wr(&f, RMT_RAM(0), sym(1u, 3u, 0u, 5u));
    wr(&f, RMT_RAM(1), sym(1u, 2u, 0u, 0u));
    wr(&f, RMT_RX_CONF0(0), 4u | 20u << 8 | 1u << 23); /* /4, idle after 20 ticks */
    wr(&f, RMT_RX_LIM(0), 0x80u);
    wr(&f, RMT_RX_CONF1(0), 1u | 1u << 3 | 1u << 15); /* RX_EN, the RMT owns it, update */
    wr(&f, RMT_INT_ENA, 1u | 4u);
    wr(&f, RMT_CONF0(0), 4u << 8 | 1u << 16 | 1u << 24); /* /4, one block, update */
    CHECK_EQ(nedge, 0);
    wr(&f, RMT_CONF0(0), 4u << 8 | 1u << 16 | 1u << 24 | 1u); /* TX_START */
    esk_mach_idle(&f.m, 200u);
    CHECK_EQ(nedge, 4);
    t0 = edge_t[0];
    CHECK_EQ(edge_lv[0], 1);
    CHECK_EQ(edge_t[1] - t0, 6u);
    CHECK_EQ(edge_lv[1], 0);
    CHECK_EQ(edge_t[2] - t0, 16u);
    CHECK_EQ(edge_t[3] - t0, 20u);
    CHECK_EQ(edge_lv[3], 0); /* the marker's level */
    CHECK(rd(&f, RMT_INT_RAW) & 1u);     /* CH0_TX_END */
    CHECK(rd(&f, INTR_STATUS_0) & (1u << 28));
    CHECK(rd(&f, RMT_INT_RAW) & 4u);     /* CH2_RX_END, 20 ticks after */
    CHECK_EQ(rd(&f, RMT_RAM(96)), sym(1u, 3u, 0u, 5u));
    CHECK_EQ(rd(&f, RMT_RAM(97)), sym(1u, 2u, 0u, 0u));
    CHECK_EQ(rd(&f, RMT_RX_STATUS(0)) & 0x1ffu, 98u); /* MEM_WADDR_EX */
    wr(&f, RMT_INT_CLR, 5u);
    CHECK(!(rd(&f, INTR_STATUS_0) & (1u << 28)));
    /* the idle level as configured: IDLE_OUT_EN, high */
    wr(&f, RMT_CONF0(0), 4u << 8 | 1u << 16 | 1u << 5 | 1u << 6 | 1u << 24);
    CHECK_EQ(edge_lv[nedge - 1], 1);
    c3_close(&f);
}

static void test_rmt_loop_wrap_stop(void)
{
    /* continuous mode counting loops; wrap mode's threshold events; TX_STOP */
    c3 f;
    int i;
    if (!rmt_open(&f))
        return;
    wr(&f, RMT_RAM(0), sym(1u, 1u, 0u, 1u));
    wr(&f, RMT_RAM(1), sym(0u, 0u, 0u, 0u));
    wr(&f, RMT_TX_LIM(0), 0x80u | 3u << 9 | 1u << 19); /* three loops */
    wr(&f, RMT_CONF0(0), 4u << 8 | 1u << 16 | 1u << 3 | 1u << 24 | 1u);
    esk_mach_idle(&f.m, 9u); /* two loops of 4 cycles */
    CHECK_EQ(rd(&f, RMT_INT_RAW) & (1u << 12), 0u);
    esk_mach_idle(&f.m, 4u);
    CHECK(rd(&f, RMT_INT_RAW) & (1u << 12)); /* CH0_TX_LOOP */
    CHECK_EQ(rd(&f, RMT_INT_RAW) & 1u, 0u);  /* no TX_END: it goes on */
    wr(&f, RMT_CONF0(0), 4u << 8 | 1u << 16 | 1u << 3 | 1u << 7 | 1u << 24); /* TX_STOP */
    i = nedge;
    esk_mach_idle(&f.m, 100u);
    CHECK_EQ(nedge, i); /* stopped */
    CHECK_EQ(rd(&f, RMT_CONF0(0)) & (1u << 7), 0u); /* TX_STOP cleared itself */
    /* wrap mode: 48 words of one-tick pulses with no marker, TX_LIM 24:
     * an event every 24 words; the marker written in the event's place */
    wr(&f, RMT_INT_CLR, 0xffffu);
    for (i = 0; i < 48; i++)
        wr(&f, RMT_RAM(i), sym(1u, 1u, 0u, 1u));
    wr(&f, RMT_TX_LIM(0), 24u);
    wr(&f, RMT_CONF0(0), 4u << 8 | 1u << 16 | 1u << 4 | 1u << 24 | 1u);
    esk_mach_idle(&f.m, 24u * 4u + 1u);
    CHECK(rd(&f, RMT_INT_RAW) & (1u << 8)); /* CH0_TX_THR_EVENT */
    wr(&f, RMT_INT_CLR, 1u << 8);
    wr(&f, RMT_RAM(0), sym(1u, 1u, 0u, 0u)); /* the next lap ends at word 0 */
    esk_mach_idle(&f.m, 24u * 4u);
    CHECK(rd(&f, RMT_INT_RAW) & (1u << 8));
    CHECK_EQ(rd(&f, RMT_INT_RAW) & 1u, 0u);
    esk_mach_idle(&f.m, 4u);
    CHECK(rd(&f, RMT_INT_RAW) & 1u); /* the end, a whole lap later */
    /* without wrap mode the memory's end is an error */
    wr(&f, RMT_INT_CLR, 0xffffu);
    wr(&f, RMT_RAM(0), sym(1u, 1u, 0u, 1u));
    wr(&f, RMT_CONF0(0), 4u << 8 | 1u << 16 | 1u << 24 | 1u);
    esk_mach_idle(&f.m, 48u * 4u + 2u);
    CHECK(rd(&f, RMT_INT_RAW) & (1u << 4)); /* CH0_TX_ERR */
    c3_close(&f);
}

static void test_rmt_carrier_and_filter(void)
{
    /* a carrier of 2 sclk high, 2 low (20 MHz at 40 MHz... 10 MHz) on a
     * high entry of 2 ticks: 4 carrier cycles; then a receiver with a
     * filter drops a short pulse */
    c3 f;
    if (!rmt_open(&f))
        return;
    wr(&f, RMT_RAM(0), sym(1u, 2u, 0u, 1u));
    wr(&f, RMT_RAM(1), sym(0u, 0u, 0u, 0u));
    wr(&f, RMT_DUTY(0), 7u << 16 | 7u); /* 8 sclk high, 8 low: 400 ns */
    wr(&f, RMT_CONF0(0), 4u << 8 | 1u << 16 | 1u << 20 | 1u << 21 | 1u << 22 | 1u << 24 | 1u);
    esk_mach_idle(&f.m, 100u);
    /* 200 ns high data: the carrier's 200 ns high half, then low */
    CHECK_EQ(nedge, 2);
    CHECK_EQ(edge_t[1] - edge_t[0], 4u);
    wr(&f, RMT_RAM(0), sym(1u, 20u, 0u, 1u)); /* 2 us: five carrier cycles */
    nedge = 0;
    wr(&f, RMT_CONF0(0), 4u << 8 | 1u << 16 | 1u << 20 | 1u << 21 | 1u << 22 | 1u << 24 | 1u);
    esk_mach_idle(&f.m, 100u);
    CHECK_EQ(nedge, 10);
    CHECK_EQ(edge_t[2] - edge_t[0], 8u); /* 400 ns a cycle */
    c3_close(&f);
}

static void test_rmt_rx_filter(void)
{
    /* a 1-tick glitch inside a long low: RX_FILTER_THRES of 8 sclk (200 ns)
     * drops it, the low written whole */
    c3 f;
    if (!rmt_open(&f))
        return;
    wr(&f, GPIO + 0x220u, 1u << 6 | 8u);
    wr(&f, RMT_RAM(0), sym(1u, 10u, 0u, 6u));
    wr(&f, RMT_RAM(1), sym(1u, 1u, 0u, 6u));
    wr(&f, RMT_RAM(2), sym(1u, 10u, 0u, 0u));
    wr(&f, RMT_RX_CONF0(0), 4u | 30u << 8 | 1u << 23);
    wr(&f, RMT_RX_CONF1(0), 1u | 1u << 3 | 1u << 4 | 8u << 5 | 1u << 15);
    wr(&f, RMT_CONF0(0), 4u << 8 | 1u << 16 | 1u << 24 | 1u);
    esk_mach_idle(&f.m, 400u);
    CHECK(rd(&f, RMT_INT_RAW) & 4u);
    CHECK_EQ(rd(&f, RMT_RAM(96)), sym(1u, 10u, 0u, 13u));
    CHECK_EQ(rd(&f, RMT_RAM(97)), sym(1u, 10u, 0u, 0u));
    c3_close(&f);
}

#define LEDC 0x60019000u
#define LEDC_CH_CONF0(n) (LEDC + 0x14u * (n))
#define LEDC_CH_HPOINT(n) (LEDC + 0x04u + 0x14u * (n))
#define LEDC_CH_DUTY(n) (LEDC + 0x08u + 0x14u * (n))
#define LEDC_CH_CONF1(n) (LEDC + 0x0Cu + 0x14u * (n))
#define LEDC_CH_DUTY_R(n) (LEDC + 0x10u + 0x14u * (n))
#define LEDC_TIMER_CONF(x) (LEDC + 0xA0u + 8u * (x))
#define LEDC_TIMER_VALUE(x) (LEDC + 0xA4u + 8u * (x))
#define LEDC_INT_RAW (LEDC + 0xC0u)
#define LEDC_INT_CLR (LEDC + 0xCCu)
#define LEDC_CONF (LEDC + 0xD0u)

/* timer 0 on XTAL_CLK divided by 4: 100 ns counts, 2 CPU cycles; 16 counts
 * (DUTY_RES 4) a period; channel 0 on GPIO8 from HPOINT and a duty */
static int ledc_open(c3 *f, u32 hpoint, u32 duty)
{
    if (!gpio_open(f))
        return 0;
    f->m.host.pin_cfg = h_edges;
    edge_m = &f->m;
    edge_pin = 8;
    wr(f, SYSTEM + 0x10u, rd(f, SYSTEM + 0x10u) | 1u << 11); /* PERIP_CLK_EN0's LEDC */
    wr(f, SYSTEM + 0x18u, rd(f, SYSTEM + 0x18u) & ~(1u << 11));
    wr(f, LEDC_CONF, 3u | 1u << 31);
    wr(f, LEDC_TIMER_CONF(0), 1u << 23 | (4u << 8) << 4 | 4u | 1u << 25); /* held, /4, 4 bits */
    wr(f, LEDC_CH_HPOINT(0), hpoint);
    wr(f, LEDC_CH_DUTY(0), duty);
    wr(f, LEDC_CH_CONF1(0), 1u << 31 | 1u << 30 | 1u << 20 | 1u << 10); /* ESP-IDF's set_duty */
    wr(f, LEDC_CH_CONF0(0), 1u << 2 | 1u << 4);
    wr(f, GPIO + 0x574u, 45u); /* GPIO8 out: ledc_ls_sig_out0 */
    wr(f, G_EN_W1TS, 1u << 8);
    nedge = 0;
    wr(f, LEDC_TIMER_CONF(0), (4u << 8) << 4 | 4u); /* counting */
    return 1;
}

static void test_ledc_pwm(void)
{
    /* high from count 2 to count 7: 500 ns of every 1.6 us */
    c3 f;
    u64 t0;
    u32 v;
    if (!ledc_open(&f, 2u, 5u << 4))
        return;
    esk_mach_idle(&f.m, 100u);
    CHECK(nedge >= 6);
    t0 = edge_t[0];
    CHECK_EQ(edge_lv[0], 1);
    CHECK_EQ(edge_t[0] - edge_t[0], 0u);
    CHECK_EQ(edge_t[1] - t0, 10u);
    CHECK_EQ(edge_lv[1], 0);
    CHECK_EQ(edge_t[2] - t0, 32u);
    CHECK_EQ(edge_t[3] - t0, 42u);
    CHECK(rd(&f, LEDC_INT_RAW) & 1u);        /* LSTIMER0_OVF */
    CHECK(rd(&f, LEDC_INT_RAW) & (1u << 4)); /* the one-step fade ended */
    CHECK_EQ(rd(&f, LEDC_CH_DUTY_R(0)), 5u << 4);
    v = rd(&f, LEDC_TIMER_VALUE(0));
    CHECK(v < 16u);
    /* a new duty at the next overflow: 0 % stays low */
    wr(&f, LEDC_CH_DUTY(0), 0u);
    wr(&f, LEDC_CH_CONF0(0), 1u << 2 | 1u << 4);
    esk_mach_idle(&f.m, 64u);
    nedge = 0;
    esk_mach_idle(&f.m, 100u);
    CHECK_EQ(nedge, 0);
    CHECK_EQ(pins_out[8].drive, 0);
    /* 16 counts: 100 %, high throughout */
    wr(&f, LEDC_CH_DUTY(0), 16u << 4);
    wr(&f, LEDC_CH_CONF0(0), 1u << 2 | 1u << 4);
    esk_mach_idle(&f.m, 64u);
    nedge = 0;
    esk_mach_idle(&f.m, 100u);
    CHECK_EQ(nedge, 0);
    CHECK_EQ(pins_out[8].drive, 1);
    /* SIG_OUT_EN off: IDLE_LV */
    wr(&f, LEDC_CH_CONF0(0), 1u << 3 | 1u << 4);
    esk_mach_idle(&f.m, 64u);
    CHECK_EQ(pins_out[8].drive, 1);
    wr(&f, LEDC_CH_CONF0(0), 1u << 4);
    esk_mach_idle(&f.m, 64u);
    CHECK_EQ(pins_out[8].drive, 0);
    c3_close(&f);
}

static void test_ledc_dither_and_fade(void)
{
    /* DUTY 5 + 8/16: half the periods 6 counts high */
    c3 f;
    int i;
    u64 high = 0;
    if (!ledc_open(&f, 0u, 5u << 4 | 8u))
        return;
    esk_mach_idle(&f.m, 32u * 16u + 4u);
    CHECK(nedge >= 32);
    for (i = 0; i + 1 < 32; i += 2)
        high += edge_t[i + 1] - edge_t[i];
    CHECK_EQ(high, (16u * 5u + 8u) * 2u);
    c3_close(&f);
    /* a fade from 0 up by 2 every overflow, 3 steps: 6 */
    if (!ledc_open(&f, 0u, 0u))
        return;
    wr(&f, LEDC_INT_CLR, 0xffffu);
    wr(&f, LEDC_CH_CONF1(0), 1u << 31 | 1u << 30 | 3u << 20 | 1u << 10 | 2u);
    wr(&f, LEDC_CH_CONF0(0), 1u << 2 | 1u << 4);
    esk_mach_idle(&f.m, 32u * 2u + 4u);
    CHECK_EQ(rd(&f, LEDC_INT_RAW) & (1u << 4), 0u);
    esk_mach_idle(&f.m, 32u * 2u);
    CHECK(rd(&f, LEDC_INT_RAW) & (1u << 4)); /* DUTY_CHNG_END_LSCH0 */
    CHECK_EQ(rd(&f, LEDC_CH_DUTY_R(0)), 6u << 4);
    nedge = 0;
    esk_mach_idle(&f.m, 64u);
    CHECK(nedge >= 3);
    i = edge_lv[0] ? 0 : 1; /* from a rising edge */
    CHECK_EQ(edge_t[i + 1] - edge_t[i], 12u); /* 6 counts */
    /* the overflow counter: every third overflow */
    wr(&f, LEDC_INT_CLR, 0xffffu);
    wr(&f, LEDC_CH_CONF0(0), 1u << 2 | 1u << 15 | 2u << 5 | 1u << 16 | 1u << 4);
    esk_mach_idle(&f.m, 32u * 2u + 2u);
    CHECK_EQ(rd(&f, LEDC_INT_RAW) & (1u << 10), 0u);
    esk_mach_idle(&f.m, 32u);
    CHECK(rd(&f, LEDC_INT_RAW) & (1u << 10)); /* OVF_CNT_LSCH0 */
    c3_close(&f);
}

#define I2C 0x60013000u
#define I2C_SCL_LOW (I2C + 0x00u)
#define I2C_CTR (I2C + 0x04u)
#define I2C_SR (I2C + 0x08u)
#define I2C_TO (I2C + 0x0Cu)
#define I2C_SLAVE_ADDR (I2C + 0x10u)
#define I2C_FIFO_CONF (I2C + 0x18u)
#define I2C_DATA (I2C + 0x1Cu)
#define I2C_INT_RAW (I2C + 0x20u)
#define I2C_INT_CLR (I2C + 0x24u)
#define I2C_SDA_HOLD (I2C + 0x30u)
#define I2C_SDA_SAMPLE (I2C + 0x34u)
#define I2C_SCL_HIGH (I2C + 0x38u)
#define I2C_STRETCH (I2C + 0x84u)
#define I2C_COMD(i) (I2C + 0x58u + 4u * (u32)(i))
#define I2C_CMD(op, n, chk, exp, val) ((u32)(op) << 11 | (u32)(val) << 10 | (u32)(exp) << 9 | (u32)(chk) << 8 | (u32)(n))
#define I2C_SCL_PIN 4
#define I2C_SDA_PIN 5

/* a register device at 0x50 on the bus, as the test's own: its eight
 * registers written and read from a pointer the first written byte sets */
static struct {
    int scl, sda;      /* the lines */
    int drive_sda;     /* 0 pulls SDA low */
    int hold_scl;      /* 1 holds SCL low (stretching) */
    int state, bit, rw, first, acking;
    u32 byte, ptr;
    u8 mem[8];
    int starts, stops;
} dev;

static int i2c_line(int pin)
{
    const esk_mach_pin *o = &pins_out[pin];
    int mcu = o->drive == 0 ? 0 : 1;
    if (pin == I2C_SDA_PIN)
        return mcu && dev.drive_sda;
    return mcu && !dev.hold_scl;
}

static void dev_edges(void)
{
    int scl = i2c_line(I2C_SCL_PIN), sda = i2c_line(I2C_SDA_PIN);
    if (scl && dev.scl && sda != dev.sda) {
        if (!sda) { /* START */
            dev.state = 1;
            dev.bit = 0;
            dev.byte = 0;
            dev.starts++;
        } else { /* STOP */
            dev.state = 0;
            dev.stops++;
        }
    }
    if (scl && !dev.scl && dev.state) { /* SCL rises: read */
        if (dev.bit < 8 && (dev.state == 1 || dev.state == 2))
            dev.byte = dev.byte << 1 | (u32)sda;
        if (dev.bit == 8 && dev.state == 3 && sda)
            dev.state = 4; /* the master's NACK */
        dev.bit++;
    }
    if (!scl && dev.scl && dev.state) { /* SCL falls: write */
        if (dev.bit == 8 && (dev.state == 1 || dev.state == 2)) {
            int ack = 1;
            if (dev.state == 1) {
                if ((dev.byte >> 1) == 0x50u) {
                    ack = 0;
                    dev.rw = (int)(dev.byte & 1u);
                    dev.first = 1;
                }
            } else {
                ack = 0;
                if (dev.first)
                    dev.ptr = dev.byte & 7u;
                else
                    dev.mem[dev.ptr++ & 7u] = (u8)dev.byte;
                dev.first = 0;
            }
            dev.drive_sda = ack;
            dev.acking = !ack;
        } else if (dev.bit == 9) {
            dev.bit = 0;
            dev.byte = 0;
            dev.drive_sda = 1;
            if (dev.state == 1)
                dev.state = dev.acking ? (dev.rw ? 3 : 2) : 0; /* addressed, or not */
            dev.acking = 0;
            if (dev.state == 3)
                dev.byte = dev.mem[dev.ptr & 7u];
        }
        if (dev.state == 3 && dev.bit < 8)
            dev.drive_sda = (int)(((u32)dev.mem[dev.ptr & 7u] >> (u32)(7 - dev.bit)) & 1u);
        if (dev.state == 3 && dev.bit == 8) {
            dev.drive_sda = 1;
            dev.ptr++;
        }
    }
    dev.scl = scl;
    dev.sda = sda;
}

static void h_i2c_cfg(void *ctx, int port, int bit, const esk_mach_pin *cfg)
{
    int n = 16 * port + bit;
    h_edges(ctx, port, bit, cfg);
    if (n == I2C_SCL_PIN || n == I2C_SDA_PIN) {
        dev_edges();
        pins_ext[I2C_SDA_PIN] = dev.drive_sda ? -1 : 0;
        pins_ext[I2C_SCL_PIN] = dev.hold_scl ? 0 : -1;
        esk_esp32c3_pins_changed(edge_m);
    }
}

/* I2C0 on GPIO4 (SCL) and GPIO5 (SDA): open-drain, pulled up, through the
 * matrix; I2C_SCLK at XTAL_CLK's 40 MHz, SCL low 20 and high 20 cycles of
 * it (1 us a bit, 20 CPU cycles) */
static int i2c_open(c3 *f, int master)
{
    int i;
    if (!gpio_open(f))
        return 0;
    memset(&dev, 0, sizeof dev);
    dev.drive_sda = 1;
    dev.scl = dev.sda = 1;
    f->m.host.pin_cfg = master ? h_i2c_cfg : h_edges;
    edge_m = &f->m;
    edge_pin = I2C_SCL_PIN;
    wr(f, SYSTEM + 0x10u, rd(f, SYSTEM + 0x10u) | 1u << 7); /* PERIP_CLK_EN0's I2C_EXT0 */
    wr(f, SYSTEM + 0x18u, rd(f, SYSTEM + 0x18u) & ~(1u << 7));
    for (i = I2C_SCL_PIN; i <= I2C_SDA_PIN; i++) {
        wr(f, GPIO + 0x74u + 4u * (u32)i, 1u << 2); /* PINn: open-drain */
        wr(f, 0x60009004u + 4u * (u32)i, 1u << 12 | 1u << 9 | 1u << 8 | 2u << 10); /* IO MUX: GPIO, IE, WPU */
        wr(f, GPIO + 0x554u + 4u * (u32)i, i == I2C_SCL_PIN ? 53u : 54u);
        wr(f, GPIO + 0x154u + 4u * (i == I2C_SCL_PIN ? 53u : 54u), 1u << 6 | (u32)i);
    }
    wr(f, G_EN_W1TS, 1u << I2C_SCL_PIN | 1u << I2C_SDA_PIN);
    wr(f, I2C_CTR, (master ? 1u << 4 : 0u) | 1u << 8 | 3u | 1u << 9);
    wr(f, I2C_SCL_LOW, 19u);
    wr(f, I2C_SCL_HIGH, 8u << 9 | 10u); /* wait 9, high 11: 20 */
    wr(f, I2C_SDA_HOLD, 4u);
    wr(f, I2C_SDA_SAMPLE, 13u);
    wr(f, I2C + 0x40u, 9u); /* START hold */
    wr(f, I2C + 0x44u, 9u);
    wr(f, I2C + 0x48u, 9u);
    wr(f, I2C + 0x4Cu, 9u);
    wr(f, I2C_FIFO_CONF, rd(f, I2C_FIFO_CONF) | 1u << 12 | 1u << 13);
    wr(f, I2C_FIFO_CONF, rd(f, I2C_FIFO_CONF) & ~(1u << 12 | 1u << 13));
    wr(f, I2C_CTR, rd(f, I2C_CTR) | 1u << 11); /* CONF_UPGATE */
    nedge = 0;
    return 1;
}

static void test_i2c_master(void)
{
    /* a write of two registers, then reading them back with a repeated
     * START, to the test's device at 0x50; the bus's timing; a NACK */
    c3 f;
    u32 i, raw;
    if (!i2c_open(&f, 1))
        return;
    wr(&f, I2C_DATA, 0xA0u);
    wr(&f, I2C_DATA, 0x02u);
    wr(&f, I2C_DATA, 0x11u);
    wr(&f, I2C_DATA, 0x22u);
    wr(&f, I2C_COMD(0), I2C_CMD(6, 0, 0, 0, 0));
    wr(&f, I2C_COMD(1), I2C_CMD(1, 4, 1, 0, 0));
    wr(&f, I2C_COMD(2), I2C_CMD(2, 0, 0, 0, 0));
    wr(&f, I2C_INT_CLR, 0x3ffffu);
    wr(&f, I2C_CTR, rd(&f, I2C_CTR) | 1u << 5); /* TRANS_START */
    esk_mach_idle(&f.m, 20u * 50u);
    raw = rd(&f, I2C_INT_RAW);
    CHECK(raw & (1u << 7));  /* TRANS_COMPLETE */
    CHECK(raw & (1u << 9));  /* TRANS_START */
    CHECK(!(raw & (1u << 10))); /* no NACK */
    CHECK_EQ(dev.mem[2], 0x11u);
    CHECK_EQ(dev.mem[3], 0x22u);
    CHECK_EQ(dev.starts, 1);
    CHECK_EQ(dev.stops, 1);
    for (i = 0; i < 3; i++)
        CHECK(rd(&f, I2C_COMD(i)) & (1u << 31));
    CHECK_EQ(rd(&f, I2C_SR) & (1u << 4), 0u); /* the bus free */
    /* SCL: 36 edges (four bytes of nine clocks), a period of 20 cycles */
    /* SCL: its fall after the START, 36 clocks (four bytes of nine), its
     * rise for the STOP; a clock every 20 cycles, low for 10 */
    CHECK_EQ(nedge, 1 + 72 + 1);
    CHECK_EQ(edge_t[2] - edge_t[0], 20u);
    CHECK_EQ(edge_t[1] - edge_t[0], 10u);
    /* reading: a write of the pointer, a repeated START, two bytes */
    wr(&f, I2C_DATA, 0xA0u);
    wr(&f, I2C_DATA, 0x02u);
    wr(&f, I2C_DATA, 0xA1u);
    wr(&f, I2C_COMD(0), I2C_CMD(6, 0, 0, 0, 0));
    wr(&f, I2C_COMD(1), I2C_CMD(1, 2, 1, 0, 0));
    wr(&f, I2C_COMD(2), I2C_CMD(6, 0, 0, 0, 0));
    wr(&f, I2C_COMD(3), I2C_CMD(1, 1, 1, 0, 0));
    wr(&f, I2C_COMD(4), I2C_CMD(3, 1, 0, 0, 0));
    wr(&f, I2C_COMD(5), I2C_CMD(3, 1, 0, 0, 1));
    wr(&f, I2C_COMD(6), I2C_CMD(2, 0, 0, 0, 0));
    wr(&f, I2C_INT_CLR, 0x3ffffu);
    wr(&f, I2C_CTR, rd(&f, I2C_CTR) | 1u << 5);
    esk_mach_idle(&f.m, 20u * 60u);
    CHECK(rd(&f, I2C_INT_RAW) & (1u << 7));
    CHECK_EQ((rd(&f, I2C_SR) >> 8) & 0x3fu, 2u); /* RXFIFO_CNT */
    raw = rd(&f, I2C_DATA);
    CHECK_EQ(raw, 0x11u);
    raw = rd(&f, I2C_DATA);
    CHECK_EQ(raw, 0x22u);
    CHECK_EQ(dev.starts, 3);
    /* nobody at 0x51: NACK, a STOP, the commands stop */
    wr(&f, I2C_DATA, 0xA2u);
    wr(&f, I2C_DATA, 0x00u);
    wr(&f, I2C_COMD(0), I2C_CMD(6, 0, 0, 0, 0));
    wr(&f, I2C_COMD(1), I2C_CMD(1, 2, 1, 0, 0));
    wr(&f, I2C_COMD(2), I2C_CMD(2, 0, 0, 0, 0));
    wr(&f, I2C_INT_CLR, 0x3ffffu);
    wr(&f, I2C_CTR, rd(&f, I2C_CTR) | 1u << 5);
    esk_mach_idle(&f.m, 20u * 40u);
    raw = rd(&f, I2C_INT_RAW);
    CHECK(raw & (1u << 10)); /* NACK */
    CHECK(raw & (1u << 7));  /* the STOP after it */
    CHECK_EQ(rd(&f, I2C_SR) & 1u, 1u); /* RESP_REC: the NACK */
    CHECK_EQ(rd(&f, I2C_COMD(1)) & (1u << 31), 0u);
    c3_close(&f);
}

static void test_i2c_stretch_and_end(void)
{
    /* the device holds SCL low after the address: the master waits; then a
     * transfer in two command sequences through END */
    c3 f;
    u32 i, raw;
    if (!i2c_open(&f, 1))
        return;
    wr(&f, I2C_DATA, 0xA0u);
    wr(&f, I2C_DATA, 0x05u);
    wr(&f, I2C_COMD(0), I2C_CMD(6, 0, 0, 0, 0));
    wr(&f, I2C_COMD(1), I2C_CMD(1, 2, 1, 0, 0));
    wr(&f, I2C_COMD(2), I2C_CMD(4, 0, 0, 0, 0)); /* END */
    wr(&f, I2C_INT_CLR, 0x3ffffu);
    wr(&f, I2C_CTR, rd(&f, I2C_CTR) | 1u << 5);
    esk_mach_idle(&f.m, 20u * 30u);
    CHECK(rd(&f, I2C_INT_RAW) & (1u << 3)); /* END_DETECT */
    CHECK_EQ(pins_out[I2C_SCL_PIN].drive, 0); /* SCL held low */
    CHECK_EQ(dev.stops, 0);
    dev.hold_scl = 1; /* the device stretches the next clock */
    pins_ext[I2C_SCL_PIN] = 0;
    esk_mach_outside(&f.m);
    esk_esp32c3_pins_changed(&f.m);
    dev_edges();
    wr(&f, I2C_DATA, 0x33u);
    wr(&f, I2C_COMD(0), I2C_CMD(1, 1, 1, 0, 0));
    wr(&f, I2C_COMD(1), I2C_CMD(2, 0, 0, 0, 0));
    wr(&f, I2C_INT_CLR, 0x3ffffu);
    wr(&f, I2C_CTR, rd(&f, I2C_CTR) | 1u << 5);
    i = (u32)nedge;
    esk_mach_idle(&f.m, 20u * 20u);
    CHECK_EQ(rd(&f, I2C_INT_RAW) & (1u << 7), 0u); /* still waiting */
    dev.hold_scl = 0;
    pins_ext[I2C_SCL_PIN] = -1;
    esk_mach_outside(&f.m);
    esk_esp32c3_pins_changed(&f.m);
    dev_edges();
    esk_mach_idle(&f.m, 20u * 20u);
    raw = rd(&f, I2C_INT_RAW);
    CHECK(raw & (1u << 7));
    CHECK_EQ(dev.mem[5], 0x33u);
    CHECK((u32)nedge > i);
    c3_close(&f);
}

/* the test as a master, bit-banging GPIO4 and GPIO5 from outside: each
 * level 3 us (60 cycles) */
static void bb(c3 *f, int scl, int sda)
{
    pins_ext[I2C_SCL_PIN] = scl ? -1 : 0;
    pins_ext[I2C_SDA_PIN] = sda ? -1 : 0;
    esk_mach_outside(&f->m); /* as the runtime tells it a change from outside */
    esk_esp32c3_pins_changed(&f->m);
    esk_mach_idle(&f->m, 60u);
}

static int bb_sda(void) { return pins_out[I2C_SDA_PIN].drive == 0 || pins_ext[I2C_SDA_PIN] == 0 ? 0 : 1; }

/* a byte out MSB first; the ACK read back (0 ACK) */
static int bb_write(c3 *f, u32 b)
{
    int i, ack;
    for (i = 7; i >= 0; i--) {
        int bit = (int)((b >> i) & 1u);
        bb(f, 0, bit);
        bb(f, 1, bit);
        bb(f, 0, bit);
    }
    bb(f, 0, 1);
    bb(f, 1, 1);
    ack = bb_sda();
    bb(f, 0, 1);
    return ack;
}

static u32 bb_read(c3 *f, int nack)
{
    u32 b = 0;
    int i;
    for (i = 0; i < 8; i++) {
        bb(f, 0, 1);
        bb(f, 1, 1);
        b = b << 1 | (u32)bb_sda();
        bb(f, 0, 1);
    }
    bb(f, 0, nack);
    bb(f, 1, nack);
    bb(f, 0, nack);
    return b;
}

static void test_i2c_slave(void)
{
    /* as the slave at 0x28: a write of two bytes into the RX FIFO, a read
     * of the TX FIFO's two bytes, an address that is not its own */
    c3 f;
    u32 v;
    if (!i2c_open(&f, 0))
        return;
    wr(&f, I2C_SLAVE_ADDR, 0x28u);
    wr(&f, I2C_CTR, rd(&f, I2C_CTR) | 1u << 11);
    wr(&f, I2C_INT_CLR, 0x3ffffu);
    bb(&f, 1, 1);
    bb(&f, 1, 0); /* START */
    CHECK(rd(&f, I2C_INT_RAW) & (1u << 15)); /* DET_START */
    CHECK(rd(&f, I2C_SR) & (1u << 4));       /* BUS_BUSY */
    v = (u32)bb_write(&f, 0x50u);
    CHECK_EQ(v, 0u); /* ACKed */
    CHECK(rd(&f, I2C_SR) & (1u << 5));       /* SLAVE_ADDRESSED */
    v = (u32)bb_write(&f, 0x5Au);
    CHECK_EQ(v, 0u);
    v = (u32)bb_write(&f, 0xC3u);
    CHECK_EQ(v, 0u);
    bb(&f, 0, 0);
    bb(&f, 1, 0);
    bb(&f, 1, 1); /* STOP */
    CHECK(rd(&f, I2C_INT_RAW) & (1u << 7));
    CHECK_EQ(rd(&f, I2C_SR) & (1u << 4), 0u);
    v = rd(&f, I2C_DATA);
    CHECK_EQ(v, 0x5Au);
    v = rd(&f, I2C_DATA);
    CHECK_EQ(v, 0xC3u);
    /* a read */
    wr(&f, I2C_DATA, 0x81u);
    wr(&f, I2C_DATA, 0x7Eu);
    bb(&f, 1, 0);
    v = (u32)bb_write(&f, 0x51u);
    CHECK_EQ(v, 0u);
    CHECK(rd(&f, I2C_SR) & (1u << 1)); /* SLAVE_RW */
    v = bb_read(&f, 0);
    CHECK_EQ(v, 0x81u);
    v = bb_read(&f, 1);
    CHECK_EQ(v, 0x7Eu);
    CHECK(rd(&f, I2C_INT_RAW) & (1u << 10)); /* the master's NACK */
    bb(&f, 0, 0);
    bb(&f, 1, 0);
    bb(&f, 1, 1);
    /* another address: no ACK */
    bb(&f, 1, 0);
    v = (u32)bb_write(&f, 0x52u);
    CHECK_EQ(v, 1u);
    bb(&f, 0, 0);
    bb(&f, 1, 0);
    bb(&f, 1, 1);
    c3_close(&f);
}

#define SPI2 0x60024000u
#define GS_CMD (SPI2 + 0x00u)
#define GS_ADDR (SPI2 + 0x04u)
#define GS_CTRL (SPI2 + 0x08u)
#define GS_CLOCK (SPI2 + 0x0Cu)
#define GS_USER (SPI2 + 0x10u)
#define GS_USER1 (SPI2 + 0x14u)
#define GS_USER2 (SPI2 + 0x18u)
#define GS_MS_DLEN (SPI2 + 0x1Cu)
#define GS_MISC (SPI2 + 0x20u)
#define GS_DMA_CONF (SPI2 + 0x30u)
#define GS_INT_CLR (SPI2 + 0x38u)
#define GS_INT_RAW (SPI2 + 0x3Cu)
#define GS_W(i) (SPI2 + 0x98u + 4u * (u32)(i))
#define GS_CLK_GATE (SPI2 + 0xE8u)
#define GDMA 0x6003F000u
#define SPI_CLK_PIN 6
#define SPI_D_PIN 7
#define SPI_Q_PIN 2
#define SPI_CS_PIN 10

/* FSPID's level at each rising edge of FSPICLK */
static int spi_d_at_rise[256], nrise;

static void h_spi_cfg(void *ctx, int port, int bit, const esk_mach_pin *cfg)
{
    int was = pins_out[SPI_CLK_PIN].drive;
    h_edges(ctx, port, bit, cfg);
    if (16 * port + bit == SPI_CLK_PIN && was == 0 && cfg->drive == 1 && nrise < 256)
        spi_d_at_rise[nrise++] = pins_out[SPI_D_PIN].drive;
}

/* GP-SPI2 on XTAL_CLK divided by 4: 10 MHz, 2 CPU cycles a clock; FSPICLK
 * on GPIO6, FSPID GPIO7, FSPICS0 GPIO10 through the matrix, FSPIQ read
 * from GPIO7 (MOSI looped back) unless `q_pin` says otherwise */
static int spi_open(c3 *f, int q_pin)
{
    if (!gpio_open(f))
        return 0;
    f->m.host.pin_cfg = h_spi_cfg;
    edge_m = &f->m;
    edge_pin = SPI_CLK_PIN;
    nrise = 0;
    wr(f, SYSTEM + 0x10u, rd(f, SYSTEM + 0x10u) | 1u << 6); /* SPI2 */
    wr(f, SYSTEM + 0x18u, rd(f, SYSTEM + 0x18u) & ~(1u << 6));
    wr(f, SYSTEM + 0x14u, rd(f, SYSTEM + 0x14u) | 1u << 6); /* DMA */
    wr(f, SYSTEM + 0x1Cu, rd(f, SYSTEM + 0x1Cu) & ~(1u << 6));
    wr(f, 0x60009004u + 4u * SPI_CLK_PIN, 1u << 12 | 1u << 9 | 2u << 10); /* IO MUX: GPIO (not MTCK) */
    wr(f, 0x60009004u + 4u * SPI_D_PIN, 1u << 12 | 1u << 9 | 2u << 10);   /* not MTDO */
    wr(f, GPIO + 0x554u + 4u * SPI_CLK_PIN, 63u);
    wr(f, GPIO + 0x554u + 4u * SPI_D_PIN, 65u);
    wr(f, GPIO + 0x554u + 4u * SPI_CS_PIN, 68u);
    wr(f, GPIO + 0x154u + 4u * 64u, 1u << 6 | (u32)q_pin); /* FSPIQ in */
    wr(f, G_EN_W1TS, 1u << SPI_CLK_PIN | 1u << SPI_D_PIN | 1u << SPI_CS_PIN);
    wr(f, GS_CLK_GATE, 1u | 2u); /* clocks on, XTAL_CLK */
    wr(f, GS_CLOCK, 3u << 12 | 1u << 6 | 3u); /* N 3, H 1, L 3 */
    wr(f, GS_MISC, 0x3Eu);                   /* CS0 only */
    wr(f, GS_CMD, 1u << 23);                 /* SPI_UPDATE */
    nedge = 0;
    return 1;
}

static void test_spi2_full_duplex(void)
{
    /* 32 bits out of W0 and back into W8, in each SPI mode */
    c3 f;
    int mode;
    for (mode = 0; mode < 4; mode++) {
        u32 idle = mode >= 2;
        if (!spi_open(&f, SPI_D_PIN))
            return;
        wr(&f, GS_MISC, 0x3Eu | idle << 29);
        wr(&f, GS_USER, 1u | 1u << 27 | 1u << 28 | 1u << 24 | (u32)(mode == 1 || mode == 2) << 9);
        wr(&f, GS_MS_DLEN, 31u);
        wr(&f, GS_W(0), 0xA55A1234u);
        wr(&f, GS_W(8), 0u);
        nedge = 0;
        wr(&f, GS_CMD, 1u << 24); /* SPI_USR */
        CHECK_EQ(pins_out[SPI_CS_PIN].drive, 0); /* CS active, low */
        CHECK(rd(&f, GS_CMD) & (1u << 24));
        esk_mach_idle(&f.m, 200u);
        CHECK_EQ(rd(&f, GS_W(8)), 0xA55A1234u);
        CHECK_EQ(rd(&f, GS_CMD) & (1u << 24), 0u); /* done */
        CHECK(rd(&f, GS_INT_RAW) & (1u << 12));    /* TRANS_DONE */
        CHECK_EQ(pins_out[SPI_CS_PIN].drive, 1);
        CHECK_EQ(nedge, 64);                        /* 32 clocks */
        CHECK_EQ(edge_t[2] - edge_t[0], 2u);        /* 100 ns each */
        CHECK_EQ(pins_out[SPI_CLK_PIN].drive, (int)idle);
        c3_close(&f);
    }
}

static void test_spi2_half_duplex_quad(void)
{
    /* a quad I/O read: command 0xEB on FSPID, a 24-bit address on four
     * lines, 4 dummy clocks, then two bytes in on four lines from outside
     * (HD 0, WP 1, Q 0, D 1: each nibble 5) */
    c3 f;
    int i;
    u32 cmd = 0;
    if (!spi_open(&f, SPI_Q_PIN))
        return;
    wr(&f, GPIO + 0x154u + 4u * 65u, 1u << 6 | SPI_D_PIN); /* FSPID in */
    wr(&f, GPIO + 0x154u + 4u * 66u, 1u << 6 | 0x1Fu);      /* HD: 0 */
    wr(&f, GPIO + 0x154u + 4u * 67u, 1u << 6 | 0x1Eu);      /* WP: 1 */
    wr(&f, GPIO + 0x154u + 4u * 64u, 1u << 6 | 0x1Fu);      /* Q: 0 */
    pins_ext[SPI_D_PIN] = 1;
    esk_mach_outside(&f.m);
    esk_esp32c3_pins_changed(&f.m);
    wr(&f, GS_USER2, 7u << 28 | 0xEBu);
    wr(&f, GS_ADDR, 0x123456u << 8);
    wr(&f, GS_USER1, 23u << 27 | 3u);
    wr(&f, GS_CTRL, rd(&f, GS_CTRL) | 1u << 6 | 1u << 15); /* FADDR_QUAD, FREAD_QUAD */
    wr(&f, GS_USER, 1u << 31 | 1u << 30 | 1u << 29 | 1u << 28);
    wr(&f, GS_MS_DLEN, 15u);
    wr(&f, GS_W(0), 0u);
    wr(&f, GS_CMD, 1u << 24);
    esk_mach_idle(&f.m, 200u);
    CHECK(rd(&f, GS_INT_RAW) & (1u << 12));
    CHECK_EQ(rd(&f, GS_W(0)) & 0xffffu, 0x5555u);
    CHECK_EQ(nrise, 8 + 6 + 4 + 4); /* clocks: command, address, dummy, data */
    for (i = 0; i < 8; i++)
        cmd = cmd << 1 | (u32)spi_d_at_rise[i];
    CHECK_EQ(cmd, 0xEBu);
    c3_close(&f);
}

static void test_spi2_lsb_first(void)
{
    /* WR_BIT_ORDER: each byte LSB first; RD_BIT_ORDER back */
    c3 f;
    int i;
    u32 first = 0;
    if (!spi_open(&f, SPI_D_PIN))
        return;
    wr(&f, GS_CTRL, rd(&f, GS_CTRL) | 1u << 25 | 1u << 26);
    wr(&f, GS_USER, 1u | 1u << 27 | 1u << 28 | 1u << 24);
    wr(&f, GS_MS_DLEN, 7u);
    wr(&f, GS_W(0), 0x01u);
    wr(&f, GS_CMD, 1u << 24);
    esk_mach_idle(&f.m, 100u);
    for (i = 0; i < 8; i++)
        first |= (u32)spi_d_at_rise[i] << i;
    CHECK_EQ(spi_d_at_rise[0], 1); /* bit 0 first */
    CHECK_EQ(first, 0x01u);
    CHECK_EQ(rd(&f, GS_W(8)) & 0xffu, 0x01u);
    c3_close(&f);
}

static void test_spi2_dma(void)
{
    /* 100 bytes out of a GDMA list and back into another, MOSI looped back:
     * the receive descriptor written back with its length and suc_eof */
    c3 f;
    const u32 tx = 0x3FC90000u, rx = 0x3FC90100u, dtx = 0x3FC90200u, drx = 0x3FC90210u;
    u32 i, ok = 1;
    if (!spi_open(&f, SPI_D_PIN))
        return;
    for (i = 0; i < 100; i += 4)
        wr(&f, tx + i, (i + 1u) | (i + 2u) << 8 | (i + 3u) << 16 | (i + 4u) << 24);
    wr(&f, dtx, 1u << 31 | 1u << 30 | 100u << 12 | 100u); /* owner, suc_eof, length, size */
    wr(&f, dtx + 4u, tx);
    wr(&f, dtx + 8u, 0u);
    wr(&f, drx, 1u << 31 | 128u);
    wr(&f, drx + 4u, rx);
    wr(&f, drx + 8u, 0u);
    wr(&f, GDMA + 0xD0u, 1u); /* OUT_RST */
    wr(&f, GDMA + 0xD0u, 0u);
    wr(&f, GDMA + 0x70u, 1u); /* IN_RST */
    wr(&f, GDMA + 0x70u, 0u);
    wr(&f, GDMA + 0x100u, 0u); /* OUT_PERI_SEL: SPI2 */
    wr(&f, GDMA + 0xA0u, 0u);  /* IN_PERI_SEL */
    wr(&f, GDMA + 0xE0u, (dtx & 0xfffffu) | 1u << 21);
    wr(&f, GDMA + 0x80u, (drx & 0xfffffu) | 1u << 22);
    wr(&f, GDMA + 0x08u, 1u << 1); /* IN_SUC_EOF enabled */
    wr(&f, GS_DMA_CONF, 1u << 27 | 1u << 28);
    wr(&f, GS_USER, 1u | 1u << 27 | 1u << 28);
    wr(&f, GS_MS_DLEN, 100u * 8u - 1u);
    wr(&f, GS_CMD, 1u << 24);
    esk_mach_idle(&f.m, 2000u);
    CHECK(rd(&f, GS_INT_RAW) & (1u << 12));
    CHECK(rd(&f, GDMA + 0x00u) & (1u << 1));       /* IN_SUC_EOF */
    CHECK(rd(&f, GDMA + 0x00u) & (1u << 8));       /* OUT_TOTAL_EOF */
    CHECK(rd(&f, INTR_STATUS_0 + 4u) & (1u << (44 - 32)));
    CHECK_EQ(rd(&f, drx), 1u << 30 | 100u << 12 | 128u); /* written back */
    CHECK_EQ(rd(&f, GDMA + 0x88u), drx);           /* IN_SUC_EOF_DES_ADDR */
    for (i = 0; i < 100; i++) {
        u32 v = rd(&f, (rx + i) & ~3u) >> (8u * ((rx + i) & 3u)) & 0xffu;
        if (v != i + 1u)
            ok = 0;
    }
    CHECK(ok);
    c3_close(&f);
}

#define SARADC 0x60040000u
#define REGI2C_C3 0x6000E000u

static double pins_v[32];

static int h_pin_volts(void *ctx, int port, int bit, double *v)
{
    int n = 16 * port + bit;
    (void)ctx;
    if (pins_v[n] < 0)
        return 0;
    *v = pins_v[n];
    return 1;
}

static int adc_open(c3 *f)
{
    int i;
    if (!gpio_open(f))
        return 0;
    for (i = 0; i < 32; i++)
        pins_v[i] = -1;
    f->m.host.pin_volts = h_pin_volts;
    wr(f, SYSTEM + 0x10u, rd(f, SYSTEM + 0x10u) | 1u << 28); /* APB_SARADC */
    wr(f, SYSTEM + 0x18u, rd(f, SYSTEM + 0x18u) & ~(1u << 28));
    wr(f, SYSTEM + 0x14u, rd(f, SYSTEM + 0x14u) | 1u << 6);  /* DMA */
    wr(f, SYSTEM + 0x1Cu, rd(f, SYSTEM + 0x1Cu) & ~(1u << 6));
    return 1;
}

/* a one-time sample of `unit`'s channel `ch` at `atten`: its data */
static u32 adc_once(c3 *f, u32 unit, u32 ch, u32 atten)
{
    u32 sel = unit ? 1u << 30 : 1u << 31;
    wr(f, SARADC + 0x4Cu, 3u << 30);
    wr(f, SARADC + 0x20u, sel | (unit << 3 | ch) << 25 | atten << 23);
    wr(f, SARADC + 0x20u, sel | (unit << 3 | ch) << 25 | atten << 23 | 1u << 29);
    /* 25 SAR_CLK cycles: at reset APB_CLK is 20 MHz, ADC_CTRL_CLK a fifth
     * of it and SAR_CLK a quarter of that, 1 us: 500 CPU cycles */
    esk_mach_idle(&f->m, 490u);
    CHECK_EQ(rd(f, SARADC + 0x44u) & (unit ? 1u << 30 : 1u << 31), 0u);
    esk_mach_idle(&f->m, 20u);
    CHECK(rd(f, SARADC + 0x44u) & (unit ? 1u << 30 : 1u << 31)); /* ADCn_DONE */
    return rd(f, SARADC + (unit ? 0x30u : 0x2Cu)) & 0xfffu;
}

static void test_adc_oneshot(void)
{
    /* 0.55 V on GPIO2: half of Vref at 0 dB, an eighth at 12 dB; 2 V clips;
     * ENCAL_GND grounds the input; a threshold monitor */
    c3 f;
    u32 v;
    if (!adc_open(&f))
        return;
    pins_v[2] = 0.55;
    pins_v[5] = 1.0;
    v = adc_once(&f, 0, 2, 0);
    CHECK_EQ(v, 2048u);
    v = adc_once(&f, 0, 2, 3);
    CHECK_EQ(v, 512u);
    v = adc_once(&f, 1, 0, 2); /* ADC2: GPIO5, 6 dB */
    CHECK_EQ(v, 1861u); /* 1.0 x 0.5 / 1.1 x 4095 */
    pins_v[2] = 2.0;
    v = adc_once(&f, 0, 2, 0);
    CHECK_EQ(v, 4095u);
    wr(&f, REGI2C_C3, 0x69u | 7u << 8 | 1u << 5 << 16 | 1u << 24); /* ENCAL_GND */
    v = adc_once(&f, 0, 2, 0);
    CHECK_EQ(v, 0u);
    wr(&f, REGI2C_C3, 0x69u | 7u << 8 | 1u << 24);
    /* monitor 0 on ADC1 channel 2: high above 3000 */
    wr(&f, SARADC + 0x34u, 2u | 3000u << 5 | 100u << 18);
    wr(&f, SARADC + 0x3Cu, 1u << 31);
    v = adc_once(&f, 0, 2, 0);
    CHECK(rd(&f, SARADC + 0x44u) & (1u << 29)); /* THRES0_HIGH */
    c3_close(&f);
}

static void test_adc_scan_dma_and_tsens(void)
{
    /* the pattern table's one entry (ADC1, channel 2, 0 dB) sampled by the
     * timer into a GDMA list, its frame ended after four; then the
     * temperature sensor at 25 degC */
    c3 f;
    const u32 buf = 0x3FC90400u, dsc = 0x3FC90500u;
    u32 i, w;
    if (!adc_open(&f))
        return;
    pins_v[2] = 0.55;
    wr(&f, dsc, 1u << 31 | 64u);
    wr(&f, dsc + 4u, buf);
    wr(&f, dsc + 8u, 0u);
    wr(&f, GDMA + 0x70u, 1u);
    wr(&f, GDMA + 0x70u, 0u);
    wr(&f, GDMA + 0xA0u, 8u); /* IN_PERI_SEL: ADC */
    wr(&f, GDMA + 0x80u, (dsc & 0xfffffu) | 1u << 22);
    wr(&f, SARADC + 0x18u, 0x08u << 18);              /* cmd0: ADC1, ch 2, 0 dB */
    wr(&f, SARADC + 0x00u, rd(&f, SARADC + 0x00u) & ~(7u << 15)); /* one entry */
    wr(&f, SARADC + 0x50u, 1u << 31 | 4u);            /* APB_ADC_TRANS, EOF every 4 */
    wr(&f, SARADC + 0x04u, (rd(&f, SARADC + 0x04u) & ~(0xfffu << 12)) | 100u << 12 | 1u << 24);
    esk_mach_idle(&f.m, 1000u * 3u + 10u);
    CHECK_EQ(rd(&f, GDMA + 0x00u) & (1u << 1), 0u); /* three samples: no EOF */
    esk_mach_idle(&f.m, 1000u);
    CHECK(rd(&f, GDMA + 0x00u) & (1u << 1));        /* IN_SUC_EOF */
    CHECK_EQ((rd(&f, dsc) >> 12) & 0xfffu, 16u);    /* four words */
    for (i = 0; i < 4; i++) {
        w = rd(&f, buf + 4u * i);
        CHECK_EQ(w, 2048u | 2u << 13);
    }
    wr(&f, SARADC + 0x04u, rd(&f, SARADC + 0x04u) & ~(1u << 24));
    /* the temperature sensor: TSENS_DAC 15 (offset 0) */
    CHECK_EQ(rd(&f, SARADC + 0x58u) & 0xffu, 0u); /* off */
    wr(&f, SYSTEM + 0x14u, rd(&f, SYSTEM + 0x14u) | 1u << 10); /* TSENS_CLK_EN */
    wr(&f, SARADC + 0x58u, rd(&f, SARADC + 0x58u) | 1u << 22);  /* TSENS_PU */
    wr(&f, REGI2C_C3, 0x69u | 6u << 8 | 15u << 16 | 1u << 24);
    CHECK_EQ(rd(&f, SARADC + 0x58u) & 0xffu, 104u); /* (25 + 20.52) / 0.4386 */
    c3_close(&f);
}

static void test_tsens_own_clock(void)
{
    /* the temperature sensor's registers follow TSENS_CLK_EN, not
     * APB_SARADC's clock: ESP-IDF turns on only the first to use them, and
     * its PHY reads TSENS_CTRL2 with the second off */
    c3 f;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    wr(&f, SYSTEM + 0x10u, rd(&f, SYSTEM + 0x10u) & ~(1u << 28)); /* APB_SARADC off */
    wr(&f, SYSTEM + 0x14u, rd(&f, SYSTEM + 0x14u) & ~(1u << 10)); /* TSENS_CLK_EN off */
    wr(&f, SARADC + 0x5Cu, 0x8005u);
    CHECK_EQ(rd(&f, SARADC + 0x5Cu), 0u); /* its clock off: ignored, reads 0 */
    wr(&f, SYSTEM + 0x14u, rd(&f, SYSTEM + 0x14u) | 1u << 10);
    CHECK_EQ(rd(&f, SARADC + 0x5Cu), 0x4002u); /* its reset value */
    wr(&f, SARADC + 0x5Cu, 0x8005u);
    CHECK_EQ(rd(&f, SARADC + 0x5Cu), 0x8005u);
    CHECK_EQ(rd(&f, SARADC + 0x58u), 0x00018000u);
    wr(&f, SARADC + 0x00u, 0x12345678u); /* the ADC's own registers stay off */
    CHECK_EQ(rd(&f, SARADC + 0x00u), 0u);
    c3_close(&f);
}

#define USJ 0x60043000u

static void test_usb_serial_jtag(void)
{
    /* no host: the send buffer takes 64 bytes, then waits for good; a
     * flush does the same at once; SERIAL_IN_EMPTY is set from reset and
     * raises source 26 once enabled */
    c3 f;
    int i;
    if (!c3_open(&f, NULL, 0, NULL, 0))
        return;
    CHECK_EQ(rd(&f, USJ + 4u), 2u);
    CHECK_EQ(rd(&f, USJ + 8u), 8u);
    for (i = 0; i < 63; i++)
        wr(&f, USJ, (u32)'A');
    CHECK_EQ(rd(&f, USJ + 4u), 2u);
    wr(&f, USJ, (u32)'B');
    CHECK_EQ(rd(&f, USJ + 4u), 0u);
    CHECK_EQ(rd(&f, USJ), 0u); /* nothing from a host */
    esk_mach_reset(&f.m);
    CHECK_EQ(rd(&f, USJ + 4u), 2u);
    wr(&f, USJ, (u32)'C');
    wr(&f, USJ + 4u, 1u); /* WR_DONE */
    CHECK_EQ(rd(&f, USJ + 4u), 0u);
    CHECK_EQ(rd(&f, INTR_STATUS_0) & (1u << 26), 0u);
    wr(&f, USJ + 0x10u, 8u);
    CHECK_EQ(rd(&f, USJ + 0x0Cu), 8u);
    CHECK(rd(&f, INTR_STATUS_0) & (1u << 26));
    wr(&f, USJ + 0x14u, 8u);
    CHECK_EQ(rd(&f, USJ + 8u), 0u);
    CHECK_EQ(rd(&f, INTR_STATUS_0) & (1u << 26), 0u);
    c3_close(&f);
}

TEST_MAIN_BEGIN
RUN(test_csrs);
RUN(test_no_atomics);
RUN(test_counter);
RUN(test_counter_user_csrs);
RUN(test_trigger);
RUN(test_pmp_no_priority);
RUN(test_level_interrupt);
RUN(test_edge_interrupt);
RUN(test_threshold);
RUN(test_wfi_wakes);
RUN(test_rom_blank_flash);
RUN(test_rom_boots_image);
RUN(test_rom_bad_digest);
RUN(test_flash_commands);
RUN(test_cache_mmu);
RUN(test_sha);
RUN(test_pc_logging);
RUN(test_cpu_clock);
RUN(test_software_interrupt);
RUN(test_systimer);
RUN(test_timg_timer);
RUN(test_timg_wdt);
RUN(test_rtc_calibration);
RUN(test_efuse);
RUN(test_gpio_reset_and_output);
RUN(test_gpio_input_interrupt);
RUN(test_gpio_matrix_uart);
RUN(test_rtc_calibration_periodic);
RUN(test_regi2c);
RUN(test_rng_and_radio);
    RUN(test_radio_calibrations);
    RUN(test_wifi_mac);
    RUN(test_wifi_ccmp);
    RUN(test_sha_dma);
    RUN(test_aes);
    RUN(test_ble);
    RUN(test_ble_con);
RUN(test_usb_serial_jtag);
RUN(test_cpu_reset_clears_pms);
RUN(test_peripheral_reset_keeps_rate);
RUN(test_rmt_tx_rx);
RUN(test_rmt_loop_wrap_stop);
RUN(test_rmt_carrier_and_filter);
RUN(test_rmt_rx_filter);
RUN(test_ledc_pwm);
RUN(test_ledc_dither_and_fade);
RUN(test_i2c_master);
RUN(test_i2c_stretch_and_end);
RUN(test_i2c_slave);
RUN(test_spi2_full_duplex);
RUN(test_spi2_half_duplex_quad);
RUN(test_spi2_lsb_first);
RUN(test_spi2_dma);
RUN(test_adc_oneshot);
RUN(test_adc_scan_dma_and_tsens);
    RUN(test_tsens_own_clock);
TEST_MAIN_END
