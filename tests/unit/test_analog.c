/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_analog.c -- the analog engine (SPEC-004): operating points, transients,
 * device models, bridges and the validation fixtures of tests/analog/fixtures. */
#include "analog/analog.h"
#include "engine/engine.h"
#include "harness.h"
#include "util/num.h"

#ifndef EMBSIM_SOURCE_DIR
#define EMBSIM_SOURCE_DIR "."
#endif

static esk_alloc A;
static esk_diag D;
static int diag_errors;

static void on_diag(void *ctx, const es_diagnostic_t *d)
{
    ES_UNUSED(ctx);
    if (d->severity >= ES_SEV_ERROR)
        diag_errors++;
    fprintf(stderr, "  [diag %d] %s %s: %s\n", (int)d->severity, d->code,
            d->subject ? d->subject : "", d->message);
}

/* ---- circuit building ------------------------------------------------------ */

#define MAX_KINDS 32
static esk_model_ops model_ops[MAX_KINDS];
static size_t nmodel_ops;

static void init_model_ops(void)
{
    size_t n, i;
    const esk_adev_ops *const *t = esk_adev_table(&n);
    for (i = 0; i < n && i < MAX_KINDS; i++) {
        memset(&model_ops[i], 0, sizeof model_ops[i]);
        model_ops[i].name = t[i]->name;
        model_ops[i].adev = t[i];
    }
    nmodel_ops = n < MAX_KINDS ? n : MAX_KINDS;
}

static const esk_model_ops *ops_for(const char *kind)
{
    size_t i;
    for (i = 0; i < nmodel_ops; i++)
        if (!strcmp(model_ops[i].name, kind))
            return &model_ops[i];
    return NULL;
}

static const esk_model_ops power_ops = {"power", 0,    NULL, NULL, NULL, NULL,
                                        NULL,    NULL, NULL, NULL, 0};

static esk_net *net_of(esk_engine *e, const char *name)
{
    esk_net *n = esk_engine_net(e, name);
    if (!n)
        esk_engine_add_net(e, name, &n);
    return n;
}

static esk_json *parse(const char *text)
{
    int line;
    const char *err;
    esk_json *j = esk_json_parse(&A, text, strlen(text), &line, &err);
    if (!j)
        fprintf(stderr, "json error line %d: %s in %s\n", line, err, text);
    return j;
}

/* a device component: pins named "p0".."p7" in the device's pin order, connected to `nets` */
static esk_comp *add_dev(esk_engine *e, const char *kind, const char *id, const char *params_json,
                         const char *const *nets, int npins)
{
    esk_json *params = params_json ? parse(params_json) : NULL;
    const esk_model_ops *ops = ops_for(kind);
    esk_pin_model models[8];
    const char *names[8] = {"p0", "p1", "p2", "p3", "p4", "p5", "p6", "p7"};
    esk_comp *c = NULL;
    int k;
    es_status st;
    CHECK(ops != NULL);
    if (!ops)
        return NULL;
    for (k = 0; k < npins; k++) {
        esk_pin_model_family(NULL, &models[k]);
        models[k].kind = ESK_PIN_PASSIVE;
    }
    st = esk_engine_add_comp(e, id, kind, ops, names, models, (uint32_t)npins, params, NULL, &c);
    CHECK(st == ES_OK);
    esk_json_free(&A, params);
    if (!c)
        return NULL;
    for (k = 0; k < npins; k++)
        if (nets[k])
            CHECK(esk_engine_connect(e, &c->pins[k], net_of(e, nets[k])) == ES_OK);
    return c;
}

static esk_comp *add_dev2(esk_engine *e, const char *kind, const char *id, const char *params,
                          const char *n1, const char *n2)
{
    const char *nets[2];
    nets[0] = n1;
    nets[1] = n2;
    return add_dev(e, kind, id, params, nets, 2);
}

static esk_comp *add_dev3(esk_engine *e, const char *kind, const char *id, const char *params,
                          const char *n1, const char *n2, const char *n3)
{
    const char *nets[3];
    nets[0] = n1;
    nets[1] = n2;
    nets[2] = n3;
    return add_dev(e, kind, id, params, nets, 3);
}

/* a power pin on a net: ground when v == 0 */
static esk_comp *add_power(esk_engine *e, const char *id, const char *net, double v)
{
    esk_pin_model m;
    const char *name = "P";
    esk_comp *c = NULL;
    esk_pin_model_family(NULL, &m);
    m.kind = ESK_PIN_POWER;
    m.power_ground = v == 0.0;
    m.nominal_v = v;
    CHECK(esk_engine_add_comp(e, id, "power", &power_ops, &name, &m, 1, NULL, NULL, &c) == ES_OK);
    if (c)
        CHECK(esk_engine_connect(e, &c->pins[0], net_of(e, net)) == ES_OK);
    return c;
}

static void engine_open(esk_engine *e)
{
    esk_diag_reset(&D);
    diag_errors = 0;
    CHECK(esk_engine_init(e, &A, &D, ESK_POLICY_ELECTRICAL) == ES_OK);
}

static void engine_run(esk_engine *e)
{
    CHECK(esk_engine_partition(e) == ES_OK);
    CHECK(esk_engine_reset(e) == ES_OK);
}

static double v_of(esk_engine *e, const char *net)
{
    esk_net *n = esk_engine_net(e, net);
    return n ? n->voltage : -999.0;
}

static double obs_num(esk_engine *e, const char *comp, const char *key)
{
    esk_comp *c = esk_engine_comp(e, comp);
    esk_json *o;
    double v = -999.0;
    if (!c || !c->adev || !c->adev->ops->observe)
        return v;
    o = esk_json_new_object(&A);
    c->adev->ops->observe(c->adev, o);
    v = esk_json_get_num(o, key, -999.0);
    esk_json_free(&A, o);
    return v;
}

static const char *obs_str(esk_engine *e, const char *comp, const char *key, char *buf, size_t cap)
{
    esk_comp *c = esk_engine_comp(e, comp);
    esk_json *o;
    buf[0] = 0;
    if (!c || !c->adev || !c->adev->ops->observe)
        return buf;
    o = esk_json_new_object(&A);
    c->adev->ops->observe(c->adev, o);
    esk_snprintf(buf, cap, "%s", esk_json_get_str(o, key, ""));
    esk_json_free(&A, o);
    return buf;
}

/* ---- tests --------------------------------------------------------------------- */

static void test_divider(void)
{
    esk_engine e;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 5}", "vin", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "vin", "out");
    add_dev2(&e, "resistor", "R2", "{\"r\": 1000}", "out", "gnd");
    engine_run(&e);
    CHECK_NEAR(v_of(&e, "vin"), 5.0, 1e-6);
    CHECK_NEAR(v_of(&e, "out"), 2.5, 1e-6);
    CHECK_NEAR(obs_num(&e, "R1", "i"), 2.5e-3, 1e-8);
    CHECK_NEAR(obs_num(&e, "R1", "p"), 6.25e-3, 1e-7);
    CHECK_NEAR(obs_num(&e, "V1", "i"), -2.5e-3, 1e-8);
    CHECK(esk_engine_advance(&e, ES_PS_PER_MS) == ES_OK);
    CHECK_NEAR(v_of(&e, "out"), 2.5, 1e-6);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_supply_pin_divider(void)
{
    esk_engine e;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_power(&e, "P", "vcc", 3.3);
    add_power(&e, "P2", "vcc",
              3.3); /* a second supply pin on the same net must not make it singular */
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "vcc", "out");
    add_dev2(&e, "resistor", "R2", "{\"r\": \"2k\"}", "out", "gnd");
    engine_run(&e);
    CHECK_NEAR(v_of(&e, "vcc"), 3.3, 1e-9);
    CHECK_NEAR(v_of(&e, "out"), 2.2, 1e-6);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_implicit_ground(void)
{
    esk_engine e;
    engine_open(&e);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 5}", "vin", "ret");
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "vin", "out");
    add_dev2(&e, "resistor", "R2", "{\"r\": \"1k\"}", "out", "ret");
    engine_run(&e);
    CHECK_NEAR(v_of(&e, "out") - v_of(&e, "ret"), 2.5, 1e-6);
    CHECK(esk_diag_count(&D, ES_SEV_INFO) >= 1);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_rc_charge(void)
{
    esk_engine e;
    double tau_v = 5.0 * (1.0 - exp(-1.0));
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 5}", "vin", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "vin", "out");
    add_dev2(&e, "capacitor", "C1", "{\"c\": \"1u\", \"ic\": 0}", "out", "gnd");
    engine_run(&e);
    CHECK_NEAR(v_of(&e, "out"), 0.0, 1e-9);
    CHECK(esk_engine_advance(&e, ES_PS_PER_MS) == ES_OK);
    CHECK_NEAR(v_of(&e, "out"), tau_v, 0.01 * tau_v);
    CHECK(esk_engine_advance(&e, 3 * ES_PS_PER_MS) == ES_OK);
    CHECK_NEAR(v_of(&e, "out"), 5.0 * (1.0 - exp(-3.0)), 0.05);
    CHECK(esk_engine_advance(&e, 20 * ES_PS_PER_MS) == ES_OK);
    CHECK_NEAR(v_of(&e, "out"), 5.0, 1e-3);
    CHECK(esk_engine_net(&e, "out")->island->steps < 2000);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_rc_with_ic(void)
{
    esk_engine e;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "out", "gnd");
    add_dev2(&e, "capacitor", "C1", "{\"c\": \"1u\", \"ic\": 2}", "out", "gnd");
    engine_run(&e);
    CHECK_NEAR(v_of(&e, "out"), 2.0, 1e-2);
    CHECK(esk_engine_advance(&e, ES_PS_PER_MS) == ES_OK);
    CHECK_NEAR(v_of(&e, "out"), 2.0 * exp(-1.0), 0.02);
    esk_engine_free(&e);
}

static void test_rl(void)
{
    esk_engine e;
    /* 5 V through 100 ohm into 10 mH: tau = 100 us; v(L) = 5 e^{-t/tau} */
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 5}", "vin", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": 100}", "vin", "mid");
    add_dev2(&e, "inductor", "L1", "{\"l\": \"10m\"}", "mid", "gnd");
    engine_run(&e);
    CHECK_NEAR(v_of(&e, "mid"), 0.0, 1e-6); /* the inductor is a short at DC */
    CHECK_NEAR(obs_num(&e, "L1", "i"), 0.05, 1e-9);
    CHECK_NEAR(obs_num(&e, "V1", "i"), -0.05, 1e-9);
    CHECK(esk_engine_advance(&e, ES_PS_PER_MS) == ES_OK);
    CHECK_NEAR(v_of(&e, "mid"), 0.0, 1e-6);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_rl_transient(void)
{
    esk_engine e;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    /* the source steps from 0 to 5 V at t = 0 (pulse), so the current ramps with tau = L/R */
    add_dev2(&e, "vsource", "V1",
             "{\"kind\": \"pulse\", \"v1\": 0, \"v2\": 5, \"tr\": \"1n\", \"pw\": 1, \"per\": 2}",
             "vin", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": 100}", "vin", "mid");
    add_dev2(&e, "inductor", "L1", "{\"l\": \"10m\"}", "mid", "gnd");
    engine_run(&e);
    CHECK_NEAR(v_of(&e, "mid"), 0.0, 1e-6);
    CHECK(esk_engine_advance(&e, 100 * ES_PS_PER_US) == ES_OK);
    CHECK_NEAR(v_of(&e, "mid"), 5.0 * exp(-1.0), 0.01 * 5.0 * exp(-1.0));
    CHECK_NEAR(obs_num(&e, "L1", "i"), 0.05 * (1.0 - exp(-1.0)), 0.05 * 0.01);
    CHECK(esk_engine_advance(&e, 2 * ES_PS_PER_MS) == ES_OK);
    CHECK_NEAR(obs_num(&e, "L1", "i"), 0.05, 1e-4);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_diode_curve(void)
{
    static const double currents[5] = {1e-6, 1e-4, 1e-3, 1e-2, 5e-2};
    int k;
    for (k = 0; k < 5; k++) {
        esk_engine e;
        char params[64];
        double expect = 0.025852 * log(currents[k] / 1e-14 + 1.0);
        engine_open(&e);
        add_power(&e, "G", "gnd", 0.0);
        esk_snprintf(params, sizeof params, "{\"dc\": %g}", currents[k]);
        add_dev2(&e, "isource", "I1", params, "gnd", "a");
        add_dev2(&e, "diode", "D1", "{\"is\": 1e-14, \"n\": 1}", "a", "gnd");
        engine_run(&e);
        CHECK_NEAR(v_of(&e, "a"), expect, 1e-3);
        CHECK_NEAR(obs_num(&e, "D1", "i"), currents[k], currents[k] * 1e-3);
        CHECK_EQ(diag_errors, 0);
        esk_engine_free(&e);
    }
}

static void test_diode_reverse_and_series(void)
{
    esk_engine e;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1", "{\"dc\": -5}", "a", "gnd");
    add_dev2(&e, "diode", "D1", "{\"rs\": 1}", "a", "k");
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "k", "gnd");
    engine_run(&e);
    CHECK_NEAR(v_of(&e, "k"), 0.0, 1e-6);
    esk_engine_free(&e);
    /* forward with rs: 1 V through 1 ohm and 100 ohm */
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 5}", "a", "gnd");
    add_dev2(&e, "diode", "D1", "{\"rs\": 1}", "a", "k");
    add_dev2(&e, "resistor", "R1", "{\"r\": 100}", "k", "gnd");
    engine_run(&e);
    CHECK(v_of(&e, "k") > 4.2 && v_of(&e, "k") < 4.4);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_led_resistor(void)
{
    esk_engine e;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_power(&e, "P", "vcc", 3.3);
    add_dev2(&e, "resistor", "R1", "{\"r\": 100}", "vcc", "a");
    add_dev2(&e, "led", "D1", "{\"color\": \"red\"}", "a", "gnd");
    engine_run(&e);
    CHECK_NEAR(obs_num(&e, "D1", "i"), (3.3 - 1.8) / 100.0, 0.1 * (3.3 - 1.8) / 100.0);
    CHECK_NEAR(obs_num(&e, "D1", "current"), obs_num(&e, "D1", "i"), 1e-12);
    CHECK(obs_num(&e, "D1", "luminance") > 0.6 && obs_num(&e, "D1", "luminance") < 0.85);
    CHECK(v_of(&e, "a") > 1.7 && v_of(&e, "a") < 1.9);
    esk_engine_free(&e);
    /* blue at 5 V with 220 ohm */
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 5}", "vcc", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": 220}", "vcc", "a");
    add_dev2(&e, "led", "D1", "{\"color\": \"blue\"}", "a", "gnd");
    engine_run(&e);
    CHECK_NEAR(obs_num(&e, "D1", "i"), (5.0 - 2.9) / 220.0, 0.1 * (5.0 - 2.9) / 220.0);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_zener(void)
{
    esk_engine e;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 12}", "vin", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": 470}", "vin", "z");
    add_dev2(&e, "zener", "Z1", "{\"vz\": 5.1}", "gnd", "z"); /* anode to ground, cathode at z */
    engine_run(&e);
    CHECK(v_of(&e, "z") > 5.0 && v_of(&e, "z") < 5.5);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_npn_switch(void)
{
    esk_engine e;
    char region[32];
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_power(&e, "P", "vcc", 3.3);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 3.3}", "ctl", "gnd");
    add_dev2(&e, "resistor", "RB", "{\"r\": \"1k\"}", "ctl", "b");
    add_dev2(&e, "resistor", "RC", "{\"r\": 220}", "vcc", "c");
    add_dev3(&e, "bjt_npn", "Q1", "{\"bf\": 100, \"vaf\": 100}", "c", "b", "gnd");
    engine_run(&e);
    CHECK(v_of(&e, "c") < 0.3);
    CHECK(v_of(&e, "b") > 0.6 && v_of(&e, "b") < 0.95);
    CHECK_NEAR(obs_num(&e, "Q1", "ic"), (3.3 - v_of(&e, "c")) / 220.0, 1e-5);
    CHECK_NEAR(obs_num(&e, "Q1", "ib"), (3.3 - v_of(&e, "b")) / 1000.0, 1e-5);
    CHECK_NEAR(obs_num(&e, "Q1", "vce"), v_of(&e, "c"), 1e-9);
    CHECK_STR(obs_str(&e, "Q1", "region", region, sizeof region), "saturation");
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
    /* cutoff */
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_power(&e, "P", "vcc", 3.3);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 0}", "ctl", "gnd");
    add_dev2(&e, "resistor", "RB", "{\"r\": \"1k\"}", "ctl", "b");
    add_dev2(&e, "resistor", "RC", "{\"r\": 220}", "vcc", "c");
    add_dev3(&e, "bjt_npn", "Q1", "{\"bf\": 100}", "c", "b", "gnd");
    engine_run(&e);
    CHECK(v_of(&e, "c") > 3.29);
    CHECK_STR(obs_str(&e, "Q1", "region", region, sizeof region), "cutoff");
    esk_engine_free(&e);
    /* active: emitter follower, 1 V at the base -> ~0.3 V at the emitter over 1k */
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_power(&e, "P", "vcc", 5.0);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 2}", "b", "gnd");
    add_dev2(&e, "resistor", "RE", "{\"r\": \"1k\"}", "e", "gnd");
    add_dev3(&e, "bjt_npn", "Q1", "{\"bf\": 100}", "vcc", "b", "e");
    engine_run(&e);
    CHECK(v_of(&e, "e") > 1.2 && v_of(&e, "e") < 1.4);
    CHECK_STR(obs_str(&e, "Q1", "region", region, sizeof region), "active");
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_pnp_switch(void)
{
    esk_engine e;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_power(&e, "P", "vcc", 3.3);
    add_dev2(&e, "resistor", "RB", "{\"r\": \"1k\"}", "b", "gnd");
    add_dev2(&e, "resistor", "RC", "{\"r\": 220}", "c", "gnd");
    add_dev3(&e, "bjt_pnp", "Q1", "{\"bf\": 100}", "c", "b", "vcc");
    engine_run(&e);
    CHECK(v_of(&e, "c") > 3.0);
    CHECK(v_of(&e, "b") > 2.3 && v_of(&e, "b") < 2.7);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_nmos_switch(void)
{
    esk_engine e;
    char region[32];
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_power(&e, "P", "vcc", 3.3);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 3.3}", "g", "gnd");
    add_dev2(&e, "resistor", "RD", "{\"r\": 220}", "vcc", "d");
    add_dev3(&e, "nmos", "M1", "{\"vto\": 2, \"kp\": 100e-6, \"w\": 1000e-6, \"l\": 1e-6}", "d",
             "g", "gnd");
    engine_run(&e);
    CHECK(v_of(&e, "d") < 0.2);
    CHECK_STR(obs_str(&e, "M1", "region", region, sizeof region), "linear");
    CHECK_NEAR(obs_num(&e, "M1", "id"), (3.3 - v_of(&e, "d")) / 220.0, 1e-6);
    esk_engine_free(&e);
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_power(&e, "P", "vcc", 3.3);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 0}", "g", "gnd");
    add_dev2(&e, "resistor", "RD", "{\"r\": 220}", "vcc", "d");
    add_dev3(&e, "nmos", "M1", "{\"vto\": 2, \"kp\": 100e-6, \"w\": 1000e-6, \"l\": 1e-6}", "d",
             "g", "gnd");
    engine_run(&e);
    CHECK(v_of(&e, "d") > 3.29);
    CHECK_STR(obs_str(&e, "M1", "region", region, sizeof region), "cutoff");
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
    /* pmos high-side switch: gate low turns it on */
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_power(&e, "P", "vcc", 3.3);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 0}", "g", "gnd");
    add_dev2(&e, "resistor", "RD", "{\"r\": 220}", "d", "gnd");
    add_dev3(&e, "pmos", "M1", "{\"vto\": -2, \"kp\": 100e-6, \"w\": 1000e-6, \"l\": 1e-6}", "d",
             "g", "vcc");
    engine_run(&e);
    CHECK(v_of(&e, "d") > 3.1);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_pulse_rc(void)
{
    esk_engine e;
    double tau = 1e-4, vhigh, vlow;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1",
             "{\"kind\": \"pulse\", \"v1\": 0, \"v2\": 3.3, \"tr\": \"1n\", \"tf\": \"1n\", "
             "\"pw\": \"0.5m\", \"per\": \"1m\"}",
             "vin", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "vin", "out");
    add_dev2(&e, "capacitor", "C1", "{\"c\": \"100n\"}", "out", "gnd");
    engine_run(&e);
    /* periodic steady state after many periods: end of the high half and end of the low half */
    vhigh = 3.3 * (1.0 - exp(-0.5e-3 / tau)) / (1.0 - exp(-1e-3 / tau));
    vlow = vhigh * exp(-0.5e-3 / tau);
    CHECK(esk_engine_advance(&e, 10 * ES_PS_PER_MS + 500 * ES_PS_PER_US) == ES_OK);
    CHECK_NEAR(v_of(&e, "out"), vhigh, 0.02 * vhigh);
    CHECK(esk_engine_advance(&e, 11 * ES_PS_PER_MS) == ES_OK);
    CHECK_NEAR(v_of(&e, "out"), vlow, 0.01);
    CHECK(esk_engine_net(&e, "out")->island->steps < 5000);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_sine_source(void)
{
    esk_engine e;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1", "{\"kind\": \"sine\", \"vo\": 1, \"va\": 2, \"freq\": 1000}",
             "vin", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "vin", "gnd");
    engine_run(&e);
    CHECK_NEAR(v_of(&e, "vin"), 1.0, 1e-9);
    CHECK(esk_engine_advance(&e, 250 * ES_PS_PER_US) == ES_OK);
    CHECK_NEAR(v_of(&e, "vin"), 3.0, 1e-6);
    CHECK(esk_engine_advance(&e, 750 * ES_PS_PER_US) == ES_OK);
    CHECK_NEAR(v_of(&e, "vin"), -1.0, 1e-6);
    esk_engine_free(&e);
}

static void test_pwl_source(void)
{
    esk_engine e;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1",
             "{\"kind\": \"pwl\", \"points\": [[0, 0], [\"1m\", 2], [\"2m\", 2], [\"3m\", 0]]}",
             "vin", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "vin", "gnd");
    engine_run(&e);
    CHECK(esk_engine_advance(&e, 500 * ES_PS_PER_US) == ES_OK);
    CHECK_NEAR(v_of(&e, "vin"), 1.0, 1e-6);
    CHECK(esk_engine_advance(&e, 1500 * ES_PS_PER_US) == ES_OK);
    CHECK_NEAR(v_of(&e, "vin"), 2.0, 1e-6);
    CHECK(esk_engine_advance(&e, 2500 * ES_PS_PER_US) == ES_OK);
    CHECK_NEAR(v_of(&e, "vin"), 1.0, 1e-6);
    CHECK(esk_engine_advance(&e, 4 * ES_PS_PER_MS) == ES_OK);
    CHECK_NEAR(v_of(&e, "vin"), 0.0, 1e-6);
    esk_engine_free(&e);
}

static void test_potentiometer(void)
{
    esk_engine e;
    esk_comp *pot;
    esk_json *pos;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 3.3}", "top", "gnd");
    pot = add_dev3(&e, "potentiometer", "P1", "{\"r\": \"10k\"}", "gnd", "w", "top");
    engine_run(&e);
    CHECK_NEAR(v_of(&e, "w"), 1.65, 1e-6); /* default position 0.5 */
    pos = esk_json_new_number(&A, 0.25);
    CHECK(esk_engine_set_input(&e, pot, "position", pos) == ES_OK);
    esk_json_free(&A, pos);
    esk_island_touch(esk_engine_net(&e, "w")->island, esk_engine_now(&e));
    CHECK(esk_engine_advance(&e, ES_PS_PER_US) == ES_OK);
    CHECK_NEAR(v_of(&e, "w"), 3.3 * 0.25, 1e-6); /* r1 = r * position between pin 1 and the wiper */
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_switch_input(void)
{
    esk_engine e;
    esk_comp *sw;
    esk_json *v;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 3.3}", "vin", "gnd");
    sw = add_dev2(&e, "switch", "S1", "{\"ron\": 1, \"roff\": \"1G\"}", "vin", "out");
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "out", "gnd");
    engine_run(&e);
    CHECK(v_of(&e, "out") < 1e-4);
    CHECK(esk_engine_advance(&e, ES_PS_PER_MS) == ES_OK);
    v = esk_json_new_bool(&A, 1);
    CHECK(esk_engine_set_input(&e, sw, "pressed", v) == ES_OK);
    esk_json_free(&A, v);
    esk_island_touch(esk_engine_net(&e, "out")->island, esk_engine_now(&e));
    CHECK(esk_engine_advance(&e, 2 * ES_PS_PER_MS) == ES_OK);
    CHECK_NEAR(v_of(&e, "out"), 3.3 * 1000.0 / 1001.0, 1e-6);
    v = esk_json_new_bool(&A, 0);
    CHECK(esk_engine_set_input(&e, sw, "pressed", v) == ES_OK);
    esk_json_free(&A, v);
    esk_island_touch(esk_engine_net(&e, "out")->island, esk_engine_now(&e));
    CHECK(esk_engine_advance(&e, 3 * ES_PS_PER_MS) == ES_OK);
    CHECK(v_of(&e, "out") < 1e-4);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

/* a watcher: a component with one digital_in pin counting on_input calls */
typedef struct watcher_state {
    int calls;
    es_time_t first_at;
    esk_logic last;
} watcher_state;

static void watcher_on_input(esk_comp *c, esk_pin *pin)
{
    watcher_state *s = (watcher_state *)c->state;
    if (s->calls == 0)
        s->first_at = pin->net && pin->net->island ? pin->net->island->t_accepted : esk_comp_now(c);
    s->calls++;
    s->last = esk_pin_read(pin);
}

static const esk_model_ops watcher_ops = {
    "watcher", sizeof(watcher_state), NULL, NULL, NULL, watcher_on_input, NULL, NULL, NULL, NULL,
    0};

static esk_comp *add_watcher(esk_engine *e, const char *id, const char *net, esk_pull pull)
{
    esk_pin_model m;
    const char *name = "IN";
    esk_comp *c = NULL;
    esk_pin_model_family("cmos_3v3", &m);
    m.kind = ESK_PIN_DIGITAL_IN;
    m.pull = pull;
    m.rpull = 10000.0;
    CHECK(esk_engine_add_comp(e, id, "watcher", &watcher_ops, &name, &m, 1, NULL, NULL, &c) ==
          ES_OK);
    if (c) {
        CHECK(esk_engine_connect(e, &c->pins[0], net_of(e, net)) == ES_OK);
        esk_pin_watch(&c->pins[0], 1);
    }
    return c;
}

static void test_comparator_bridge(void)
{
    esk_engine e;
    esk_comp *w;
    watcher_state *ws;
    double tau = 1e-4, t_cross = tau * log(3.3 / (3.3 - 2.0));
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1",
             "{\"kind\": \"pulse\", \"v1\": 0, \"v2\": 3.3, \"tr\": \"1n\", \"tf\": \"1n\", "
             "\"pw\": \"0.5m\", \"per\": \"1m\"}",
             "vin", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "vin", "out");
    add_dev2(&e, "capacitor", "C1", "{\"c\": \"100n\"}", "out", "gnd");
    w = add_watcher(&e, "U1", "out", ESK_PULL_NONE);
    engine_run(&e);
    ws = (watcher_state *)w->state;
    /* the initial value (0) is delivered at the operating point */
    CHECK_EQ(ws->calls, 1);
    CHECK_EQ(w->pins[0].sampled, ESK_L0);
    CHECK(esk_engine_advance(&e, 5 * ES_PS_PER_MS) == ES_OK);
    CHECK_EQ(ws->calls, 11); /* five rising and five falling crossings */
    CHECK_EQ(w->pins[0].sampled, ESK_L0);
    CHECK(esk_engine_advance(&e, 5 * ES_PS_PER_MS + 300 * ES_PS_PER_US) == ES_OK);
    CHECK_EQ(ws->calls, 12);
    CHECK_EQ(ws->last, ESK_L1);
    CHECK_EQ(w->pins[0].sampled, ESK_L1);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
    /* the first crossing time: v = 3.3 (1 - e^{-t/tau}) reaches vih = 2.0 */
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1",
             "{\"kind\": \"pulse\", \"v1\": 0, \"v2\": 3.3, \"tr\": \"1n\", \"tf\": \"1n\", "
             "\"pw\": \"0.5m\", \"per\": \"1m\"}",
             "vin", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "vin", "out");
    add_dev2(&e, "capacitor", "C1", "{\"c\": \"100n\"}", "out", "gnd");
    w = add_watcher(&e, "U1", "out", ESK_PULL_NONE);
    engine_run(&e);
    ws = (watcher_state *)w->state;
    ws->calls = 0;
    CHECK(esk_engine_advance(&e, 200 * ES_PS_PER_US) == ES_OK);
    CHECK_EQ(ws->calls, 1);
    CHECK_NEAR((double)ws->first_at * 1e-12, t_cross, 2e-6);
    esk_engine_free(&e);
}

static void test_pull_up_input(void)
{
    esk_engine e;
    esk_comp *w, *sw;
    watcher_state *ws;
    esk_json *v;
    /* a button to ground on an input with a 10k pull-up: the pull keeps it high */
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    sw = add_dev2(&e, "switch", "S1", "{\"ron\": 1}", "btn", "gnd");
    w = add_watcher(&e, "U1", "btn", ESK_PULL_UP);
    engine_run(&e);
    ws = (watcher_state *)w->state;
    CHECK_NEAR(v_of(&e, "btn"), 3.3, 1e-3);
    CHECK_EQ(w->pins[0].sampled, ESK_L1);
    CHECK(esk_engine_advance(&e, ES_PS_PER_MS) == ES_OK);
    v = esk_json_new_bool(&A, 1);
    esk_engine_set_input(&e, sw, "pressed", v);
    esk_json_free(&A, v);
    esk_island_touch(esk_engine_net(&e, "btn")->island, esk_engine_now(&e));
    CHECK(esk_engine_advance(&e, 2 * ES_PS_PER_MS) == ES_OK);
    CHECK(v_of(&e, "btn") < 0.01);
    CHECK_EQ(w->pins[0].sampled, ESK_L0);
    CHECK_EQ(ws->calls, 2);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

/* a driver: a component with one digital_out pin */
static const esk_model_ops driver_ops = {"driver", 0,    NULL, NULL, NULL, NULL,
                                         NULL,     NULL, NULL, NULL, 0};

static esk_comp *add_driver(esk_engine *e, const char *id, const char *net)
{
    esk_pin_model m;
    const char *name = "OUT";
    esk_comp *c = NULL;
    esk_pin_model_family("cmos_3v3", &m);
    m.kind = ESK_PIN_DIGITAL_OUT;
    CHECK(esk_engine_add_comp(e, id, "driver", &driver_ops, &name, &m, 1, NULL, NULL, &c) == ES_OK);
    if (c)
        CHECK(esk_engine_connect(e, &c->pins[0], net_of(e, net)) == ES_OK);
    return c;
}

static void test_thevenin_bridge(void)
{
    esk_engine e;
    esk_comp *drv, *w;
    watcher_state *ws;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    drv = add_driver(&e, "MCU", "pin");
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "pin", "out");
    add_dev2(&e, "capacitor", "C1", "{\"c\": \"100n\"}", "out", "gnd");
    add_dev2(&e, "resistor", "R2", "{\"r\": \"100k\"}", "out", "gnd");
    w = add_watcher(&e, "U1", "out", ESK_PULL_NONE);
    engine_run(&e);
    ws = (watcher_state *)w->state;
    /* Z at start: the node floats to 0 through R2 */
    CHECK_NEAR(v_of(&e, "pin"), 0.0, 1e-6);
    CHECK(esk_engine_advance(&e, ES_PS_PER_MS) == ES_OK);
    esk_pin_drive(&drv->pins[0], ESK_L1, ESK_STR_STRONG, 0);
    CHECK(esk_engine_advance(&e, ES_PS_PER_MS + 10 * ES_PS_PER_US) == ES_OK);
    /* the source ramped to voh = 2.9 at 500 V/us: 5.8 ns; after 10 us the pin is near 2.9 */
    CHECK(v_of(&e, "pin") > 2.5 && v_of(&e, "pin") <= 2.9);
    CHECK(esk_engine_advance(&e, 3 * ES_PS_PER_MS) == ES_OK);
    CHECK_NEAR(v_of(&e, "out"), 2.9 * 100000.0 / 101025.0, 1e-3);
    CHECK_EQ(w->pins[0].sampled, ESK_L1);
    CHECK_EQ(ws->calls, 2);
    esk_pin_drive(&drv->pins[0], ESK_L0, ESK_STR_STRONG, 0);
    CHECK(esk_engine_advance(&e, 6 * ES_PS_PER_MS) == ES_OK);
    CHECK_NEAR(v_of(&e, "out"), 0.4 * 100000.0 / 101025.0, 1e-3);
    CHECK_EQ(w->pins[0].sampled, ESK_L0);
    CHECK_EQ(ws->calls, 3);
    esk_pin_release(&drv->pins[0]);
    /* open: the node decays through R2 with tau = 10 ms */
    CHECK(esk_engine_advance(&e, 80 * ES_PS_PER_MS) == ES_OK);
    CHECK_NEAR(v_of(&e, "out"), 0.0, 1e-3);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_opamp_inverting(void)
{
    esk_engine e;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 0.5}", "vin", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "vin", "inv");
    add_dev2(&e, "resistor", "R2", "{\"r\": \"10k\"}", "inv", "out");
    add_dev3(&e, "opamp", "U1", "{\"gain\": 1e5, \"vpos\": 12, \"vneg\": -12}", "gnd", "inv",
             "out");
    engine_run(&e);
    CHECK_NEAR(v_of(&e, "out"), -5.0, 0.01);
    CHECK_NEAR(v_of(&e, "inv"), 0.0, 1e-3);
    esk_engine_free(&e);
    /* saturation */
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 2}", "vin", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "vin", "inv");
    add_dev2(&e, "resistor", "R2", "{\"r\": \"10k\"}", "inv", "out");
    add_dev3(&e, "opamp", "U1", "{\"gain\": 1e5, \"vpos\": 12, \"vneg\": -12}", "gnd", "inv",
             "out");
    engine_run(&e);
    CHECK(v_of(&e, "out") < -11.5 && v_of(&e, "out") > -12.01);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_regulator(void)
{
    esk_engine e;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 9}", "vin", "gnd");
    add_dev3(&e, "regulator", "U1", "{\"vout\": 3.3, \"dropout\": 0.3}", "vin", "vcc", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": 33}", "vcc", "gnd");
    engine_run(&e);
    CHECK_NEAR(v_of(&e, "vcc"), 3.3, 1e-6);
    CHECK_NEAR(obs_num(&e, "U1", "iout"), 0.1, 1e-6);
    CHECK_NEAR(obs_num(&e, "V1", "i"), -0.1, 1e-6); /* the load current comes from the input */
    esk_engine_free(&e);
    /* dropout: 3 V in gives 2.7 V out */
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 3}", "vin", "gnd");
    add_dev3(&e, "regulator", "U1", "{\"vout\": 3.3, \"dropout\": 0.3}", "vin", "vcc", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": 33}", "vcc", "gnd");
    engine_run(&e);
    CHECK_NEAR(v_of(&e, "vcc"), 2.7, 1e-6);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_pull_device(void)
{
    esk_engine e;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev(&e, "pull", "PU1", "{\"level\": 1, \"voltage\": 3.3, \"r\": \"10k\"}",
            (const char *const[]){"n"}, 1);
    add_dev2(&e, "resistor", "R1", "{\"r\": \"10k\"}", "n", "gnd");
    engine_run(&e);
    CHECK_NEAR(v_of(&e, "n"), 1.65, 1e-6);
    esk_engine_free(&e);
}

static void test_bounce(void)
{
    esk_engine e;
    esk_comp *w, *sw;
    watcher_state *ws;
    esk_json *v;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    sw =
        add_dev2(&e, "switch", "S1",
                 "{\"ron\": 1, \"bounce\": {\"count\": 4, \"interval\": \"100us\"}}", "btn", "gnd");
    w = add_watcher(&e, "U1", "btn", ESK_PULL_UP);
    engine_run(&e);
    ws = (watcher_state *)w->state;
    CHECK(esk_engine_advance(&e, ES_PS_PER_MS) == ES_OK);
    v = esk_json_new_bool(&A, 1);
    esk_engine_set_input(&e, sw, "pressed", v);
    esk_json_free(&A, v);
    esk_island_touch(esk_engine_net(&e, "btn")->island, esk_engine_now(&e));
    CHECK(esk_engine_advance(&e, 3 * ES_PS_PER_MS) == ES_OK);
    /* closed, open, closed, open, then closed for good: 5 crossings after the initial sample */
    CHECK_EQ(ws->calls, 6);
    CHECK_EQ(w->pins[0].sampled, ESK_L0);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

/* two islands in one engine: their step events must not clip each other to
 * 1 ps steps (the engine clips a step to the next event of any class) */
static void test_two_islands_step_independently(void)
{
    esk_engine e;
    esk_island *ia, *ib;
    engine_open(&e);
    add_power(&e, "GA", "gnda", 0.0);
    add_dev2(&e, "vsource", "VA", "{\"dc\": 5}", "ina", "gnda");
    add_dev2(&e, "resistor", "RA", "{\"r\": \"1k\"}", "ina", "outa");
    add_dev2(&e, "capacitor", "CA", "{\"c\": \"1u\", \"ic\": 0}", "outa", "gnda");
    add_power(&e, "GB", "gndb", 0.0);
    add_dev2(&e, "vsource", "VB", "{\"dc\": 3.3}", "inb", "gndb");
    add_dev2(&e, "resistor", "RB", "{\"r\": \"10k\"}", "inb", "outb");
    add_dev2(&e, "resistor", "RB2", "{\"r\": \"10k\"}", "outb", "gndb");
    engine_run(&e);
    ia = esk_engine_net(&e, "outa")->island;
    ib = esk_engine_net(&e, "outb")->island;
    CHECK(ia != NULL && ib != NULL && ia != ib);
    CHECK(esk_engine_advance(&e, 2 * ES_PS_PER_MS) == ES_OK);
    CHECK_NEAR(v_of(&e, "outa"), 5.0 * (1.0 - exp(-2.0)), 0.01 * 5.0);
    CHECK_NEAR(v_of(&e, "outb"), 1.65, 1e-6);
    CHECK(ia->steps < 200);
    CHECK(ib->steps < 60); /* a resistive island: the step grows to h_max */
    CHECK(ib->h_next_ps >= ES_PS_PER_MS / 2);
    CHECK(ia->t_accepted >= 2 * ES_PS_PER_MS && ib->t_accepted >= 2 * ES_PS_PER_MS);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

/* a board's rails: an ideal supply pin on the regulator's input net, MCU power
 * pins on its output net, pull-ups, an LED and an input */
static void test_regulator_board_rails(void)
{
    esk_engine e;
    esk_comp *w;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_power(&e, "USB", "5v", 5.0);
    add_dev3(&e, "regulator", "U2",
             "{\"vout\": 3.3, \"dropout\": 1.2, \"imax\": 0.8, \"quiescent\": \"5m\"}", "5v", "3v3",
             "gnd");
    add_power(&e, "VDD", "3v3", 3.3);
    add_power(&e, "VDDA", "3v3", 3.3);
    add_power(&e, "VBAT", "3v3", 3.3);
    add_dev(&e, "pull", "PU1", "{\"level\": 1, \"voltage\": 3.3, \"r\": \"10k\"}",
            (const char *const[]){"pc13"}, 1);
    add_dev(&e, "pull", "PU2", "{\"level\": 1, \"voltage\": 3.3, \"r\": \"10k\"}",
            (const char *const[]){"btn"}, 1);
    add_dev2(&e, "resistor", "R1", "{\"r\": 510}", "3v3", "leda");
    add_dev2(&e, "led", "D1", "{\"color\": \"red\"}", "leda", "gnd");
    add_dev2(&e, "resistor", "R2", "{\"r\": \"1k\"}", "pc13", "ledb");
    add_dev2(&e, "led", "D2", "{\"color\": \"green\"}", "ledb", "gnd");
    add_dev2(&e, "switch", "S1", "{\"ron\": 1}", "btn", "gnd");
    w = add_watcher(&e, "MCU", "btn", ESK_PULL_NONE);
    engine_run(&e);
    CHECK_NEAR(v_of(&e, "5v"), 5.0, 1e-9); /* held by the ideal source whatever the load */
    CHECK_NEAR(v_of(&e, "3v3"), 3.3, 1e-6);
    CHECK(v_of(&e, "leda") > 1.6 && v_of(&e, "leda") < 1.9);
    CHECK(v_of(&e, "btn") > 3.29);
    CHECK_EQ(w->pins[0].sampled, ESK_L1);
    /* the supply pin delivers the regulator's input current: load + quiescent */
    CHECK_NEAR(obs_num(&e, "U2", "iout"), (3.3 - v_of(&e, "leda")) / 510.0, 1e-6);
    CHECK_NEAR(esk_engine_comp(&e, "USB")->pins[0].bridge->i_last[0],
               -(obs_num(&e, "U2", "iout") + 5e-3), 1e-6);
    CHECK(esk_engine_advance(&e, 2 * ES_PS_PER_MS) == ES_OK);
    CHECK_NEAR(v_of(&e, "5v"), 5.0, 1e-9);
    CHECK_NEAR(v_of(&e, "3v3"), 3.3, 1e-6);
    CHECK(esk_engine_net(&e, "3v3")->island->steps < 100);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

/* the same rails with no source on the input: the input floats near 0 V, the
 * regulator is in dropout and nothing blows up */
static void test_regulator_unpowered(void)
{
    esk_engine e;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev3(&e, "regulator", "U2",
             "{\"vout\": 3.3, \"dropout\": 1.2, \"imax\": 0.8, \"quiescent\": \"5m\"}", "5v", "3v3",
             "gnd");
    add_power(&e, "VDD", "3v3", 3.3);
    add_dev2(&e, "resistor", "R1", "{\"r\": 510}", "3v3", "leda");
    add_dev2(&e, "led", "D1", "{\"color\": \"red\"}", "leda", "gnd");
    engine_run(&e);
    CHECK(fabs(v_of(&e, "5v")) < 1e-3);
    CHECK(fabs(v_of(&e, "3v3")) < 1e-3);
    CHECK(fabs(v_of(&e, "leda")) < 1e-3);
    CHECK(esk_engine_advance(&e, ES_PS_PER_MS) == ES_OK);
    CHECK(fabs(v_of(&e, "5v")) < 1e-3 && fabs(v_of(&e, "3v3")) < 1e-3);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
    /* an unpowered regulator whose output is pulled up externally: the output
     * cannot sink, so it follows the pull-up, and the input still floats */
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev3(&e, "regulator", "U2", "{\"vout\": 3.3, \"dropout\": 1.2}", "5v", "3v3", "gnd");
    add_dev(&e, "pull", "PU1", "{\"level\": 1, \"voltage\": 3.3, \"r\": \"10k\"}",
            (const char *const[]){"3v3"}, 1);
    engine_run(&e);
    CHECK(fabs(v_of(&e, "5v")) < 1e-3);
    CHECK(v_of(&e, "3v3") > 3.2 && v_of(&e, "3v3") < 3.31);
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
}

static void test_registry(void)
{
    size_t n;
    const esk_adev_ops *const *t = esk_adev_table(&n);
    CHECK(n >= 17);
    CHECK(t[0] == esk_adev_find("resistor"));
    CHECK(esk_adev_find("nmos") != NULL);
    CHECK(esk_adev_find("nope") == NULL);
    CHECK_STR(esk_bridge_in_ops.name, "bridge_in");
}

static void test_observe_json(void)
{
    esk_engine e;
    esk_json *o;
    engine_open(&e);
    add_power(&e, "G", "gnd", 0.0);
    add_dev2(&e, "vsource", "V1", "{\"dc\": 5}", "vin", "gnd");
    add_dev2(&e, "resistor", "R1", "{\"r\": \"1k\"}", "vin", "out");
    add_dev2(&e, "resistor", "R2", "{\"r\": 1000}", "out", "gnd");
    engine_run(&e);
    o = esk_island_observe(esk_engine_net(&e, "out")->island);
    CHECK(o != NULL);
    CHECK_NEAR(esk_json_get_num(esk_json_get(o, "nodes"), "out", 0.0), 2.5, 1e-6);
    CHECK_NEAR(esk_json_get_num(esk_json_get(esk_json_get(o, "devices"), "R2"), "i", 0.0), 2.5e-3,
               1e-9);
    esk_json_free(&A, o);
    esk_engine_free(&e);
}

/* ---- fixtures (tests/analog/fixtures/README.md) ---------------------------------- */

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = 0;
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static esk_json *load_json(const char *dir, const char *file)
{
    char path[512];
    size_t len = 0;
    char *text;
    esk_json *j;
    int line;
    const char *err;
    esk_snprintf(path, sizeof path, "%s/tests/analog/fixtures/%s/%s", EMBSIM_SOURCE_DIR, dir, file);
    text = read_file(path, &len);
    if (!text) {
        fprintf(stderr, "cannot read %s\n", path);
        return NULL;
    }
    j = esk_json_parse(&A, text, len, &line, &err);
    if (!j)
        fprintf(stderr, "%s:%d: %s\n", path, line, err);
    free(text);
    return j;
}

static int fixture_build(esk_engine *e, const esk_json *circuit)
{
    const esk_json *comps = esk_json_get(circuit, "components");
    const esk_json *supplies = esk_json_get(circuit, "supplies");
    const char *gnd = esk_json_get_str(circuit, "ground", NULL);
    size_t i;
    if (gnd)
        add_power(e, "__gnd", gnd, 0.0);
    for (i = 0; i < esk_json_len(supplies); i++) {
        const esk_json *s = esk_json_at(supplies, i);
        char id[64];
        double v = 0.0;
        esk_snprintf(id, sizeof id, "__sup%u", (unsigned)i);
        if (esk_json_to_double(s, &v) == 0)
            add_power(e, id, s->key, v);
    }
    for (i = 0; i < esk_json_len(comps); i++) {
        const esk_json *c = esk_json_at(comps, i);
        const esk_json *pins = esk_json_get(c, "pins");
        const esk_json *params = esk_json_get(c, "params");
        const char *nets[8];
        const char *names[8] = {"p0", "p1", "p2", "p3", "p4", "p5", "p6", "p7"};
        esk_pin_model models[8];
        const esk_model_ops *ops = ops_for(esk_json_get_str(c, "device", ""));
        esk_comp *comp = NULL;
        int np = (int)esk_json_len(pins), k;
        if (!ops || np > 8) {
            fprintf(stderr, "fixture: unknown device %s\n", esk_json_get_str(c, "device", "?"));
            return -1;
        }
        for (k = 0; k < np; k++) {
            nets[k] = esk_json_str(esk_json_at(pins, (size_t)k));
            esk_pin_model_family(NULL, &models[k]);
            models[k].kind = ESK_PIN_PASSIVE;
        }
        if (esk_engine_add_comp(e, esk_json_get_str(c, "id", "?"), ops->name, ops, names, models,
                                (uint32_t)np, params, NULL, &comp) != ES_OK)
            return -1;
        for (k = 0; k < np; k++)
            if (nets[k] && esk_engine_connect(e, &comp->pins[k], net_of(e, nets[k])) != ES_OK)
                return -1;
    }
    return 0;
}

static void fixture_check_nets(esk_engine *e, const esk_json *nets, double abs_tol, double rel_tol,
                               const char *when)
{
    size_t i;
    for (i = 0; i < esk_json_len(nets); i++) {
        const esk_json *n = esk_json_at(nets, i);
        double expect = 0.0, got;
        if (esk_json_to_double(n, &expect) != 0)
            continue;
        got = v_of(e, n->key);
        t_checks++;
        if (fabs(got - expect) > abs_tol + rel_tol * fabs(expect)) {
            t_failures++;
            fprintf(stderr, "fixture %s: net %s = %g, expected %g\n", when, n->key, got, expect);
        }
    }
}

static void fixture_check_devices(esk_engine *e, const esk_json *checks, const char *when)
{
    size_t i;
    for (i = 0; i < esk_json_len(checks); i++) {
        const esk_json *c = esk_json_at(checks, i);
        const char *dev = esk_json_get_str(c, "device", ""), *key = esk_json_get_str(c, "key", "");
        const esk_json *eq = esk_json_get(c, "equals");
        t_checks++;
        if (eq && eq->type == ESK_JSON_STRING) {
            char buf[64];
            if (strcmp(obs_str(e, dev, key, buf, sizeof buf), esk_json_str(eq)) != 0) {
                t_failures++;
                fprintf(stderr, "fixture %s: %s.%s = %s, expected %s\n", when, dev, key, buf,
                        esk_json_str(eq));
            }
        } else {
            double got = obs_num(e, dev, key);
            double lo = esk_json_get_num(c, "min", -1e300), hi = esk_json_get_num(c, "max", 1e300);
            if (eq) {
                double ev = 0.0, tol = esk_json_get_num(c, "tol", 0.0);
                esk_json_to_double(eq, &ev);
                lo = ev - tol;
                hi = ev + tol;
            }
            if (got < lo || got > hi) {
                t_failures++;
                fprintf(stderr, "fixture %s: %s.%s = %g, expected in [%g, %g]\n", when, dev, key,
                        got, lo, hi);
            }
        }
    }
}

static void run_fixture(const char *dir)
{
    esk_json *circuit = load_json(dir, "circuit.json"), *expected = load_json(dir, "expected.json");
    esk_engine e;
    const esk_json *samples, *tol;
    double abs_tol, rel_tol;
    size_t i;
    CHECK(circuit != NULL && expected != NULL);
    if (!circuit || !expected) {
        esk_json_free(&A, circuit);
        esk_json_free(&A, expected);
        return;
    }
    fprintf(stderr, "  fixture %s\n", dir);
    engine_open(&e);
    CHECK(fixture_build(&e, circuit) == 0);
    engine_run(&e);
    tol = esk_json_get(expected, "tolerance");
    abs_tol = esk_json_get_num(tol, "abs", 1e-3);
    rel_tol = esk_json_get_num(tol, "rel", 1e-2);
    fixture_check_nets(&e, esk_json_get(expected, "op"), abs_tol, rel_tol, "op");
    fixture_check_devices(&e, esk_json_get(expected, "op_devices"), "op");
    samples = esk_json_get(expected, "samples");
    for (i = 0; i < esk_json_len(samples); i++) {
        const esk_json *s = esk_json_at(samples, i);
        es_time_t t = 0;
        char when[64];
        if (esk_json_to_time(esk_json_get(s, "t"), &t) != 0) {
            FAIL("sample time does not parse");
            continue;
        }
        esk_snprintf(when, sizeof when, "%s@%s", dir, esk_json_get_str(s, "t", "?"));
        CHECK(esk_engine_advance(&e, t) == ES_OK);
        fixture_check_nets(&e, esk_json_get(s, "nets"), esk_json_get_num(s, "abs", abs_tol),
                           esk_json_get_num(s, "rel", rel_tol), when);
        fixture_check_devices(&e, esk_json_get(s, "devices"), when);
    }
    CHECK_EQ(diag_errors, 0);
    esk_engine_free(&e);
    esk_json_free(&A, circuit);
    esk_json_free(&A, expected);
}

static void test_fixtures(void)
{
    run_fixture("divider");
    run_fixture("rc-charge");
    run_fixture("rl");
    run_fixture("npn-switch");
    run_fixture("led-resistor");
    run_fixture("pwm-rc");
    run_fixture("pot-divider");
}

TEST_MAIN_BEGIN
esk_alloc_init(&A, NULL);
esk_diag_init(&D, &A, 64);
D.fn = on_diag;
init_model_ops();
RUN(test_registry);
RUN(test_divider);
RUN(test_supply_pin_divider);
RUN(test_implicit_ground);
RUN(test_observe_json);
RUN(test_rc_charge);
RUN(test_rc_with_ic);
RUN(test_rl);
RUN(test_rl_transient);
RUN(test_diode_curve);
RUN(test_diode_reverse_and_series);
RUN(test_led_resistor);
RUN(test_zener);
RUN(test_npn_switch);
RUN(test_pnp_switch);
RUN(test_nmos_switch);
RUN(test_pulse_rc);
RUN(test_sine_source);
RUN(test_pwl_source);
RUN(test_potentiometer);
RUN(test_switch_input);
RUN(test_comparator_bridge);
RUN(test_pull_up_input);
RUN(test_thevenin_bridge);
RUN(test_opamp_inverting);
RUN(test_regulator);
RUN(test_pull_device);
RUN(test_bounce);
RUN(test_two_islands_step_independently);
RUN(test_regulator_board_rails);
RUN(test_regulator_unpowered);
RUN(test_fixtures);
esk_diag_free(&D);
TEST_MAIN_END
