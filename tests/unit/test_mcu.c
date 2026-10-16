/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_mcu.c -- the MCU machine (SPEC-009): the Cortex-M core on its bus
 * running the committed test firmware (tests/firmware), its breakpoints and
 * its sleep. */
#include "harness.h"
#include "mcu/mach.h"

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

typedef struct console {
    char text[4096];
    size_t len;
} console;

static void con_out(void *ctx, int stream, int c)
{
    console *k = (console *)ctx;
    (void)stream;
    if (k->len + 1 < sizeof k->text) {
        k->text[k->len++] = (char)c;
        k->text[k->len] = 0;
    }
}

/* an STM32F103C8's memory and a Cortex-M3, with the image loaded */
static int machine(struct esk_mach *m, esk_alloc *a, const char *image, console *k)
{
    size_t len = 0;
    unsigned char *img = slurp(image, &len);
    struct esk_cpu *cpu = NULL;
    u32 entry = 0;
    char why[160];
    es_status st;
    if (!img) {
        FAIL(image);
        return 0;
    }
    esk_mach_init(m, a);
    m->reset_hz = 8000000u;
    m->host.ctx = k;
    m->host.out = con_out;
    CHECK_EQ(esk_bus_add_region(m, 0x08000000u, 64u * 1024u, ESK_MEM_FLASH), ES_OK);
    CHECK_EQ(esk_bus_add_mirror(&m->bus, 0, 0x08000000u), ES_OK);
    CHECK_EQ(esk_bus_add_region(m, 0x20000000u, 20u * 1024u, ESK_MEM_RAM), ES_OK);
    m->bus.rg[0].erased = 0xffffffffu;
    esk_bus_erase(&m->bus.rg[0]);
    CHECK_EQ(esk_cortexm_create(m, "cortex-m3", 4, &cpu), ES_OK);
    st = esk_mach_load_image(m, img, len, &entry, why, sizeof why);
    free(img);
    if (st != ES_OK) {
        FAIL(why);
        return 0;
    }
    esk_mach_reset(m);
    return 1;
}

static void test_hello(void)
{
    esk_alloc a;
    struct esk_mach m;
    console k;
    memset(&k, 0, sizeof k);
    esk_alloc_init(&a, NULL);
    if (!machine(&m, &a, "tests/firmware/hello/hello.elf", &k))
        return;
    esk_mach_run(&m, 100000000u, 0);
    CHECK_EQ(m.state, ESK_MACH_EXIT);
    CHECK_EQ(m.exit_status, 0);
    CHECK_STR(k.text, "hello from the Cortex-M3: fib(20) = 06765\n");
    CHECK(m.insns > 1000);
    CHECK(m.cycles >= m.insns);
    esk_mach_free(&m);
    CHECK_EQ(a.live_bytes, 0);
}

/* the same run cut into quanta, and with a breakpoint taken and resumed,
 * ends in the same state after the same cycles */
static void test_quanta_and_breakpoints(void)
{
    esk_alloc a;
    struct esk_mach m1, m2;
    console k1, k2;
    u32 pc;
    int stops = 0, guard;
    memset(&k1, 0, sizeof k1);
    memset(&k2, 0, sizeof k2);
    esk_alloc_init(&a, NULL);
    if (!machine(&m1, &a, "tests/firmware/hello/hello.elf", &k1) ||
        !machine(&m2, &a, "tests/firmware/hello/hello.elf", &k2))
        return;
    esk_mach_run(&m1, 100000000u, 0);
    /* a breakpoint on whatever instruction is 300 in, inside fib(), which
     * runs again and again */
    esk_mach_run(&m2, 100000u, 300);
    pc = m2.cpu->ops->pc(m2.cpu);
    CHECK_EQ(esk_mach_bp_add(&m2, pc), 0);
    m2.bp_skip = 1;
    for (guard = 0; guard < 100000 && m2.state == ESK_MACH_RUN; guard++) {
        esk_mach_run(&m2, 97, 0);
        if (m2.stop_bp) {
            stops++;
            CHECK_EQ(m2.cpu->ops->pc(m2.cpu), pc);
            m2.bp_skip = 1;
        }
    }
    CHECK(stops >= 1);
    CHECK_EQ(m2.state, ESK_MACH_EXIT);
    CHECK_EQ(m1.cycles, m2.cycles);
    CHECK_EQ(m1.insns, m2.insns);
    CHECK_STR(k1.text, k2.text);
    esk_mach_free(&m1);
    esk_mach_free(&m2);
}

TEST_MAIN_BEGIN
RUN(test_hello);
RUN(test_quanta_and_breakpoints);
TEST_MAIN_END
