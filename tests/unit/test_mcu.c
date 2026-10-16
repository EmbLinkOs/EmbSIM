/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_mcu.c -- the MCU machine (SPEC-009): the Cortex-M core on its bus
 * running the committed test firmware (tests/firmware), its breakpoints and
 * its sleep. */
#include "harness.h"
#include "embsim/session.h"
#include "mcu/build.h"
#include "util/json.h"

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

/* ---- the Blue Pill, from the catalog's descriptions --------------------- */

typedef struct board {
    struct esk_mach m;
    esk_alloc a;
    esk_json *mcu, *regs;
    char out[8192];      /* USART1's bytes */
    size_t nout;
    char log[8192];      /* the diagnostics and the LED, as EmbCC's stderr */
    size_t nlog;
    double led_ms[64];
    int nled, led_on, led_known;
    u64 chunk_cycles;    /* the machine's cycles when the chunk began */
    double chunk_ps, hz; /* the time then, and the core clock in the chunk */
    const char *input;
} board;

static double now_ms(const board *b)
{
    return (b->chunk_ps + (double)(b->m.cycles - b->chunk_cycles) * 1e12 / b->hz) / 1e9;
}

static void logf_(board *b, const char *s)
{
    size_t n = strlen(s);
    if (b->nlog + n + 1 < sizeof b->log) {
        memcpy(b->log + b->nlog, s, n + 1);
        b->nlog += n;
    }
}

static void b_uart(void *ctx, const char *unit, int c, u32 bit_cycles)
{
    board *b = (board *)ctx;
    (void)bit_cycles;
    if (!strcmp(unit, "USART1") && b->nout + 1 < sizeof b->out) {
        b->out[b->nout++] = (char)c;
        b->out[b->nout] = 0;
    }
}

static void b_diag(void *ctx, int sev, const char *code, const char *msg)
{
    char line[400];
    (void)sev;
    snprintf(line, sizeof line, "%s: %s\n", code, msg);
    logf_((board *)ctx, line);
}

static void b_pin_cfg(void *ctx, int port, int bit, const esk_mach_pin *cfg)
{
    board *b = (board *)ctx;
    int on;
    char line[80];
    if (port != 2 || bit != 13)
        return;
    on = cfg->drive == 0; /* the Blue Pill's LED is lit by a low PC13 */
    if (b->led_known && on == b->led_on)
        return;
    if (!b->led_known && !on) {
        b->led_known = 1;
        b->led_on = 0;
        return;
    }
    b->led_known = 1;
    b->led_on = on;
    if (b->nled < 64)
        b->led_ms[b->nled++] = now_ms(b);
    snprintf(line, sizeof line, "bluepill: LED %s (PC13)\n", on ? "on" : "off");
    logf_(b, line);
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

static int bluepill(board *b, const char *image)
{
    size_t len = 0;
    unsigned char *img;
    char why[200];
    u32 entry = 0;
    es_status st;
    memset(b, 0, sizeof *b);
    esk_alloc_init(&b->a, NULL);
    b->mcu = load_json(&b->a, "catalog/mcus/st/stm32f103c8/mcu.json");
    b->regs = load_json(&b->a, "catalog/mcus/st/stm32f103c8/registers.json");
    CHECK(b->mcu != NULL && b->regs != NULL);
    esk_mach_init(&b->m, &b->a);
    b->m.host.ctx = b;
    b->m.host.uart_tx = b_uart;
    b->m.host.diag = b_diag;
    b->m.host.pin_cfg = b_pin_cfg;
    st = esk_mach_build(&b->m, b->mcu, b->regs, 8000000u, why, sizeof why);
    if (st != ES_OK) {
        FAIL(why);
        return 0;
    }
    img = slurp(image, &len);
    if (!img) {
        FAIL(image);
        return 0;
    }
    st = esk_mach_load_image(&b->m, img, len, &entry, why, sizeof why);
    free(img);
    if (st != ES_OK) {
        FAIL(why);
        return 0;
    }
    esk_mach_reset(&b->m);
    return 1;
}

/* the runtime's loop, without the session: quanta at the core clock, the
 * serial input fed while the receiver has room */
static void run_board(board *b, u64 max_cycles)
{
    const char *in = b->input;
    struct esk_rx_port *rx = esk_mach_rx_find(&b->m, "USART1");
    while (b->m.state == ESK_MACH_RUN && b->m.cycles < max_cycles) {
        u64 used;
        b->hz = (double)esk_mach_core_hz(&b->m);
        b->chunk_cycles = b->m.cycles;
        if (in && *in && rx && rx->room(rx->ctx))
            rx->put(rx->ctx, *in++);
        used = esk_mach_run(&b->m, 2000, 0);
        b->chunk_ps += (double)used * 1e12 / b->hz;
        if (!used && b->m.asleep)
            break; /* nothing left that can wake it */
    }
}

static void board_free(board *b)
{
    esk_mach_free(&b->m);
    esk_json_free(&b->a, b->mcu);
    esk_json_free(&b->a, b->regs);
    CHECK_EQ(b->a.live_bytes, 0);
}

/* EmbCC's expected output with EmbSIM's documented deltas applied */
static char *expected(const char *rel)
{
    size_t len = 0;
    unsigned char *t = slurp(rel, &len);
    char *s;
    if (!t)
        return NULL;
    s = (char *)malloc(len + 1);
    memcpy(s, t, len);
    s[len] = 0;
    free(t);
    return s;
}

static void apply_deltas(char *text, const char *deltas_rel)
{
    esk_alloc a;
    esk_json *d;
    size_t i;
    esk_alloc_init(&a, NULL);
    d = load_json(&a, deltas_rel);
    CHECK(d != NULL);
    for (i = 0; d && i < esk_json_len(esk_json_get(d, "deltas")); i++) {
        const esk_json *x = esk_json_at(esk_json_get(d, "deltas"), i);
        const char *from = esk_json_get_str(x, "embcc", ""), *to = esk_json_get_str(x, "embsim", "");
        char *at = strstr(text, from);
        CHECK(at != NULL && strlen(from) == strlen(to));
        if (at)
            memcpy(at, to, strlen(to));
    }
    esk_json_free(&a, d);
}

static void test_bluepill_golden(void)
{
    board *b = (board *)calloc(1, sizeof *b);
    char *want;
    int i;
    if (!bluepill(b, "tests/firmware/bluepill-golden/bluepill.elf"))
        return;
    run_board(b, 200000000u);
    want = expected("tests/firmware/bluepill-golden/bluepill.txt");
    if (want) {
        apply_deltas(want, "tests/firmware/bluepill-golden/deltas.json");
        CHECK_STR(b->out, want);
        free(want);
    }
    printf("%s", b->log);
    /* the LED toggled by TIM2's update interrupt every 1 ms at 72 MHz, then
     * every 2 ms at 36 MHz */
    /* (the first two are the pin's set-up: on as an output, off by BSRR) */
    CHECK_EQ(b->nled, 10);
    for (i = 3; i < 8 && i < b->nled; i++)
        CHECK_NEAR(b->led_ms[i] - b->led_ms[i - 1], 1.0, 0.0005);
    for (i = 9; i < 10 && i < b->nled; i++)
        CHECK_NEAR(b->led_ms[i] - b->led_ms[i - 1], 2.0, 0.0005);
    CHECK(strstr(b->log, "mcu.clock_gated: a read at pc") != NULL);
    CHECK(strstr(b->log, "GPIOA.IDR reads as 0 while its clock is off (RCC.APB2ENR.IOPAEN is 0)") != NULL);
    CHECK(strstr(b->log, "USART2.BRR is ignored while its clock is off (RCC.APB1ENR.USART2EN is 0)") != NULL);
    CHECK(strstr(b->log, "mcu.bus_fault: a word write at 0x40024000") != NULL);
    board_free(b);
    free(b);
}

/* USART1's receiver reads the serial input */
static void test_bluepill_input(void)
{
    board *b = (board *)calloc(1, sizeof *b);
    if (!bluepill(b, "tests/firmware/bluepill-golden/bluepill.elf"))
        return;
    b->input = "hi there\nnot this\n";
    run_board(b, 200000000u);
    CHECK(strstr(b->out, "rx 'hi there'\n") != NULL);
    board_free(b);
    free(b);
}

/* ---- the runtime in a session ------------------------------------------------ */

static esk_json *rpc(es_session *s, esk_alloc *a, const char *method, const char *params)
{
    char req[1024], *res = NULL;
    size_t len = 0;
    esk_json *j = NULL, *r = NULL;
    int line = 0;
    const char *err = NULL;
    snprintf(req, sizeof req, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":%s}", method, params);
    if (es_session_rpc(s, req, strlen(req), &res, &len) != ES_OK)
        return NULL;
    j = esk_json_parse(a, res, len, &line, &err);
    es_free(res);
    if (j && esk_json_get(j, "result"))
        r = esk_json_clone(a, esk_json_get(j, "result"));
    if (j && esk_json_get(j, "error"))
        fprintf(stderr, "%s: %s\n", method, esk_json_get_str(esk_json_get(j, "error"), "message", "?"));
    esk_json_free(a, j);
    return r;
}

/* the text a serial probe has seen */
static void serial_text(es_session *s, esk_alloc *a, const char *probe, char *out, size_t cap)
{
    char params[128];
    esk_json *r;
    snprintf(params, sizeof params, "{\"probe\":\"%s\"}", probe);
    r = rpc(s, a, "probe.read", params);
    snprintf(out, cap, "%s", r ? esk_json_get_str(r, "text", "") : "");
    esk_json_free(a, r);
}

static void mcu_output(es_session *s, esk_alloc *a, const char *key, char *out, size_t cap)
{
    esk_json *r = rpc(s, a, "observe.snapshot", "{\"include\":[\"components\"]}");
    const esk_json *u1 = esk_json_get(esk_json_get(r, "components"), "B1.U1");
    const esk_json *o = esk_json_get(u1, "outputs") ? esk_json_get(u1, "outputs") : u1;
    const esk_json *v = esk_json_get(o, key);
    if (esk_json_str(v))
        snprintf(out, cap, "%s", esk_json_str(v));
    else
        snprintf(out, cap, "%lld", (long long)esk_json_get_int(o, key, -1));
    esk_json_free(a, r);
}

static void test_session_runtime(void)
{
    es_session *s = NULL;
    esk_alloc a;
    char path[1024], text[4096], first[4096], insns[64], insns2[64], state[32];
    esk_json *r;
    esk_alloc_init(&a, NULL);
    snprintf(path, sizeof path, "%s/examples/bluepill-button-led/project.json", EMBSIM_SOURCE_DIR);
    CHECK_EQ(es_session_create(NULL, &s), ES_OK);
    if (!s)
        return;
    CHECK_EQ(es_session_load_file(s, path), ES_OK);
    esk_json_free(&a, rpc(s, &a, "control.reset", "{}"));
    r = rpc(s, &a, "probe.add", "{\"target\":\"serial:B1.USART1\",\"kind\":\"serial\"}");
    CHECK(r != NULL);
    esk_json_free(&a, r);
    esk_json_free(&a, rpc(s, &a, "control.advance", "{\"until\":\"2ms\"}"));
    /* the board's console input: USART1's receiver, at its baud rate */
    esk_json_free(&a, rpc(s, &a, "input.serial", "{\"target\":\"B1\",\"text\":\"abc\"}"));
    esk_json_free(&a, rpc(s, &a, "control.advance", "{\"until\":\"3ms\"}"));
    serial_text(s, &a, "console", text, sizeof text);
    CHECK(strstr(text, "EmbSIM Blue Pill demo\r\nABC") != NULL);
    mcu_output(s, &a, "state", state, sizeof state);
    CHECK_STR(state, "sleeping");
    /* the reset button holds NRST low: the core is held, then boots again */
    esk_json_free(&a, rpc(s, &a, "input.press", "{\"target\":\"B1.SW_RESET\"}"));
    esk_json_free(&a, rpc(s, &a, "control.advance", "{\"until\":\"3.5ms\"}"));
    mcu_output(s, &a, "state", state, sizeof state);
    CHECK_STR(state, "reset");
    esk_json_free(&a, rpc(s, &a, "input.release", "{\"target\":\"B1.SW_RESET\"}"));
    esk_json_free(&a, rpc(s, &a, "control.advance", "{\"until\":\"6ms\"}"));
    serial_text(s, &a, "console", text, sizeof text);
    CHECK(strstr(text, "ABCEmbSIM Blue Pill demo\r\n") != NULL);
    mcu_output(s, &a, "instructions", insns, sizeof insns);
    snprintf(first, sizeof first, "%s", text);
    /* a session reset runs the same again, to the instruction */
    esk_json_free(&a, rpc(s, &a, "control.reset", "{}"));
    esk_json_free(&a, rpc(s, &a, "control.advance", "{\"until\":\"2ms\"}"));
    esk_json_free(&a, rpc(s, &a, "input.serial", "{\"target\":\"B1.USART1\",\"text\":\"abc\"}"));
    esk_json_free(&a, rpc(s, &a, "control.advance", "{\"until\":\"3ms\"}"));
    esk_json_free(&a, rpc(s, &a, "input.press", "{\"target\":\"B1.SW_RESET\"}"));
    esk_json_free(&a, rpc(s, &a, "control.advance", "{\"until\":\"3.5ms\"}"));
    esk_json_free(&a, rpc(s, &a, "input.release", "{\"target\":\"B1.SW_RESET\"}"));
    esk_json_free(&a, rpc(s, &a, "control.advance", "{\"until\":\"6ms\"}"));
    serial_text(s, &a, "console", text, sizeof text);
    CHECK_STR(text, first);
    mcu_output(s, &a, "instructions", insns2, sizeof insns2);
    CHECK_STR(insns2, insns);
    es_session_destroy(s);
}

/* one run of the example with its inputs scheduled ahead; with `bp`, a
 * breakpoint on the EXTI handler stops it and it is continued each time */
typedef struct run_result {
    char console[2048], terminal[2048], pc13[4096], insns[32], cycles[32];
    int stops;
} run_result;

static void example_run(run_result *out, int bp)
{
    es_session *s = NULL;
    esk_alloc a;
    char path[1024];
    esk_json *r;
    int guard;
    memset(out, 0, sizeof *out);
    esk_alloc_init(&a, NULL);
    snprintf(path, sizeof path, "%s/examples/bluepill-button-led/project.json", EMBSIM_SOURCE_DIR);
    if (es_session_create(NULL, &s) != ES_OK || es_session_load_file(s, path) != ES_OK) {
        FAIL("the example does not load");
        es_session_destroy(s);
        return;
    }
    esk_json_free(&a, rpc(s, &a, "control.reset", "{}"));
    esk_json_free(&a, rpc(s, &a, "input.set", "{\"target\":\"SW1\",\"input\":\"pressed\",\"value\":true,\"at\":\"3ms\"}"));
    esk_json_free(&a, rpc(s, &a, "input.set", "{\"target\":\"SW1\",\"input\":\"pressed\",\"value\":false,\"at\":\"5ms\"}"));
    esk_json_free(&a, rpc(s, &a, "input.set", "{\"target\":\"VT1\",\"input\":\"tx_text\",\"value\":\"ok\",\"at\":\"6ms\"}"));
    if (bp) {
        r = rpc(s, &a, "debug.breakpoint", "{\"board\":\"B1\",\"op\":\"add\",\"symbol\":\"EXTI0_IRQHandler\"}");
        CHECK(r != NULL);
        esk_json_free(&a, r);
    }
    for (guard = 0; guard < 10; guard++) {
        const esk_json *stopped;
        r = rpc(s, &a, "control.advance", "{\"until\":\"8ms\"}");
        stopped = esk_json_get(r, "stopped");
        if (!stopped) {
            CHECK_STR(r ? esk_json_get_str(r, "now_ps", "") : "", "8000000000");
            esk_json_free(&a, r);
            break;
        }
        out->stops++;
        CHECK_STR(esk_json_get_str(stopped, "function", ""), "EXTI0_IRQHandler");
        CHECK_STR(esk_json_get_str(stopped, "reason", ""), "breakpoint");
        esk_json_free(&a, r);
        /* the registers and memory are there to look at */
        r = rpc(s, &a, "debug.registers", "{\"board\":\"B1\"}");
        CHECK(r != NULL && esk_json_len(esk_json_get(r, "regs")) >= 17);
        esk_json_free(&a, r);
        r = rpc(s, &a, "debug.disassemble", "{\"board\":\"B1\",\"count\":4}");
        CHECK(r != NULL && esk_json_len(esk_json_get(r, "insns")) == 4);
        CHECK_STR(esk_json_get_str(esk_json_at(esk_json_get(r, "insns"), 0), "label", ""), "EXTI0_IRQHandler");
        esk_json_free(&a, r);
    }
    serial_text(s, &a, "console", out->console, sizeof out->console);
    serial_text(s, &a, "terminal", out->terminal, sizeof out->terminal);
    mcu_output(s, &a, "instructions", out->insns, sizeof out->insns);
    mcu_output(s, &a, "cycles", out->cycles, sizeof out->cycles);
    /* PC13's waveform as its changes (a stop adds samples, not changes) */
    r = rpc(s, &a, "probe.read", "{\"probe\":\"pc13\"}");
    if (r) {
        const esk_json *sm = esk_json_get(r, "samples");
        size_t i, n = 0;
        int64_t last = -1;
        for (i = 0; i < esk_json_len(sm); i++) {
            const esk_json *e = esk_json_at(sm, i);
            const esk_json *val = esk_json_at(e, 1);
            int64_t v = val && val->type == ESK_JSON_NUMBER ? (int64_t)val->u.number.value : -1;
            if (v == last)
                continue;
            last = v;
            n += (size_t)snprintf(out->pc13 + n, sizeof out->pc13 - n, "%s=%lld ",
                                  esk_json_str(esk_json_at(e, 0)) ? esk_json_str(esk_json_at(e, 0)) : "?", (long long)v);
            if (n >= sizeof out->pc13)
                break;
        }
        esk_json_free(&a, r);
    }
    es_session_destroy(s);
}

/* M1's exit criterion: a breakpoint-and-continue run equals an uninterrupted one */
static void test_breakpoint_continue_equivalence(void)
{
    run_result *plain = (run_result *)calloc(1, sizeof *plain), *stopped = (run_result *)calloc(1, sizeof *stopped);
    example_run(plain, 0);
    example_run(stopped, 1);
    CHECK_EQ(plain->stops, 0);
    CHECK_EQ(stopped->stops, 2); /* the press and the release */
    CHECK(strstr(plain->console, "pressed\r\nreleased\r\n") != NULL);
    CHECK_STR(stopped->console, plain->console);
    CHECK_STR(stopped->terminal, plain->terminal);
    CHECK_STR(stopped->insns, plain->insns);
    CHECK_STR(stopped->cycles, plain->cycles);
    CHECK(strlen(plain->pc13) > 10);
    CHECK_STR(stopped->pc13, plain->pc13);
    free(plain);
    free(stopped);
}

/* source-level debugging from the image's DWARF line tables */
static void test_source_lines(void)
{
    es_session *s = NULL;
    esk_alloc a;
    char path[1024];
    esk_json *r;
    esk_alloc_init(&a, NULL);
    snprintf(path, sizeof path, "%s/examples/bluepill-button-led/project.json", EMBSIM_SOURCE_DIR);
    if (es_session_create(NULL, &s) != ES_OK || es_session_load_file(s, path) != ES_OK) {
        FAIL("the example does not load");
        es_session_destroy(s);
        return;
    }
    esk_json_free(&a, rpc(s, &a, "control.reset", "{}"));
    esk_json_free(&a, rpc(s, &a, "input.set", "{\"target\":\"SW1\",\"input\":\"pressed\",\"value\":true,\"at\":\"3ms\"}"));
    /* the handler's led(pressed) line */
    r = rpc(s, &a, "debug.breakpoint", "{\"board\":\"B1\",\"op\":\"add\",\"file\":\"main.c\",\"line\":68}");
    CHECK(r != NULL && esk_json_len(esk_json_get(r, "addresses")) >= 1);
    esk_json_free(&a, r);
    r = rpc(s, &a, "control.advance", "{\"until\":\"8ms\"}");
    CHECK(r && esk_json_get(r, "stopped"));
    if (r && esk_json_get(r, "stopped")) {
        const esk_json *w = esk_json_get(r, "stopped");
        CHECK_STR(esk_json_get_str(w, "function", ""), "EXTI0_IRQHandler");
        CHECK(strstr(esk_json_get_str(w, "file", ""), "main.c") != NULL);
        CHECK_EQ(esk_json_get_int(w, "line", 0), 68);
    }
    esk_json_free(&a, r);
    r = rpc(s, &a, "debug.source", "{\"board\":\"B1\",\"file\":\"main.c\"}");
    CHECK(r != NULL && strstr(esk_json_get_str(r, "text", ""), "void EXTI0_IRQHandler(void)") != NULL);
    CHECK(r != NULL && esk_json_len(esk_json_get(r, "lines")) > 10);
    esk_json_free(&a, r);
    es_session_destroy(s);
}

TEST_MAIN_BEGIN
RUN(test_hello);
RUN(test_quanta_and_breakpoints);
RUN(test_bluepill_golden);
RUN(test_bluepill_input);
RUN(test_session_runtime);
RUN(test_breakpoint_continue_equivalence);
RUN(test_source_lines);
TEST_MAIN_END
