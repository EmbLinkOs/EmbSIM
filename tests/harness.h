/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* harness.h -- the minimal test harness (CS-27). */
#ifndef EMBSIM_TEST_HARNESS_H
#define EMBSIM_TEST_HARNESS_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int t_failures = 0, t_checks = 0;

#define CHECK(cond) do { t_checks++; if (!(cond)) { t_failures++; \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { t_checks++; if ((a) != (b)) { t_failures++; \
    fprintf(stderr, "%s:%d: CHECK_EQ failed: %s != %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, \
    (long long)(a), (long long)(b)); } } while (0)
static int t_str_eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
#define CHECK_STR(a, b) do { const char *t_a = (a), *t_b = (b); t_checks++; if (!t_str_eq(t_a, t_b)) { t_failures++; \
    fprintf(stderr, "%s:%d: CHECK_STR failed: %s = \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, \
    t_a ? t_a : "(null)", t_b ? t_b : "(null)"); } } while (0)
#define CHECK_NEAR(a, b, tol) do { t_checks++; if (fabs((double)(a) - (double)(b)) > (tol)) { t_failures++; \
    fprintf(stderr, "%s:%d: CHECK_NEAR failed: %s = %g, expected %g +/- %g\n", __FILE__, __LINE__, #a, \
    (double)(a), (double)(b), (double)(tol)); } } while (0)
#define TEST_MAIN_BEGIN int main(void) {
#define TEST_MAIN_END printf("%d checks, %d failures\n", t_checks, t_failures); return t_failures ? 1 : 0; }
#define RUN(fn) do { printf("- %s\n", #fn); fn(); } while (0)

#endif
