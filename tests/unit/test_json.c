/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "util/json.h"
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

TEST_MAIN_BEGIN
esk_alloc_init(&A, NULL);
RUN(test_roundtrip);
RUN(test_errors);
RUN(test_build);
RUN(test_numbers);
CHECK_EQ(A.allocations, 0);
TEST_MAIN_END
