/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "analog/analog.h"
#include "session/session_priv.h"

static double measured(esk_probe *p, const char *kind, es_time_t from, es_time_t to)
{
    double result = NAN;
    char why[160];
    CHECK(esk_probe_window(p, kind, from, to, to, &result, why, sizeof why));
    return result;
}

static void test_weighted_pulses(void)
{
    es_sample_t samples[] = {{0, 1}, {250, 0}, {1000, 1}, {1250, 0}, {2000, 1}};
    esk_probe p;
    memset(&p, 0, sizeof p);
    p.kind = ESK_PROBE_DIGITAL;
    p.ring = samples; p.len = p.cap = 5;
    CHECK_NEAR(measured(&p, "mean", 0, 2000), .25, 1e-12);
    CHECK_NEAR(measured(&p, "rms", 0, 2000), .5, 1e-12);
    CHECK_NEAR(measured(&p, "duty_cycle", 125, 1125), .25, 1e-12);
    CHECK_NEAR(measured(&p, "duty_cycle", 500, 900), 0, 1e-12);
    CHECK_NEAR(measured(&p, "duty_cycle", 0, 250), 1, 1e-12);
    p.kind = ESK_PROBE_VALUE;
    samples[0].value = samples[2].value = samples[4].value = .02;
    CHECK_NEAR(measured(&p, "mean", 0, 2000), .005, 1e-12);
    CHECK_NEAR(measured(&p, "rms", 0, 2000), .01, 1e-12);
}

static void test_invalid_and_wrapped_history(void)
{
    es_sample_t samples[] = {{1000, 1}, {1250, 0}, {0, 1}, {250, 0}};
    esk_probe p;
    double result;
    char why[160];
    memset(&p, 0, sizeof p);
    p.kind = ESK_PROBE_DIGITAL;
    p.ring = samples; p.len = p.cap = 4; p.head = 2;
    CHECK_NEAR(measured(&p, "duty_cycle", 0, 2000), .25, 1e-12);
    p.len = 2; p.head = 0;
    CHECK(!esk_probe_window(&p, "mean", 0, 2000, 2000, &result, why, sizeof why));
    CHECK_STR(why, "measurement window begins before retained probe history");
    CHECK(!esk_probe_window(&p, "mean", 1000, 2000, 1500, &result, why, sizeof why));
    CHECK(!esk_probe_window(&p, "mean", 1000, 1000, 2000, &result, why, sizeof why));
    samples[0].value = 2;
    CHECK(!esk_probe_window(&p, "duty_cycle", 1000, 2000, 2000, &result, why, sizeof why));
    p.kind = ESK_PROBE_ANALOG; samples[0].value = NAN;
    CHECK(!esk_probe_window(&p, "mean", 1000, 2000, 2000, &result, why, sizeof why));
    samples[0].value = 1;
    CHECK(!esk_probe_window(&p, "duty_cycle", 1000, 2000, 2000, &result, why, sizeof why));
    CHECK(!esk_probe_window(&p, "median", 1000, 2000, 2000, &result, why, sizeof why));
}

static void test_same_time_edges_and_long_timestamps(void)
{
    es_time_t base = UINT64_C(10000000000000000);
    es_sample_t samples[] = {{0, 3}, {0, 1}, {0, 0}, {0, 3}};
    esk_probe p;
    memset(&p, 0, sizeof p);
    samples[0].time_ps = samples[1].time_ps = base;
    samples[2].time_ps = base + 250;
    samples[3].time_ps = base + 1000; /* right endpoint excluded */
    p.kind = ESK_PROBE_DIGITAL; p.ring = samples; p.len = p.cap = 4;
    CHECK_NEAR(measured(&p, "duty_cycle", base, base + 1000), .25, 1e-12);
}

static void test_probe_units(void)
{
    esk_probe p;
    esk_comp component;
    esk_net net;
    esk_adev native;
    memset(&p, 0, sizeof p);
    memset(&component, 0, sizeof component);
    CHECK_STR(esk_probe_unit(NULL), "");
    p.kind = ESK_PROBE_ANALOG; p.net = &net;
    CHECK_STR(esk_probe_unit(&p), "V");
    p.kind = ESK_PROBE_DIGITAL;
    CHECK_STR(esk_probe_unit(&p), "");
    p.net = NULL; p.kind = ESK_PROBE_VALUE; p.comp = &component; p.quantity = "ic";
    CHECK_STR(esk_probe_unit(&p), ""); /* custom output names do not imply amperes */
    component.adev = &native;
    CHECK_STR(esk_probe_unit(&p), "A");
    p.quantity = "p";
    CHECK_STR(esk_probe_unit(&p), "W");
}

TEST_MAIN_BEGIN
RUN(test_weighted_pulses);
RUN(test_invalid_and_wrapped_history);
RUN(test_same_time_edges_and_long_timestamps);
RUN(test_probe_units);
TEST_MAIN_END
