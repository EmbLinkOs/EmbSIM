/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include <string.h>
#include "harness.h"
#include "util/match.h"
#include "util/json.h"
#include "util/diag.h"
#include "util/num.h"

static esk_alloc A;

static void test_roundtrip(void)
{
    const char *src = "{\"a\":1,\"b\":[true,false,null,\"x\\n\\u00e9\"],\"c\":{\"d\":-2.5e3},\"big\":\"18446744073709551615\"}";
    int line;
    const char *err;
    esk_json *j = esk_json_parse(&A, src, strlen(src), &line, &err);
    size_t len;
    char *out;
    CHECK(j != NULL);
    if (!j) return;
    CHECK_EQ(esk_json_get_int(j, "a", 0), 1);
    CHECK_EQ(esk_json_len(esk_json_get(j, "b")), 4);
    CHECK_STR(esk_json_str(esk_json_at(esk_json_get(j, "b"), 3)), "x\n\xc3\xa9");
    CHECK_NEAR(esk_json_get_num(esk_json_get(j, "c"), "d", 0), -2500.0, 1e-9);
    out = esk_json_format(&A, j, 0, &len);
    CHECK_STR(out, "{\"a\":1,\"b\":[true,false,null,\"x\\n\xc3\xa9\"],\"c\":{\"d\":-2.5e3},\"big\":\"18446744073709551615\"}");
    esk_free(&A, out, len + 1);
    esk_json_free(&A, j);
}

static void test_errors(void)
{
    int line = 0;
    const char *err = NULL;
    CHECK(esk_json_parse(&A, "{\"a\":}", 6, &line, &err) == NULL);
    CHECK(err != NULL);
    CHECK(esk_json_parse(&A, "[1,2", 4, &line, &err) == NULL);
    CHECK(esk_json_parse(&A, "{} x", 4, &line, &err) == NULL);
    CHECK(esk_json_parse(&A, "\n\n{\"a\":tru}", 11, &line, &err) == NULL);
    CHECK_EQ(line, 3);
}

static void test_build(void)
{
    esk_json *o = esk_json_new_object(&A);
    esk_json *arr = esk_json_new_array(&A);
    size_t len;
    char *out;
    esk_json_array_push(&A, arr, esk_json_new_integer(&A, 7));
    esk_json_object_set(&A, o, "k", arr);
    esk_json_set_u64_string(&A, o, "t", UINT64_MAX);
    esk_json_set_string(&A, o, "k2", "q\"");
    esk_json_set_string(&A, o, "k2", "r"); /* replace */
    out = esk_json_format(&A, o, 0, &len);
    CHECK_STR(out, "{\"k\":[7],\"t\":\"18446744073709551615\",\"k2\":\"r\"}");
    esk_free(&A, out, len + 1);
    esk_json_free(&A, o);
}

static void test_numbers(void)
{
    double v;
    es_time_t t;
    uint64_t u;
    CHECK(esk_parse_si("4.7k", &v) == 0); CHECK_NEAR(v, 4700, 1e-9);
    CHECK(esk_parse_si("100n", &v) == 0); CHECK_NEAR(v, 100e-9, 1e-18);
    CHECK(esk_parse_si("1meg", &v) == 0); CHECK_NEAR(v, 1e6, 1e-3);
    CHECK(esk_parse_si("2.2uF", &v) == 0); CHECK_NEAR(v, 2.2e-6, 1e-15);
    CHECK(esk_parse_si("10m", &v) == 0); CHECK_NEAR(v, 0.01, 1e-12);
    CHECK(esk_parse_si("abc", &v) != 0);
    CHECK(esk_parse_time("10ms", &t) == 0); CHECK(t == 10 * ES_PS_PER_MS);
    CHECK(esk_parse_time("1.5us", &t) == 0); CHECK(t == 1500000);
    CHECK(esk_parse_time("12345ps", &t) == 0); CHECK(t == 12345);
    CHECK(esk_parse_time("12345", &t) == 0); CHECK(t == 12345);
    CHECK(esk_parse_time("0.0001ps", &t) != 0);
    CHECK(esk_parse_time("2s", &t) == 0); CHECK(t == 2 * ES_PS_PER_S);
    CHECK(esk_parse_u64("0x08000000", &u) == 0); CHECK(u == 0x08000000u);
    CHECK(esk_parse_u64("64K", &u) == 0); CHECK(u == 65536);
    CHECK(esk_parse_u64("2M", &u) == 0); CHECK(u == 2u << 20);
    {
        char buf[32];
        esk_format_time(buf, sizeof buf, 12345678901ull);
        CHECK_STR(buf, "12.345 ms");
        esk_format_time(buf, sizeof buf, 1000);
        CHECK_STR(buf, "1 ns");
    }
}

/* the scenario matcher: the last match, its group, and what it refuses */
static void test_match(void)
{
    const char *t = "T=17.0 C\r\nT=25.25 C\r\nok";
    esk_match m;
    CHECK(esk_match_last("([0-9.]+) C", t, strlen(t), &m) == 1);
    CHECK(m.has_group && strncmp(t + m.group_start, "25.25", m.group_end - m.group_start) == 0);
    CHECK(esk_match_last("T=(\\d+)\\.\\d+ C", t, strlen(t), &m) == 1 && t[m.group_start] == '2');
    CHECK(esk_match_last("^T=", t, strlen(t), &m) == 1 && m.start == 0);
    CHECK(esk_match_last("ok$", t, strlen(t), &m) == 1);
    CHECK(esk_match_last("C$", t, strlen(t), &m) == 0);
    CHECK(esk_match_last("[^0-9 =T.C\r\n]+", t, strlen(t), &m) == 1 && !strncmp(t + m.start, "ok", 2));
    CHECK(esk_match_last("x?ok", t, strlen(t), &m) == 1);
    CHECK(esk_match_last("missing", t, strlen(t), &m) == 0);
    CHECK(esk_match_last("a|b", t, strlen(t), &m) == -1);
    CHECK(esk_match_last("a{2}", t, strlen(t), &m) == -1);
    CHECK(esk_match_last("((a))", t, strlen(t), &m) == -1);
    CHECK(esk_match_last("(ab)+", t, strlen(t), &m) == -1);
    CHECK(esk_match_last("[abc", t, strlen(t), &m) == -1);
}

static void test_french(void)
{
    char fr[512];
    /* a format's conversions carried into the French */
    CHECK(esk_diag_french("net.floating_input", "U1.PA0", "U1.PA0 sampled net N3 with no driver and no pull", fr,
                          sizeof fr));
    CHECK(!strcmp(fr, "U1.PA0 a échantillonné le réseau N3 sans pilote ni rappel"));
    /* the MCU runtime's component prefix, and a fragment translated */
    CHECK(esk_diag_french("mcu.clock_gated", "U1", "U1: a write at pc 0x08000124: USART2.CR1 is ignored while its clock is off (RCC.APB1ENR.USART2EN is 0)",
                          fr, sizeof fr));
    CHECK(!strcmp(fr, "U1 : une écriture à pc 0x08000124 : USART2.CR1 ignore l'écriture tant que son horloge est coupée (RCC.APB1ENR.USART2EN vaut 0)"));
    /* %% and floating conversions */
    CHECK(esk_diag_french("perf.hot_net", "N1", "N1 takes 93% of the analog steps: each of its 120 edges (1000 Hz) re-solves its 4-node island of N1 (embsim run --circuit-profile shows the rest)",
                          fr, sizeof fr));
    CHECK(!strncmp(fr, "N1 prend 93% des pas analogiques : chacun de ses 120 fronts (1000 Hz)", 69));
    /* a message with text appended after its format */
    CHECK(esk_diag_french("analog.no_convergence", "isl0", "transient did not converge; worst residuals: N1 (0.1, via R1)", fr,
                          sizeof fr));
    CHECK(!strcmp(fr, "transient n'a pas convergé ; pires résidus : N1 (0.1, via R1)"));
    /* a message passed through whole has no translation */
    CHECK(!esk_diag_french("schema.parse", NULL, "unexpected character", fr, sizeof fr));
    CHECK(!esk_diag_french("catalog.model_unsupported", NULL, "lib: why", fr, sizeof fr));
    CHECK(!esk_diag_french("no.such_code", NULL, "anything", fr, sizeof fr));
    /* a message cut at the record's length still finds its format */
    {
        char cutmsg[240];
        memset(cutmsg, 'x', sizeof cutmsg);
        memcpy(cutmsg, "B1.U1: no behavioural model yet for ", 36);
        cutmsg[239] = 0;
        CHECK(esk_diag_french("mcu.model_missing", "B1.U1", cutmsg, fr, sizeof fr));
        CHECK(!strncmp(fr, "B1.U1 : pas encore de modèle de comportement pour xxx", 54));
        cutmsg[100] = 0; /* a short one that does not reach its end is another message */
        CHECK(!esk_diag_french("mcu.model_missing", "B1.U1", cutmsg, fr, sizeof fr));
    }
    /* a short buffer is cut, never overrun */
    CHECK(esk_diag_french("io.read", NULL, "cannot read /a/very/long/path", fr, 8) && strlen(fr) == 7);
}

TEST_MAIN_BEGIN
esk_alloc_init(&A, NULL);
RUN(test_roundtrip);
RUN(test_match);
RUN(test_errors);
RUN(test_build);
RUN(test_numbers);
RUN(test_french);
CHECK_EQ(A.allocations, 0);
TEST_MAIN_END
