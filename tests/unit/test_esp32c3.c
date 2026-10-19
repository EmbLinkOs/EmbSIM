/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_esp32c3.c -- the ESP32-C3's ESP-RV core and its interrupt matrix
 * (ESP32-C3 TRM v1.4, chapters 1 and 8): hand-assembled RV32 programs at
 * the reset pc, run on the machine the catalog's description builds; then
 * its boot: Espressif's own ROM from the catalog booting the in-package
 * flash (an ESP image through SPI1, the SHA accelerator, the cache and
 * MMU), the flash's commands, the PC logging. */
#include "harness.h"
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
        cycles -= esk_mach_run(&f->m, cycles, 0);
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
    CHECK(strstr(uart_out, "Saved PC:0x4004c0") != NULL);
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

TEST_MAIN_BEGIN
RUN(test_csrs);
RUN(test_no_atomics);
RUN(test_counter);
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
TEST_MAIN_END
