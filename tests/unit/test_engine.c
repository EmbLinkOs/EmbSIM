/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Digital engine tests: resolution and strengths (SPEC-003 §3), inertial
 * delays and gates (§5), transmission-gate switches, the virtual terminal. */
#include "harness.h"
#include "engine/engine.h"
#include "model/registry.h"
#include "util/num.h"

static esk_alloc A;
static esk_diag D;
static int diag_count_code(const char *code)
{
    int n = 0;
    size_t i;
    for (i = 0; i < D.len; i++)
        if (!strcmp(D.log[(D.head + i) % D.cap].code, code))
            n++;
    return n;
}

static esk_comp *add(esk_engine *e, esk_registry *r, const char *id, const char *prim, const char *const *pins,
                     const esk_pin_model *models, uint32_t n, const char *params_json)
{
    esk_comp *c = NULL;
    esk_json *params = NULL;
    int line;
    const char *err;
    if (params_json)
        params = esk_json_parse(&A, params_json, strlen(params_json), &line, &err);
    CHECK(esk_engine_add_comp(e, id, prim, esk_registry_find(r, prim), pins, models, n, params, NULL, &c) == ES_OK);
    esk_json_free(&A, params);
    return c;
}

static void pm(esk_pin_model *m, esk_pin_kind k, esk_strength s, esk_pull pull)
{
    esk_pin_model_family(NULL, m);
    m->kind = k;
    m->strength = s;
    m->pull = pull;
}

static void test_resolution(void)
{
    esk_engine e;
    esk_registry r;
    esk_net *n;
    esk_pin_model mods[2];
    static const char *const out[] = { "OUT" };
    static const char *const one[] = { "1" };
    esk_comp *lvl, *pull, *probe;
    esk_engine_init(&e, &A, &D, ESK_POLICY_FUNCTIONAL);
    esk_registry_init(&r, &A);
    esk_engine_add_net(&e, "N", &n);
    pm(&mods[0], ESK_PIN_DIGITAL_OUT, ESK_STR_STRONG, ESK_PULL_NONE);
    pm(&mods[1], ESK_PIN_DIGITAL_IN, ESK_STR_STRONG, ESK_PULL_NONE);
    lvl = add(&e, &r, "L1", "logic_level", out, mods, 1, "{\"level\":1}");
    pull = add(&e, &r, "P1", "pull", one, mods, 1, "{\"level\":0}");
    probe = add(&e, &r, "Q1", "logic_probe", one, &mods[1], 1, NULL);
    esk_engine_connect(&e, &lvl->pins[0], n);
    esk_engine_connect(&e, &pull->pins[0], n);
    esk_engine_connect(&e, &probe->pins[0], n);
    CHECK(esk_engine_reset(&e) == ES_OK);
    CHECK(esk_net_value(n) == ESK_L1);            /* strong beats weak */
    CHECK(n->strength == ESK_STR_STRONG);
    {
        esk_json v; memset(&v, 0, sizeof v); v.type = ESK_JSON_NUMBER; v.u.number.value = 0; v.u.number.is_integer = 1;
        esk_engine_set_input(&e, lvl, "level", &v);
    }
    esk_engine_advance(&e, 10);
    CHECK(esk_net_value(n) == ESK_L0);
    /* a second strong driver disagreeing: X and a contention diagnostic */
    {
        esk_comp *l2 = add(&e, &r, "L2", "logic_level", out, mods, 1, "{\"level\":1}");
        esk_engine_connect(&e, &l2->pins[0], n);
        esk_engine_partition(&e);
        esk_engine_reset(&e);
        esk_engine_advance(&e, 10);
        CHECK(esk_net_value(n) == ESK_LX);
        CHECK(diag_count_code("net.contention") >= 1);
    }
    esk_engine_free(&e); /* its components use the registry's models */
    esk_registry_free(&r);
}

static void test_floating(void)
{
    esk_engine e;
    esk_registry r;
    esk_net *n;
    esk_pin_model in;
    static const char *const one[] = { "1" };
    esk_comp *probe;
    esk_diag_reset(&D);
    esk_engine_init(&e, &A, &D, ESK_POLICY_FUNCTIONAL);
    esk_registry_init(&r, &A);
    esk_engine_add_net(&e, "F", &n);
    pm(&in, ESK_PIN_DIGITAL_IN, ESK_STR_STRONG, ESK_PULL_NONE);
    probe = add(&e, &r, "Q", "logic_probe", one, &in, 1, NULL);
    esk_engine_connect(&e, &probe->pins[0], n);
    esk_engine_reset(&e);
    CHECK(esk_pin_read(&probe->pins[0]) == ESK_LZ);
    CHECK(diag_count_code("net.floating_input") == 1);
    esk_pin_read(&probe->pins[0]);
    CHECK(diag_count_code("net.floating_input") == 1); /* once per episode */
    /* with a pull the input reads the pull */
    probe->pins[0].model.pull = ESK_PULL_UP;
    CHECK(esk_pin_read(&probe->pins[0]) == ESK_L1);
    esk_engine_free(&e); /* its components use the registry's models */
    esk_registry_free(&r);
}

static void test_gates_and_delay(void)
{
    esk_engine e;
    esk_registry r;
    esk_net *a, *b, *y, *clk;
    esk_pin_model gate_m[3], out_m, in_m;
    static const char *const nand_pins[] = { "A", "B", "Y" };
    static const char *const out[] = { "OUT" };
    esk_comp *g, *la, *lb, *ck;
    esk_engine_init(&e, &A, &D, ESK_POLICY_TIMED);
    esk_registry_init(&r, &A);
    esk_engine_add_net(&e, "A", &a);
    esk_engine_add_net(&e, "B", &b);
    esk_engine_add_net(&e, "Y", &y);
    esk_engine_add_net(&e, "CLK", &clk);
    pm(&gate_m[0], ESK_PIN_DIGITAL_IN, ESK_STR_STRONG, ESK_PULL_NONE);
    gate_m[1] = gate_m[0];
    pm(&gate_m[2], ESK_PIN_DIGITAL_OUT, ESK_STR_STRONG, ESK_PULL_NONE);
    pm(&out_m, ESK_PIN_DIGITAL_OUT, ESK_STR_STRONG, ESK_PULL_NONE);
    pm(&in_m, ESK_PIN_DIGITAL_IN, ESK_STR_STRONG, ESK_PULL_NONE);
    g = add(&e, &r, "G", "nand", nand_pins, gate_m, 3, "{\"tpd_ns\": 10}");
    la = add(&e, &r, "LA", "logic_level", out, &out_m, 1, "{\"level\":1}");
    lb = add(&e, &r, "LB", "logic_level", out, &out_m, 1, "{\"level\":0}");
    ck = add(&e, &r, "CK", "clock_source", out, &out_m, 1, "{\"hz\": 1000000}");
    esk_engine_connect(&e, &la->pins[0], a);
    esk_engine_connect(&e, &lb->pins[0], b);
    esk_engine_connect(&e, esk_comp_pin(g, "A"), a);
    esk_engine_connect(&e, esk_comp_pin(g, "B"), b);
    esk_engine_connect(&e, esk_comp_pin(g, "Y"), y);
    esk_engine_connect(&e, &ck->pins[0], clk);
    esk_engine_reset(&e);
    esk_engine_advance(&e, 20000);
    CHECK(esk_net_value(y) == ESK_L1); /* nand(1,0) */
    {
        esk_json v; memset(&v, 0, sizeof v); v.type = ESK_JSON_NUMBER; v.u.number.value = 1; v.u.number.is_integer = 1; v.u.number.integer = 1;
        esk_engine_set_input(&e, lb, "level", &v);
    }
    esk_engine_advance(&e, 20000 + 9999);
    CHECK(esk_net_value(y) == ESK_L1); /* not yet: tpd 10 ns */
    esk_engine_advance(&e, 20000 + 10000);
    CHECK(esk_net_value(y) == ESK_L0);
    /* inertial: a 3 ns pulse on B is swallowed */
    {
        esk_json v; memset(&v, 0, sizeof v); v.type = ESK_JSON_NUMBER; v.u.number.is_integer = 1;
        v.u.number.value = 0; v.u.number.integer = 0;
        esk_engine_set_input(&e, lb, "level", &v);
        esk_engine_advance(&e, e.sched.now + 3000);
        v.u.number.value = 1; v.u.number.integer = 1;
        esk_engine_set_input(&e, lb, "level", &v);
        esk_engine_advance(&e, e.sched.now + 20000);
        CHECK(esk_net_value(y) == ESK_L0);
    }
    /* the clock: 1 MHz, so at 2.5 us it has made 5 edges (toggles) */
    esk_engine_advance(&e, 2500000);
    CHECK(esk_net_value(clk) == ESK_L1); /* starts low at 0, toggles at 0.5, 1, 1.5, 2, 2.5 -> high */
    esk_engine_free(&e); /* its components use the registry's models */
    esk_registry_free(&r);
}

static void test_switch_gate(void)
{
    esk_engine e;
    esk_registry r;
    esk_net *n1, *n2;
    esk_pin_model sw_m[2], out_m, in_m;
    static const char *const sw_pins[] = { "1", "2" };
    static const char *const out[] = { "OUT" };
    static const char *const one[] = { "1" };
    esk_comp *sw, *lvl, *probe;
    esk_engine_init(&e, &A, &D, ESK_POLICY_FUNCTIONAL);
    esk_registry_init(&r, &A);
    esk_engine_add_net(&e, "N1", &n1);
    esk_engine_add_net(&e, "N2", &n2);
    pm(&sw_m[0], ESK_PIN_DIGITAL_IO, ESK_STR_STRONG, ESK_PULL_NONE);
    sw_m[1] = sw_m[0];
    pm(&out_m, ESK_PIN_DIGITAL_OUT, ESK_STR_STRONG, ESK_PULL_NONE);
    pm(&in_m, ESK_PIN_DIGITAL_IN, ESK_STR_STRONG, ESK_PULL_DOWN);
    sw = add(&e, &r, "SW", "push_button_digital", sw_pins, sw_m, 2, NULL);
    lvl = add(&e, &r, "L", "logic_level", out, &out_m, 1, "{\"level\":1}");
    probe = add(&e, &r, "Q", "logic_probe", one, &in_m, 1, NULL);
    esk_engine_connect(&e, &lvl->pins[0], n1);
    esk_engine_connect(&e, esk_comp_pin(sw, "1"), n1);
    esk_engine_connect(&e, esk_comp_pin(sw, "2"), n2);
    esk_engine_connect(&e, &probe->pins[0], n2);
    esk_engine_reset(&e);
    CHECK(esk_net_value(n2) == ESK_L0);            /* the probe's pull-down is a weak driver */
    CHECK(n2->strength == ESK_STR_WEAK);
    CHECK(esk_pin_read(&probe->pins[0]) == ESK_L0);
    {
        esk_json v; memset(&v, 0, sizeof v); v.type = ESK_JSON_BOOL; v.u.boolean = 1;
        esk_engine_set_input(&e, sw, "pressed", &v);
        esk_engine_advance(&e, 10);
        CHECK(esk_net_value(n2) == ESK_L1);
        CHECK(n2->strength == ESK_STR_STRONG);
        v.u.boolean = 0;
        esk_engine_set_input(&e, sw, "pressed", &v);
        esk_engine_advance(&e, 20);
        CHECK(esk_net_value(n2) == ESK_L0);
        CHECK(n2->strength == ESK_STR_WEAK);
    }
    esk_engine_free(&e); /* its components use the registry's models */
    esk_registry_free(&r);
}

static void test_virtual_terminal(void)
{
    esk_engine e;
    esk_registry r;
    esk_net *line;
    esk_pin_model vt_m[2];
    static const char *const vt_pins[] = { "RX", "TX" };
    esk_comp *a, *b;
    esk_json out;
    esk_engine_init(&e, &A, &D, ESK_POLICY_TIMED);
    esk_registry_init(&r, &A);
    esk_engine_add_net(&e, "AB", &line);
    esk_engine_add_net(&e, "BA", NULL);
    pm(&vt_m[0], ESK_PIN_DIGITAL_IN, ESK_STR_STRONG, ESK_PULL_NONE);
    pm(&vt_m[1], ESK_PIN_DIGITAL_OUT, ESK_STR_STRONG, ESK_PULL_NONE);
    a = add(&e, &r, "TA", "virtual_terminal", vt_pins, vt_m, 2, "{\"baud\": 115200}");
    b = add(&e, &r, "TB", "virtual_terminal", vt_pins, vt_m, 2, "{\"baud\": 115200}");
    esk_engine_connect(&e, esk_comp_pin(a, "TX"), line);
    esk_engine_connect(&e, esk_comp_pin(b, "RX"), line);
    esk_engine_connect(&e, esk_comp_pin(a, "RX"), esk_engine_net(&e, "BA"));
    esk_engine_connect(&e, esk_comp_pin(b, "TX"), esk_engine_net(&e, "BA"));
    esk_engine_reset(&e);
    {
        esk_json v; memset(&v, 0, sizeof v); v.type = ESK_JSON_STRING; v.u.string.data = (char *)"hi!"; v.u.string.len = 3;
        esk_engine_set_input(&e, a, "tx_text", &v);
    }
    esk_engine_advance(&e, 1 * ES_PS_PER_MS); /* 3 bytes at 115200 take ~260 us */
    memset(&out, 0, sizeof out);
    {
        esk_json *o = esk_json_new_object(&A);
        b->ops->observe(b, o);
        CHECK_STR(esk_json_get_str(o, "rx_text", ""), "hi!");
        esk_json_free(&A, o);
    }
    esk_engine_free(&e); /* its components use the registry's models */
    esk_registry_free(&r);
}

TEST_MAIN_BEGIN
esk_alloc_init(&A, NULL);
esk_diag_init(&D, &A, 64);
RUN(test_resolution);
RUN(test_floating);
RUN(test_gates_and_delay);
RUN(test_switch_gate);
RUN(test_virtual_terminal);
esk_diag_free(&D);
CHECK_EQ(A.allocations, 0);
TEST_MAIN_END
