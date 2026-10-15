/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "time/clock.h"
#include "time/sched.h"

static esk_alloc A;
static char trace[256];
static size_t tlen;

static void rec(void *ctx, void *arg, es_time_t now)
{
    ES_UNUSED(ctx); ES_UNUSED(now);
    if (tlen + 1 < sizeof trace)
        trace[tlen++] = (char)(intptr_t)arg;
    trace[tlen] = 0;
}

static esk_sched *g;
static void ring(void *ctx, void *arg, es_time_t now)
{
    ES_UNUSED(ctx); ES_UNUSED(arg);
    esk_sched_at(g, now, ESK_EV_SIGNAL, ring, NULL, NULL, NULL); /* zero-delay loop */
}

static void test_order(void)
{
    esk_sched s;
    esk_sched_init(&s, &A, 100, 10);
    tlen = 0;
    /* same time, classes out of order, seq ties */
    esk_sched_at(&s, 10, ESK_EV_SIGNAL, rec, NULL, (void *)'c', NULL);
    esk_sched_at(&s, 10, ESK_EV_EXTERNAL, rec, NULL, (void *)'a', NULL);
    esk_sched_at(&s, 5, ESK_EV_PROBE, rec, NULL, (void *)'p', NULL);
    esk_sched_at(&s, 10, ESK_EV_EXTERNAL, rec, NULL, (void *)'b', NULL);
    esk_sched_at(&s, 10, ESK_EV_CORE, rec, NULL, (void *)'k', NULL);
    CHECK(esk_sched_advance(&s, 10) == ES_OK);
    CHECK_STR(trace, "pabkc");
    CHECK(s.now == 10);
    CHECK(esk_sched_at(&s, 9, ESK_EV_SIGNAL, rec, NULL, NULL, NULL) == ES_ETIME);
    esk_sched_free(&s);
}

static void test_cancel(void)
{
    esk_sched s;
    esk_event_handle h1, h2;
    esk_sched_init(&s, &A, 100, 10);
    tlen = 0;
    esk_sched_at(&s, 1, ESK_EV_SIGNAL, rec, NULL, (void *)'x', &h1);
    esk_sched_at(&s, 2, ESK_EV_SIGNAL, rec, NULL, (void *)'y', &h2);
    esk_sched_cancel(&s, h1);
    esk_sched_advance(&s, 5);
    CHECK_STR(trace, "y");
    esk_sched_cancel(&s, h2); /* stale: ignored */
    esk_sched_at(&s, 6, ESK_EV_SIGNAL, rec, NULL, (void *)'z', &h1);
    esk_sched_cancel(&s, h2); /* stale handle must not cancel the reused slot */
    esk_sched_advance(&s, 6);
    CHECK_STR(trace, "yz");
    esk_sched_free(&s);
}

static void test_delta_overflow(void)
{
    esk_sched s;
    esk_sched_init(&s, &A, 1000, 50);
    g = &s;
    esk_sched_at(&s, 3, ESK_EV_SIGNAL, ring, NULL, NULL, NULL);
    CHECK(esk_sched_advance(&s, 10) == ES_ELIMIT);
    CHECK(s.delta_overflow);
    CHECK(s.now == 3);
    esk_sched_free(&s);
}

static void test_limit(void)
{
    esk_sched s;
    int i;
    esk_sched_init(&s, &A, 4, 10);
    for (i = 0; i < 4; i++)
        CHECK(esk_sched_at(&s, 1, ESK_EV_SIGNAL, rec, NULL, NULL, NULL) == ES_OK);
    CHECK(esk_sched_at(&s, 1, ESK_EV_SIGNAL, rec, NULL, NULL, NULL) == ES_ELIMIT);
    esk_sched_free(&s);
}

static void test_clock(void)
{
    esk_clock c;
    uint64_t k;
    CHECK(esk_clock_set_hz(&c, 72000000, 0) == 0);
    /* period 1e12/72e6 = 13888.888... = 125000/9 */
    CHECK(c.num == 125000 && c.den == 9);
    CHECK(esk_clock_edge(&c, 9) == 125000);
    CHECK(esk_clock_edge(&c, 1) == 13888);
    CHECK(esk_clock_edge(&c, 2) == 27777);
    /* 72e6 edges = exactly 1 s */
    CHECK(esk_clock_edge(&c, 72000000) == ES_PS_PER_S);
    CHECK(esk_clock_edge(&c, 72000000ull * 1000) == ES_PS_PER_S * 1000);
    CHECK(esk_clock_next(&c, 0) == 13888);
    CHECK(esk_clock_next(&c, 13888) == 27777);
    k = esk_clock_edges_until(&c, ES_PS_PER_S);
    CHECK(k == 72000000);
    CHECK(esk_clock_advance(&c, 72000000) == ES_PS_PER_S);
    CHECK(esk_clock_time(&c) == ES_PS_PER_S);
    CHECK(esk_clock_set_hz(&c, 32768, 100) == 0);
    CHECK(esk_clock_edge(&c, 32768) == 100 + ES_PS_PER_S);
    {
        esk_clock d;
        CHECK(esk_clock_derive(&d, &c, 8, 1, 100) == 0); /* prescaler 8 */
        CHECK(esk_clock_edge(&d, 4096) == 100 + ES_PS_PER_S);
        CHECK_NEAR(esk_clock_hz(&d), 4096.0, 1e-6);
    }
    CHECK(esk_clock_set_hz(&c, 0, 0) != 0);
    esk_clock_disable(&c);
    CHECK(esk_clock_next(&c, 0) == ES_TIME_MAX);
}

TEST_MAIN_BEGIN
esk_alloc_init(&A, NULL);
RUN(test_order);
RUN(test_cancel);
RUN(test_delta_overflow);
RUN(test_limit);
RUN(test_clock);
CHECK_EQ(A.allocations, 0);
TEST_MAIN_END
