/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Digital engine tests: resolution and strengths (SPEC-003 §3), inertial
 * delays and gates (§5), transmission-gate switches, the virtual terminal. */
#include "harness.h"
#include "engine/engine.h"
#include "model/registry.h"
#include "util/num.h"
#include "util/wcrypto.h"

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

/* ---- the air (engine/air.h) ------------------------------------------------ */

typedef struct t_node {
    esk_air_node n;
    uint32_t mhz;
    int answer;            /* rx answers a unicast frame */
    int got;               /* frames received */
    es_time_t got_at;
    int rssi;
    uint8_t last[512];
    uint32_t last_len;
    int done;
    int acked;
    es_time_t done_at;
} t_node;

static uint32_t t_tuned(void *ctx, int kind) { return (kind & ((t_node *)ctx)->n.kind) ? ((t_node *)ctx)->mhz : 0u; }
static int t_rx(void *ctx, const esk_air_frame *f, int rssi, es_time_t at)
{
    t_node *t = (t_node *)ctx;
    t->got++;
    t->got_at = at;
    t->rssi = rssi;
    t->last_len = f->len < sizeof t->last ? f->len : (uint32_t)sizeof t->last;
    memcpy(t->last, f->data, t->last_len);
    return t->answer && !(f->data[4] & 1u);
}
static void t_done(void *ctx, uint32_t token, int acked, es_time_t at)
{
    t_node *t = (t_node *)ctx;
    (void)token;
    t->done++;
    t->acked = acked;
    t->done_at = at;
}
static void t_init(t_node *t, uint32_t mhz)
{
    memset(t, 0, sizeof *t);
    t->n.ctx = t;
    t->n.kind = ESK_AIR_WIFI;
    t->n.tuned = t_tuned;
    t->n.rx = t_rx;
    t->n.tx_done = t_done;
    t->mhz = mhz;
}
static void t_send(esk_engine *e, t_node *from, const uint8_t *f, uint32_t len, es_time_t start)
{
    esk_air_frame fr;
    fr.data = f;
    fr.len = len;
    fr.mhz = from->mhz;
    fr.dbm = 20.0;
    fr.rate_kbps = 1000u;
    fr.kind = ESK_AIR_WIFI;
    CHECK(esk_air_send(e, &from->n, &fr, start, esk_air_wifi_airtime(len, 1000u), 7u) == ES_OK);
}

static void test_air(void)
{
    esk_engine e;
    t_node a, b, c;
    esk_comp ca, cb;
    uint8_t unicast[24], group[24];
    const es_time_t us = ES_PS_PER_US;
    /* airtimes: DSSS (192 us of preamble and header, the bits at the rate)
     * and OFDM (20 us, then 4 us symbols with SERVICE and tail bits) */
    CHECK_EQ(esk_air_wifi_airtime(24, 1000u), 416 * us);
    CHECK_EQ(esk_air_wifi_airtime(24, 11000u), 192 * us + 224 * us / 11);
    CHECK_EQ(esk_air_wifi_airtime(24, 6000u), 64 * us);
    CHECK_EQ(esk_air_wifi_airtime(1500, 54000u), 244 * us);
    CHECK_EQ(esk_air_ble_airtime(10, 1000u), 144 * us);
    CHECK_EQ(esk_air_ble_airtime(10, 2000u), 76 * us);
    /* the channels */
    CHECK_EQ(esk_air_wifi_mhz(1), 2412);
    CHECK_EQ(esk_air_wifi_mhz(6), 2437);
    CHECK_EQ(esk_air_wifi_mhz(13), 2472);
    CHECK_EQ(esk_air_wifi_mhz(14), 2484);
    CHECK_EQ(esk_air_wifi_mhz(15), 0);
    CHECK_EQ(esk_air_wifi_channel(2437), 6);
    CHECK_EQ(esk_air_wifi_channel(2484), 14);
    CHECK_EQ(esk_air_wifi_channel(2413), 0);
    /* free-space path loss over the bench distance: 3 m at 2437 MHz is
     * 49.7 dB; nearer than 10 cm counts as 10 cm */
    memset(&ca, 0, sizeof ca);
    memset(&cb, 0, sizeof cb);
    ca.posed = cb.posed = 1;
    cb.pose[0] = 3000.0;
    t_init(&a, 2437u);
    t_init(&b, 2437u);
    a.n.comp = &ca;
    b.n.comp = &cb;
    CHECK_NEAR(esk_air_rssi(&a.n, &b.n, 20.0, 2437u), -29.73, 0.01);
    cb.pose[0] = 10.0;
    CHECK_NEAR(esk_air_rssi(&a.n, &b.n, 20.0, 2437u), 20.0 - (-20.0 + 67.738 - 27.55), 0.01);
    a.n.comp = b.n.comp = NULL; /* unplaced: a metre apart */
    CHECK_NEAR(esk_air_rssi(&a.n, &b.n, 0.0, 2412u), -40.10, 0.01);

    esk_engine_init(&e, &A, &D, ESK_POLICY_TIMED);
    esk_engine_reset(&e);
    t_init(&a, 2437u);
    t_init(&b, 2437u);
    t_init(&c, 2412u);
    b.answer = 1;
    CHECK(esk_air_attach(&e, &a.n) == ES_OK);
    CHECK(esk_air_attach(&e, &b.n) == ES_OK);
    CHECK(esk_air_attach(&e, &c.n) == ES_OK);
    memset(unicast, 0, sizeof unicast);
    unicast[4] = 0x02; /* address 1, unicast */
    memset(group, 0, sizeof group);
    group[4] = 0xFF;
    /* a unicast frame reaches the receivers on its channel when it ends;
     * one answers, and the sender hears so a SIFS and an ACK later */
    t_send(&e, &a, unicast, sizeof unicast, 100 * us);
    esk_engine_advance(&e, 2 * ES_PS_PER_MS);
    CHECK_EQ(b.got, 1);
    CHECK_EQ(b.got_at, 516 * us);
    CHECK_EQ(b.rssi, -20);
    CHECK_EQ(c.got, 0);
    CHECK_EQ(a.done, 1);
    CHECK_EQ(a.acked, 1);
    CHECK_EQ(a.done_at, (516 + 10 + 304) * us);
    /* no one answers: the sender learns it when the ACK's time is over */
    b.answer = 0;
    t_send(&e, &a, unicast, sizeof unicast, 3 * ES_PS_PER_MS);
    esk_engine_advance(&e, 4 * ES_PS_PER_MS);
    CHECK_EQ(a.done, 2);
    CHECK_EQ(a.acked, 0);
    CHECK_EQ(a.done_at, 3 * ES_PS_PER_MS + (416 + 10 + 304) * us);
    /* a group frame: no ACK, done when it ends */
    t_send(&e, &a, group, sizeof group, 5 * ES_PS_PER_MS);
    esk_engine_advance(&e, 6 * ES_PS_PER_MS);
    CHECK_EQ(b.got, 3);
    CHECK_EQ(a.done, 3);
    CHECK_EQ(a.acked, 0);
    CHECK_EQ(a.done_at, 5 * ES_PS_PER_MS + 416 * us);
    CHECK_EQ(e.air.frames, 3);
    /* out of range: 20 km away (-106 dBm) is below the receiver's sensitivity */
    ca.pose[0] = 0.0;
    cb.pose[0] = 20000000.0;
    a.n.comp = &ca;
    b.n.comp = &cb;
    t_send(&e, &a, group, sizeof group, 7 * ES_PS_PER_MS);
    esk_engine_advance(&e, 8 * ES_PS_PER_MS);
    CHECK_EQ(b.got, 3);
    /* a reset clears what was in flight */
    t_send(&e, &a, group, sizeof group, 9 * ES_PS_PER_MS);
    esk_engine_reset(&e);
    CHECK_EQ(e.air.frames, 0);
    esk_engine_advance(&e, 20 * ES_PS_PER_MS);
    CHECK_EQ(a.done, 4);
    esk_air_detach(&e, &a.n);
    esk_air_detach(&e, &b.n);
    esk_air_detach(&e, &c.n);
    CHECK_EQ(e.air.nodes.len, 0);
    esk_engine_free(&e);
}

/* a BLE packet is taken by the receivers tuned to it when its access
 * address ends, whole, with its end as its time */
/* two BLE packets on one channel at once collide: neither is taken when they
 * are as strong, the one 21 dB stronger is (the co-channel rejection) */
static void t_ble(esk_engine *e, t_node *from, uint8_t tag, double dbm, es_time_t start)
{
    uint8_t p[8] = { 0x02, 6, 0, 0, 0, 0, 0, 0 };
    esk_air_frame fr;
    p[2] = tag;
    fr.data = p;
    fr.len = sizeof p;
    fr.mhz = 2402u;
    fr.dbm = dbm;
    fr.rate_kbps = 1000u;
    fr.kind = ESK_AIR_BLE;
    CHECK(esk_air_send(e, &from->n, &fr, start, esk_air_ble_airtime(fr.len, 1000u), 1u) == ES_OK);
}

static void test_air_ble_collision(void)
{
    esk_engine e;
    t_node a, b, rx;
    esk_engine_init(&e, &A, &D, ESK_POLICY_FUNCTIONAL);
    esk_engine_reset(&e);
    t_init(&a, 2402u);
    t_init(&b, 2402u);
    t_init(&rx, 2402u);
    a.n.kind = b.n.kind = rx.n.kind = ESK_AIR_BLE;
    CHECK(esk_air_attach(&e, &a.n) == ES_OK);
    CHECK(esk_air_attach(&e, &b.n) == ES_OK);
    CHECK(esk_air_attach(&e, &rx.n) == ES_OK);
    t_ble(&e, &a, 0xA, 0.0, 1 * ES_PS_PER_MS);
    t_ble(&e, &b, 0xB, 0.0, 1 * ES_PS_PER_MS + 50u * ES_PS_PER_US); /* overlapping */
    esk_engine_advance(&e, 2 * ES_PS_PER_MS);
    CHECK_EQ(rx.got, 0);
    t_ble(&e, &a, 0xA, 0.0, 3 * ES_PS_PER_MS);
    t_ble(&e, &b, 0xB, -21.0, 3 * ES_PS_PER_MS); /* captured */
    esk_engine_advance(&e, 4 * ES_PS_PER_MS);
    CHECK_EQ(rx.got, 1);
    CHECK_EQ(rx.last[2], 0xAu);
    t_ble(&e, &a, 0xA, 0.0, 5 * ES_PS_PER_MS);
    t_ble(&e, &b, 0xB, 0.0, 5 * ES_PS_PER_MS + esk_air_ble_airtime(8u, 1000u)); /* one after the other */
    esk_engine_advance(&e, 6 * ES_PS_PER_MS);
    CHECK_EQ(rx.got, 3);
    esk_air_detach(&e, &a.n);
    esk_air_detach(&e, &b.n);
    esk_air_detach(&e, &rx.n);
    esk_engine_free(&e);
}

static void test_air_ble(void)
{
    static const uint8_t pdu[10] = { 0x02, 8, 1, 2, 3, 4, 5, 6, 7, 8 };
    const es_time_t us = ES_PS_PER_US;
    esk_engine e;
    t_node a, b, c;
    esk_air_frame fr;
    esk_engine_init(&e, &A, &D, ESK_POLICY_FUNCTIONAL);
    esk_engine_reset(&e);
    t_init(&a, 2402u);
    t_init(&b, 2402u);
    t_init(&c, 2426u);
    a.n.kind = b.n.kind = c.n.kind = ESK_AIR_BLE;
    CHECK(esk_air_attach(&e, &a.n) == ES_OK && esk_air_attach(&e, &b.n) == ES_OK && esk_air_attach(&e, &c.n) == ES_OK);
    fr.data = pdu;
    fr.len = sizeof pdu;
    fr.mhz = 2402u;
    fr.dbm = 0.0;
    fr.rate_kbps = 1000u;
    fr.kind = ESK_AIR_BLE;
    CHECK(esk_air_send(&e, &a.n, &fr, 100 * us, esk_air_ble_airtime(sizeof pdu, 1000u), 5u) == ES_OK);
    esk_engine_advance(&e, 139 * us);
    CHECK_EQ(b.got, 0);
    esk_engine_advance(&e, 141 * us); /* 1 byte of preamble, 4 of access address */
    CHECK_EQ(b.got, 1);
    CHECK_EQ(b.got_at, 244 * us);
    CHECK_EQ(c.got, 0);
    CHECK_EQ(a.done, 0);
    c.mhz = 2402u; /* too late for this one */
    b.mhz = 2480u;
    esk_engine_advance(&e, 300 * us);
    CHECK_EQ(c.got, 0);
    CHECK_EQ(a.done, 1);
    CHECK_EQ(a.done_at, 244 * us);
    esk_air_detach(&e, &a.n);
    esk_air_detach(&e, &b.n);
    esk_air_detach(&e, &c.n);
    esk_engine_free(&e);
}

/* a station's frame to the access point: management `subtype`, `body` */
static uint32_t t_mgmt(uint8_t *f, unsigned subtype, const uint8_t *bssid, const uint8_t *body, uint32_t n)
{
    static const uint8_t sta[6] = { 0x7c, 0xdf, 0xa1, 0x45, 0x6d, 0x62 };
    memset(f, 0, 24);
    f[0] = (uint8_t)(subtype << 4);
    memcpy(f + 4, bssid, 6);
    memcpy(f + 10, sta, 6);
    memcpy(f + 16, bssid, 6);
    memcpy(f + 24, body, n);
    return 24 + n;
}

/* a station's data frame to the distribution system: to `da`, `ethertype` */
static uint32_t t_data(uint8_t *f, const uint8_t *bssid, const uint8_t *da, int pm, unsigned ethertype,
                       const uint8_t *body, uint32_t n)
{
    static const uint8_t sta[6] = { 0x7c, 0xdf, 0xa1, 0x45, 0x6d, 0x62 };
    static const uint8_t snap[6] = { 0xAA, 0xAA, 0x03, 0, 0, 0 };
    memset(f, 0, 24);
    f[0] = 0x08;
    f[1] = (uint8_t)(0x01u | (pm ? 0x10u : 0u)); /* to DS, power management */
    memcpy(f + 4, bssid, 6);
    memcpy(f + 10, sta, 6);
    memcpy(f + 16, da, 6);
    memcpy(f + 24, snap, 6);
    f[30] = (uint8_t)(ethertype >> 8);
    f[31] = (uint8_t)ethertype;
    memcpy(f + 32, body, n);
    return 32 + n;
}

/* a UDP datagram in an IPv4 packet from 0.0.0.0 (or `src`) to `dst` */
static uint32_t t_udp(uint8_t *p, const uint8_t *src, const uint8_t *dst, unsigned sport, unsigned dport,
                      const uint8_t *body, uint32_t n)
{
    uint32_t tot = 28 + n, sum = 0, i;
    memset(p, 0, 28);
    p[0] = 0x45;
    p[2] = (uint8_t)(tot >> 8);
    p[3] = (uint8_t)tot;
    p[8] = 64;
    p[9] = 17;
    memcpy(p + 12, src, 4);
    memcpy(p + 16, dst, 4);
    for (i = 0; i < 20; i += 2)
        sum += (uint32_t)p[i] << 8 | p[i + 1];
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    p[10] = (uint8_t)(~sum >> 8);
    p[11] = (uint8_t)~sum;
    p[20] = (uint8_t)(sport >> 8);
    p[21] = (uint8_t)sport;
    p[22] = (uint8_t)(dport >> 8);
    p[23] = (uint8_t)dport;
    p[24] = (uint8_t)((8 + n) >> 8);
    p[25] = (uint8_t)(8 + n);
    memcpy(p + 28, body, n);
    return tot;
}

/* a DHCP client message of `type`, asking for `want` (or none) */
static uint32_t t_dhcp(uint8_t *m, unsigned type, const uint8_t *want)
{
    static const uint8_t sta[6] = { 0x7c, 0xdf, 0xa1, 0x45, 0x6d, 0x62 };
    uint32_t n = 240;
    memset(m, 0, 300);
    m[0] = 1;
    m[1] = 1;
    m[2] = 6;
    m[4] = 0x12;
    m[5] = 0x34;
    m[6] = 0x56;
    m[7] = 0x78;
    memcpy(m + 28, sta, 6);
    m[236] = 99;
    m[237] = 130;
    m[238] = 83;
    m[239] = 99;
    m[n++] = 53;
    m[n++] = 1;
    m[n++] = (uint8_t)type;
    if (want) {
        m[n++] = 50;
        m[n++] = 4;
        memcpy(m + n, want, 4);
        n += 4;
    }
    m[n++] = 255;
    return 300;
}

/* the DHCP message type of a reply frame from the AP (FromDS, LLC, IPv4, UDP 67 -> 68), 0 for none */
static unsigned t_dhcp_type(const uint8_t *f, uint32_t len)
{
    uint32_t i;
    if (len < 32 + 28 + 244 || f[0] != 0x08 || f[1] != 0x02 || f[30] != 0x08 || f[31] != 0x00 || f[32 + 9] != 17 ||
        f[32 + 23] != 68)
        return 0;
    for (i = 32 + 28 + 240; i + 2 < len && f[i] != 255; i += 2u + f[i + 1])
        if (f[i] == 53)
            return f[i + 2];
    return 0;
}

static void test_wifi_ap(void)
{
    static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    static const uint8_t bssid[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
    static const uint8_t probe[] = { 0, 0, 1, 4, 0x82, 0x84, 0x8b, 0x96 };   /* any SSID */
    static const uint8_t probe_other[] = { 0, 3, 'f', 'o', 'o' };
    static const uint8_t auth[] = { 0, 0, 1, 0, 0, 0 };                       /* open, transaction 1 */
    static const uint8_t assoc[] = { 0x21, 0x04, 10, 0, 0, 3, 'L', 'a', 'b' };
    esk_engine e;
    esk_registry r;
    esk_comp *ap;
    t_node sta;
    uint8_t f[64];
    esk_json *o;
    esk_engine_init(&e, &A, &D, ESK_POLICY_FUNCTIONAL);
    esk_registry_init(&r, &A);
    ap = add(&e, &r, "AP", "wifi_access_point", NULL, NULL, 0,
             "{\"ssid\": \"Lab\", \"channel\": 11, \"bssid\": \"02:00:00:00:00:01\"}");
    esk_engine_reset(&e);
    t_init(&sta, 2462u);
    sta.answer = 1;
    CHECK(esk_air_attach(&e, &sta.n) == ES_OK);
    /* its first beacon (at 2 TU + its airtime): the SSID, rates, channel,
     * TIM, ERP; an open network's */
    esk_engine_advance(&e, 5 * ES_PS_PER_MS);
    CHECK_EQ(sta.got, 1);
    CHECK_EQ(sta.last[0], 0x80);
    CHECK(!memcmp(sta.last + 4, bcast, 6) && !memcmp(sta.last + 10, bssid, 6) && !memcmp(sta.last + 16, bssid, 6));
    CHECK_EQ(sta.last[32] | sta.last[33] << 8, 100);               /* the interval */
    CHECK_EQ(sta.last[34] | sta.last[35] << 8, 0x0421);            /* ESS, short preamble and slot */
    CHECK(sta.last[36] == 0 && sta.last[37] == 3 && !memcmp(sta.last + 38, "Lab", 3));
    CHECK_EQ(sta.last_len, 24 + 12 + 5 + 10 + 6 + 3 + 6 + 3);
    CHECK(sta.last[41] == 1 && sta.last[51] == 50 && sta.last[57] == 3 && sta.last[59] == 11);
    CHECK(sta.last[60] == 5 && sta.last[66] == 42);
    /* a probe request for any SSID is answered; one for another is not */
    t_send(&e, &sta, f, t_mgmt(f, 4, bcast, probe, sizeof probe), 10 * ES_PS_PER_MS);
    esk_engine_advance(&e, 15 * ES_PS_PER_MS);
    CHECK_EQ(sta.got, 2);
    CHECK_EQ(sta.last[0], 0x50);
    CHECK(!memcmp(sta.last + 4, f + 10, 6));
    CHECK_EQ(sta.last_len, 24 + 12 + 5 + 10 + 6 + 3 + 3); /* no TIM */
    t_send(&e, &sta, f, t_mgmt(f, 4, bcast, probe_other, sizeof probe_other), 20 * ES_PS_PER_MS);
    esk_engine_advance(&e, 25 * ES_PS_PER_MS);
    CHECK_EQ(sta.got, 2);
    /* open system authentication, then association: AID 1 */
    t_send(&e, &sta, f, t_mgmt(f, 11, bssid, auth, sizeof auth), 30 * ES_PS_PER_MS);
    esk_engine_advance(&e, 35 * ES_PS_PER_MS);
    CHECK_EQ(sta.acked, 1);
    CHECK_EQ(sta.got, 3);
    CHECK(sta.last[0] == 0xB0 && sta.last[26] == 2 && sta.last[28] == 0);
    t_send(&e, &sta, f, t_mgmt(f, 0, bssid, assoc, sizeof assoc), 40 * ES_PS_PER_MS);
    esk_engine_advance(&e, 45 * ES_PS_PER_MS);
    CHECK_EQ(sta.got, 4);
    CHECK(sta.last[0] == 0x10 && sta.last[26] == 0 && sta.last[28] == 1 && sta.last[29] == 0xC0);
    o = esk_json_new_object(&A);
    ap->ops->observe(ap, o);
    CHECK_EQ(esk_json_get_num(o, "stations", -1), 1);
    CHECK_STR(esk_json_get_str(o, "mac", ""), "02:00:00:00:00:01");
    CHECK(strstr(esk_json_get_str(o, "log", ""), "7c:df:a1:45:6d:62 associated, aid 1") != NULL);
    esk_json_free(&A, o);
    /* ARP for the router: answered with the BSSID */
    {
        static const uint8_t gw[4] = { 192, 168, 1, 1 }, zero[4] = { 0, 0, 0, 0 }, all[4] = { 255, 255, 255, 255 };
        static const uint8_t want[4] = { 192, 168, 1, 100 }, other[4] = { 192, 168, 1, 77 };
        uint8_t arp[28], ip[400], m[300], d[512];
        uint32_t n;
        memset(arp, 0, sizeof arp);
        arp[1] = 1;
        arp[2] = 8;
        arp[4] = 6;
        arp[5] = 4;
        arp[7] = 1;
        arp[8] = 0x7c;
        arp[9] = 0xdf;
        arp[10] = 0xa1;
        arp[11] = 0x45;
        arp[12] = 0x6d;
        arp[13] = 0x62;
        arp[14] = 192;
        arp[15] = 168;
        arp[16] = 1;
        arp[17] = 100;
        memcpy(arp + 24, gw, 4);
        t_send(&e, &sta, d, t_data(d, bssid, bcast, 0, 0x0806, arp, sizeof arp), 46 * ES_PS_PER_MS);
        esk_engine_advance(&e, 49 * ES_PS_PER_MS);
        CHECK_EQ(sta.got, 5);
        CHECK(sta.last[0] == 0x08 && sta.last[1] == 0x02 && sta.last[30] == 0x08 && sta.last[31] == 0x06);
        CHECK(sta.last[39] == 2 && !memcmp(sta.last + 40, bssid, 6) && !memcmp(sta.last + 46, gw, 4));
        /* DHCP: DISCOVER, OFFER of 192.168.1.100; REQUEST, ACK; another address, NAK */
        n = t_udp(ip, zero, all, 68, 67, m, t_dhcp(m, 1, NULL));
        t_send(&e, &sta, d, t_data(d, bssid, bcast, 0, 0x0800, ip, n), 50 * ES_PS_PER_MS);
        esk_engine_advance(&e, 55 * ES_PS_PER_MS);
        CHECK_EQ(sta.got, 6);
        CHECK_EQ(t_dhcp_type(sta.last, sta.last_len), 2);
        CHECK(!memcmp(sta.last + 32 + 28 + 16, want, 4));
        CHECK(!memcmp(sta.last + 32 + 28 + 4, m + 4, 4)); /* its xid */
        n = t_udp(ip, zero, all, 68, 67, m, t_dhcp(m, 3, want));
        t_send(&e, &sta, d, t_data(d, bssid, bcast, 0, 0x0800, ip, n), 56 * ES_PS_PER_MS);
        esk_engine_advance(&e, 61 * ES_PS_PER_MS);
        CHECK_EQ(t_dhcp_type(sta.last, sta.last_len), 5);
        n = t_udp(ip, zero, all, 68, 67, m, t_dhcp(m, 3, other));
        t_send(&e, &sta, d, t_data(d, bssid, bcast, 0, 0x0800, ip, n), 62 * ES_PS_PER_MS);
        esk_engine_advance(&e, 67 * ES_PS_PER_MS);
        CHECK_EQ(t_dhcp_type(sta.last, sta.last_len), 6);
        o = esk_json_new_object(&A);
        ap->ops->observe(ap, o);
        CHECK(strstr(esk_json_get_str(o, "log", ""), "7c:df:a1:45:6d:62 leased 192.168.1.100") != NULL);
        esk_json_free(&A, o);
        /* a ping to the router is answered, its checksum right */
        memset(ip, 0, 28 + 8);
        ip[0] = 0x45;
        ip[3] = 36;
        ip[8] = 64;
        ip[9] = 1;
        memcpy(ip + 12, want, 4);
        memcpy(ip + 16, gw, 4);
        {
            uint32_t sum = 0, i;
            for (i = 0; i < 20; i += 2)
                sum += (uint32_t)ip[i] << 8 | ip[i + 1];
            while (sum >> 16)
                sum = (sum & 0xFFFFu) + (sum >> 16);
            ip[10] = (uint8_t)(~sum >> 8);
            ip[11] = (uint8_t)~sum;
            ip[20] = 8; /* echo request */
            ip[24] = 0;
            ip[25] = 0;
            ip[28] = 'p';
            ip[29] = 'i';
            ip[30] = 'n';
            ip[31] = 'g';
            ip[22] = 0x87; /* ~(0x0800 + 'pi' + 'ng') */
            ip[23] = 0x29;
        }
        t_send(&e, &sta, d, t_data(d, bssid, bssid, 0, 0x0800, ip, 36), 68 * ES_PS_PER_MS);
        esk_engine_advance(&e, 71 * ES_PS_PER_MS);
        CHECK(sta.last[32 + 9] == 1 && sta.last[32 + 20] == 0 && !memcmp(sta.last + 32 + 16, want, 4));
        CHECK(!memcmp(sta.last + 32 + 28, "ping", 4));
        {
            uint32_t sum = 0, i;
            for (i = 0; i < 16; i += 2)
                sum += (uint32_t)sta.last[52 + i] << 8 | sta.last[52 + i + 1];
            while (sum >> 16)
                sum = (sum & 0xFFFFu) + (sum >> 16);
            CHECK_EQ(sum, 0xFFFF);
        }
        /* dozing: the ARP reply is kept, announced in the TIM, sent on a PS-Poll */
        n = (uint32_t)sta.got;
        t_send(&e, &sta, d, t_data(d, bssid, bcast, 1, 0x0806, arp, sizeof arp), 72 * ES_PS_PER_MS);
        esk_engine_advance(&e, 76 * ES_PS_PER_MS);
        CHECK_EQ(sta.got, (int)n);
        esk_engine_advance(&e, 106 * ES_PS_PER_MS); /* the beacon at 2 TU + 100 TU, 104.448 ms */
        CHECK_EQ(sta.got, (int)n + 1);
        CHECK_EQ(sta.last[0], 0x80);
        CHECK(sta.last[60] == 5 && sta.last[61] == 4 && sta.last[64] == 0 && sta.last[65] == 0x02); /* AID 1 */
        memset(d, 0, 16);
        d[0] = 0xA4; /* PS-Poll */
        d[2] = 1;
        d[3] = 0xC0;
        memcpy(d + 4, bssid, 6);
        d[10] = 0x7c;
        d[11] = 0xdf;
        d[12] = 0xa1;
        d[13] = 0x45;
        d[14] = 0x6d;
        d[15] = 0x62;
        t_send(&e, &sta, d, 16, 107 * ES_PS_PER_MS);
        esk_engine_advance(&e, 109 * ES_PS_PER_MS);
        CHECK_EQ(sta.got, (int)n + 2);
        CHECK(sta.last[0] == 0x08 && sta.last[31] == 0x06 && !(sta.last[1] & 0x20u));
    }
    /* a deauthentication forgets it */
    t_send(&e, &sta, f, t_mgmt(f, 12, bssid, auth, 2), 110 * ES_PS_PER_MS);
    esk_engine_advance(&e, 115 * ES_PS_PER_MS);
    o = esk_json_new_object(&A);
    ap->ops->observe(ap, o);
    CHECK_EQ(esk_json_get_num(o, "stations", -1), 0);
    esk_json_free(&A, o);
    /* beacons go on every 102.4 ms */
    esk_engine_advance(&e, 1000 * ES_PS_PER_MS);
    o = esk_json_new_object(&A);
    ap->ops->observe(ap, o);
    CHECK_EQ(esk_json_get_num(o, "beacons", -1), 10);
    esk_json_free(&A, o);
    esk_air_detach(&e, &sta.n);
    esk_engine_free(&e);
    esk_registry_free(&r);
}

/* a BLE advertiser for the scanner: ADV_IND (or `type`) from `addr` with a
 * Complete Local Name */
static void t_adv(esk_engine *e, t_node *from, unsigned type, const uint8_t addr[6], const char *name, es_time_t start)
{
    static uint8_t p[40];
    esk_air_frame fr;
    uint32_t i, n = (uint32_t)strlen(name), len = 6u + 3u + 2u + n;
    p[0] = (uint8_t)type;
    p[1] = (uint8_t)len;
    for (i = 0; i < 6; i++)
        p[2 + i] = addr[5 - i]; /* AdvA, little-endian */
    p[8] = 2;
    p[9] = 1;
    p[10] = 6;
    p[11] = (uint8_t)(n + 1u);
    p[12] = 9;
    memcpy(p + 13, name, n);
    fr.data = p;
    fr.len = len + 2u;
    fr.mhz = from->mhz;
    fr.dbm = 0.0;
    fr.rate_kbps = 1000u;
    fr.kind = ESK_AIR_BLE;
    CHECK(esk_air_send(e, &from->n, &fr, start, esk_air_ble_airtime(fr.len, 1000u), 1u) == ES_OK);
}

/* the scanner (ble_scanner.c): one advertising channel each interval, in
 * turn; the advertisers it heard, as a phone's scanner app lists them */
static void test_ble_scanner(void)
{
    static const uint8_t a1[6] = { 0x7c, 0xdf, 0xa1, 0x45, 0x6d, 0x64 };
    static const uint8_t a2[6] = { 0xc0, 0x11, 0x22, 0x33, 0x44, 0x55 };
    esk_engine e;
    esk_registry r;
    esk_comp *sc, *sc2;
    t_node adv;
    esk_json *o;
    const char *list;
    esk_engine_init(&e, &A, &D, ESK_POLICY_FUNCTIONAL);
    esk_registry_init(&r, &A);
    sc = add(&e, &r, "PHONE", "ble_scanner", NULL, NULL, 0, NULL);
    sc2 = add(&e, &r, "PHONE2", "ble_scanner", NULL, NULL, 0, "{\"window\": 10, \"interval\": 30}");
    CHECK(sc != NULL && sc2 != NULL);
    esk_engine_reset(&e);
    t_init(&adv, 2402u);
    adv.n.kind = ESK_AIR_BLE;
    CHECK(esk_air_attach(&e, &adv.n) == ES_OK);
    /* on 37 for the first 30 ms; the second scanner only for 10 */
    t_adv(&e, &adv, 0, a1, "nimble-bleprph", 1 * ES_PS_PER_MS);
    adv.mhz = 2426u; /* 38: not heard now */
    t_adv(&e, &adv, 0, a1, "nimble-bleprph", 2 * ES_PS_PER_MS);
    adv.mhz = 2402u;
    t_adv(&e, &adv, 0x42, a2, "tag", 15 * ES_PS_PER_MS); /* ADV_NONCONN_IND, random */
    esk_engine_advance(&e, 20 * ES_PS_PER_MS);
    o = esk_json_new_object(&A);
    sc->ops->observe(sc, o);
    CHECK_EQ(esk_json_get_num(o, "devices", -1), 2);
    CHECK_EQ(esk_json_get_num(o, "pdus", -1), 2);
    list = esk_json_get_str(o, "list", "");
    CHECK(strstr(list, "7c:df:a1:45:6d:64 (public) ADV_IND -40dBm connectable \"nimble-bleprph\"\n") != NULL);
    CHECK(strstr(list, "c0:11:22:33:44:55 (random) ADV_NONCONN_IND -40dBm - \"tag\"\n") != NULL);
    esk_json_free(&A, o);
    o = esk_json_new_object(&A);
    sc2->ops->observe(sc2, o);
    CHECK_EQ(esk_json_get_num(o, "pdus", -1), 1); /* its window had closed */
    esk_json_free(&A, o);
    /* then on 38 */
    adv.mhz = 2426u;
    t_adv(&e, &adv, 0, a1, "nimble-bleprph", 32 * ES_PS_PER_MS);
    adv.mhz = 2402u;
    t_adv(&e, &adv, 0, a1, "nimble-bleprph", 33 * ES_PS_PER_MS);
    esk_engine_advance(&e, 40 * ES_PS_PER_MS);
    o = esk_json_new_object(&A);
    sc->ops->observe(sc, o);
    CHECK_EQ(esk_json_get_num(o, "devices", -1), 2);
    CHECK_EQ(esk_json_get_num(o, "pdus", -1), 3);
    esk_json_free(&A, o);
    /* an iBeacon (Apple's manufacturer data) on 38: decoded */
    {
        static const uint8_t ib[] = { 0x00, 6 + 3 + 27, 0x55, 0x44, 0x33, 0x22, 0x11, 0xc0, 2, 1, 0x1a, 26, 0xff,
                                      0x4c, 0x00, 0x02, 0x15, 0x2d, 0x7a, 0x9f, 0x0c, 0xe0, 0xe8, 0x4c, 0xc9, 0xa7,
                                      0x1b, 0xa2, 0x1d, 0xb2, 0xd0, 0x34, 0xa1, 0x00, 0x05, 0x00, 0x58, 0xc5 };
        esk_air_frame fr;
        fr.data = ib;
        fr.len = sizeof ib;
        fr.mhz = 2426u;
        fr.dbm = 0.0;
        fr.rate_kbps = 1000u;
        fr.kind = ESK_AIR_BLE;
        CHECK(esk_air_send(&e, &adv.n, &fr, 45 * ES_PS_PER_MS, esk_air_ble_airtime(fr.len, 1000u), 1u) == ES_OK);
        esk_engine_advance(&e, 50 * ES_PS_PER_MS);
        o = esk_json_new_object(&A);
        sc->ops->observe(sc, o);
        CHECK(strstr(esk_json_get_str(o, "list", ""),
                     "c0:11:22:33:44:55 (public) ADV_IND -40dBm connectable \"\" iBeacon "
                     "2D7A9F0C-E0E8-4CC9-A71B-A21DB2D034A1 major 5 minor 88 -59dBm@1m\n") != NULL);
        esk_json_free(&A, o);
    }
    esk_air_detach(&e, &adv.n);
    esk_engine_free(&e);
    esk_registry_free(&r);
}

/* active scanning (ble_scanner.c): a SCAN_REQ T_IFS after a scannable
 * PDU, the SCAN_RSP's name taken; no request after a non-scannable one;
 * the backoff's limit doubled after two requests unanswered */
static void test_ble_active_scan(void)
{
    static const uint8_t a1[6] = { 0x7c, 0xdf, 0xa1, 0x45, 0x6d, 0x64 };
    static const uint8_t me[6] = { 0xf2, 0x4b, 0x9c, 0x01, 0x23, 0x45 };
    esk_engine e;
    esk_registry r;
    esk_comp *sc;
    t_node adv;
    esk_json *o;
    es_time_t req_end;
    uint32_t i;
    esk_engine_init(&e, &A, &D, ESK_POLICY_FUNCTIONAL);
    esk_registry_init(&r, &A);
    sc = add(&e, &r, "PHONE", "ble_scanner", NULL, NULL, 0, NULL);
    CHECK(sc != NULL);
    esk_engine_reset(&e);
    t_init(&adv, 2402u);
    adv.n.kind = ESK_AIR_BLE;
    CHECK(esk_air_attach(&e, &adv.n) == ES_OK);
    t_adv(&e, &adv, 0, a1, "esp", 1 * ES_PS_PER_MS);
    esk_engine_advance(&e, 1600 * ES_PS_PER_US);
    /* the request: random ScanA, public AdvA */
    CHECK_EQ(adv.got, 1);
    CHECK_EQ(adv.last_len, 14u);
    CHECK_EQ(adv.last[0], 0x43u);
    CHECK_EQ(adv.last[1], 12u);
    for (i = 0; i < 6; i++) {
        CHECK_EQ(adv.last[2 + i], me[5 - i]);
        CHECK_EQ(adv.last[8 + i], a1[5 - i]);
    }
    req_end = 1 * ES_PS_PER_MS + esk_air_ble_airtime(6u + 3u + 2u + 3u + 2u, 1000u) + 150u * ES_PS_PER_US +
              esk_air_ble_airtime(14u, 1000u);
    CHECK_EQ(adv.got_at, req_end);
    /* the response T_IFS later: its name joins the advertiser's */
    {
        static const char name[] = "Long name works now";
        uint8_t p[40];
        esk_air_frame fr;
        uint32_t n = (uint32_t)strlen(name);
        p[0] = 0x04;
        p[1] = (uint8_t)(6u + 2u + n);
        for (i = 0; i < 6; i++)
            p[2 + i] = a1[5 - i];
        p[8] = (uint8_t)(n + 1u);
        p[9] = 0x09;
        memcpy(p + 10, name, n);
        fr.data = p;
        fr.len = 10u + n;
        fr.mhz = 2402u;
        fr.dbm = 0.0;
        fr.rate_kbps = 1000u;
        fr.kind = ESK_AIR_BLE;
        CHECK(esk_air_send(&e, &adv.n, &fr, req_end + 150u * ES_PS_PER_US, esk_air_ble_airtime(fr.len, 1000u), 1u) ==
              ES_OK);
    }
    esk_engine_advance(&e, 3 * ES_PS_PER_MS);
    o = esk_json_new_object(&A);
    sc->ops->observe(sc, o);
    CHECK_EQ(esk_json_get_num(o, "requests", -1), 1);
    CHECK_EQ(esk_json_get_num(o, "pdus", -1), 2);
    CHECK(strstr(esk_json_get_str(o, "list", ""),
                 "7c:df:a1:45:6d:64 (public) ADV_IND -40dBm connectable \"Long name works now\"\n") != NULL);
    esk_json_free(&A, o);
    /* not after ADV_NONCONN_IND */
    t_adv(&e, &adv, 2, a1, "esp", 5 * ES_PS_PER_MS);
    esk_engine_advance(&e, 6 * ES_PS_PER_MS);
    CHECK_EQ(adv.got, 1);
    /* unanswered: a request each time until two fail, then a count from 1 to 2 */
    t_adv(&e, &adv, 0, a1, "esp", 7 * ES_PS_PER_MS);
    esk_engine_advance(&e, 8 * ES_PS_PER_MS);
    CHECK_EQ(adv.got, 2);
    t_adv(&e, &adv, 0, a1, "esp", 9 * ES_PS_PER_MS);
    esk_engine_advance(&e, 10 * ES_PS_PER_MS);
    CHECK_EQ(adv.got, 3);
    for (i = 0; i < 8; i++) {
        t_adv(&e, &adv, 0, a1, "esp", (11 + 2 * (es_time_t)i) * ES_PS_PER_MS);
        esk_engine_advance(&e, (12 + 2 * (es_time_t)i) * ES_PS_PER_MS);
    }
    CHECK(adv.got < 3 + 8);
    esk_air_detach(&e, &adv.n);
    esk_engine_free(&e);
    esk_registry_free(&r);
}

/* a peripheral the test plays for the phone (ble_central.c): it follows
 * the connection's channels, answers each packet T_IFS later with the
 * sequence numbers, and serves a battery service */
typedef struct t_slave {
    esk_air_node n;
    esk_engine *e;
    es_time_t anchor0, interval;
    int connected, silent;
    uint8_t ci[36];
    int sn, nesn;
    uint8_t q[8][32];
    uint8_t qlen[8];
    int qh, qn, sent_data;
    int packets;
} t_slave;

static uint32_t sl_mhz(uint32_t ch) { return ch <= 10u ? 2404u + 2u * ch : 2428u + 2u * (ch - 11u); }

static uint32_t sl_tuned(void *ctx, int kind)
{
    t_slave *s = (t_slave *)ctx;
    es_time_t now = s->e->sched.now;
    uint32_t k;
    if (kind != ESK_AIR_BLE || s->silent)
        return 0;
    if (!s->connected)
        return 2402u;
    if (now + ES_PS_PER_MS < s->anchor0)
        return 0;
    k = (uint32_t)((now + s->interval / 2u - s->anchor0) / s->interval); /* the event */
    return sl_mhz(((k + 1u) * 7u) % 37u);
}

static void sl_att(t_slave *s, const uint8_t *a, uint32_t n)
{
    uint8_t r[32];
    uint32_t len = 0;
    if (!n || s->qn == 8)
        return;
    if (a[0] == 0x02) { /* MTU: 100 */
        r[0] = 0x03, r[1] = 100, r[2] = 0, len = 3;
    } else if (a[0] == 0x10 && (a[1] | a[2] << 8) == 1) { /* the battery service, 1-3 */
        static const uint8_t svc[8] = { 0x11, 6, 1, 0, 3, 0, 0x0F, 0x18 };
        memcpy(r, svc, sizeof svc), len = sizeof svc;
    } else if (a[0] == 0x08 && (a[1] | a[2] << 8) == 1) { /* its level, readable, at 3 */
        static const uint8_t chr[9] = { 0x09, 7, 2, 0, 0x02, 3, 0, 0x19, 0x2A };
        memcpy(r, chr, sizeof chr), len = sizeof chr;
    } else if (a[0] == 0x0A && (a[1] | a[2] << 8) == 3) {
        r[0] = 0x0B, r[1] = 100, len = 2;
    } else if (a[0] == 0x10 || a[0] == 0x08) { /* nothing more */
        r[0] = 0x01, r[1] = a[0], r[2] = a[1], r[3] = a[2], r[4] = 0x0A, len = 5;
    }
    if (!len)
        return;
    s->q[(s->qh + s->qn) % 8][0] = (uint8_t)len;
    s->q[(s->qh + s->qn) % 8][1] = 0;
    s->q[(s->qh + s->qn) % 8][2] = 4;
    s->q[(s->qh + s->qn) % 8][3] = 0;
    memcpy(s->q[(s->qh + s->qn) % 8] + 4, r, len);
    s->qlen[(s->qh + s->qn) % 8] = (uint8_t)(len + 4u);
    s->qn++;
}

static int sl_rx(void *ctx, const esk_air_frame *f, int rssi, es_time_t at)
{
    t_slave *s = (t_slave *)ctx;
    const uint8_t *p = f->data;
    uint8_t out[40];
    esk_air_frame fr;
    uint32_t n = 0;
    (void)rssi;
    if (!s->connected) {
        if ((p[0] & 15u) == 5u && f->len == 36u) { /* CONNECT_IND */
            memcpy(s->ci, p, 36);
            s->connected = 1;
            s->interval = (es_time_t)(p[24] | p[25] << 8) * 1250u * ES_PS_PER_US;
            s->anchor0 = at + 1250u * ES_PS_PER_US;
        }
        return 0;
    }
    s->packets++;
    if (((p[0] >> 2) & 1u) != (unsigned)s->sn) { /* ours was received */
        if (s->sent_data && s->qn) {
            s->qh = (s->qh + 1) % 8;
            s->qn--;
        }
        s->sn ^= 1;
    }
    if (((p[0] >> 3) & 1u) == (unsigned)s->nesn) { /* new */
        s->nesn ^= 1;
        if ((p[0] & 3u) == 2u && p[1] >= 4u)
            sl_att(s, p + 6, p[1] - 4u);
    }
    s->sent_data = s->qn > 0;
    if (s->qn) {
        out[n++] = (uint8_t)(2u | (unsigned)s->nesn << 2 | (unsigned)s->sn << 3);
        out[n++] = s->qlen[s->qh];
        memcpy(out + n, s->q[s->qh], s->qlen[s->qh]);
        n += s->qlen[s->qh];
    } else {
        out[n++] = (uint8_t)(1u | (unsigned)s->nesn << 2 | (unsigned)s->sn << 3);
        out[n++] = 0;
    }
    fr.data = out;
    fr.len = n;
    fr.mhz = f->mhz;
    fr.dbm = 0.0;
    fr.rate_kbps = 1000u;
    fr.kind = ESK_AIR_BLE;
    CHECK(esk_air_send(s->e, &s->n, &fr, at + 150u * ES_PS_PER_US, esk_air_ble_airtime(n, 1000u), 9u) == ES_OK);
    return 0;
}

/* the phone (ble_central.c) against it: the CONNECT_IND, the connection's
 * channels and timing, GATT discovery, the supervision timeout */
static void test_ble_central(void)
{
    static const uint8_t dev[6] = { 0x7c, 0xdf, 0xa1, 0x45, 0x6d, 0x64 };
    esk_engine e;
    esk_registry r;
    esk_comp *ph;
    t_slave sl;
    t_node adv;
    esk_json *o;
    esk_engine_init(&e, &A, &D, ESK_POLICY_FUNCTIONAL);
    esk_registry_init(&r, &A);
    ph = add(&e, &r, "PHONE", "ble_central", NULL, NULL, 0, "{\"target\": \"dev\", \"timeout\": 200}");
    CHECK(ph != NULL);
    esk_engine_reset(&e);
    memset(&sl, 0, sizeof sl);
    sl.n.ctx = &sl;
    sl.n.kind = ESK_AIR_BLE;
    sl.n.tuned = sl_tuned;
    sl.n.rx = sl_rx;
    sl.e = &e;
    CHECK(esk_air_attach(&e, &sl.n) == ES_OK);
    /* its advertising, on 37, from the slave's own node */
    t_init(&adv, 2402u);
    adv.n.kind = ESK_AIR_BLE;
    {
        static uint8_t p[16];
        uint32_t i;
        esk_air_frame fr;
        p[0] = 0; /* ADV_IND, public */
        p[1] = 6 + 5;
        for (i = 0; i < 6; i++)
            p[2 + i] = dev[5 - i];
        p[8] = 4, p[9] = 9, p[10] = 'd', p[11] = 'e', p[12] = 'v';
        fr.data = p;
        fr.len = 13;
        fr.mhz = 2402u;
        fr.dbm = 0.0;
        fr.rate_kbps = 1000u;
        fr.kind = ESK_AIR_BLE;
        CHECK(esk_air_send(&e, &sl.n, &fr, ES_PS_PER_MS, esk_air_ble_airtime(13, 1000u), 1u) == ES_OK);
    }
    esk_engine_advance(&e, 2 * ES_PS_PER_MS);
    CHECK(sl.connected);
    /* the CONNECT_IND: the phone's random address, the device's, 30 ms,
     * latency 0, 200 ms, all 37 channels, hop 7 */
    CHECK_EQ(sl.ci[0], 0x05 | 0x40);
    CHECK_EQ(sl.ci[1], 34);
    CHECK(sl.ci[2] == 0x33 && sl.ci[7] == 0xc4);
    CHECK(sl.ci[8] == 0x64 && sl.ci[13] == 0x7c);
    CHECK_EQ(sl.ci[24] | sl.ci[25] << 8, 24);
    CHECK_EQ(sl.ci[26] | sl.ci[27] << 8, 0);
    CHECK_EQ(sl.ci[28] | sl.ci[29] << 8, 20);
    CHECK(sl.ci[30] == 0xFF && sl.ci[34] == 0x1F);
    CHECK_EQ(sl.ci[35], 7);
    /* the discovery: the MTU, the services, their characteristics, the reads */
    esk_engine_advance(&e, 600 * ES_PS_PER_MS);
    o = esk_json_new_object(&A);
    ph->ops->observe(ph, o);
    CHECK_STR(esk_json_get_str(o, "state", ""), "connected");
    CHECK_STR(esk_json_get_str(o, "peer", ""), "7c:df:a1:45:6d:64");
    CHECK_EQ(esk_json_get_num(o, "mtu", -1), 100);
    CHECK_EQ(esk_json_get_num(o, "services", -1), 1);
    CHECK_STR(esk_json_get_str(o, "gatt", ""), "service 0x180F handles 1-3\n  characteristic 0x2A19 handle 3 [ read ] = 64\n");
    CHECK(strstr(esk_json_get_str(o, "log", ""), "GATT database discovered: 1 services") != NULL);
    CHECK(esk_json_get_num(o, "events", -1) >= 19);
    esk_json_free(&A, o);
    CHECK(sl.packets >= 19);
    /* the device goes silent: lost after the supervision timeout */
    sl.silent = 1;
    esk_engine_advance(&e, 900 * ES_PS_PER_MS);
    o = esk_json_new_object(&A);
    ph->ops->observe(ph, o);
    CHECK_STR(esk_json_get_str(o, "state", ""), "disconnected");
    CHECK(strstr(esk_json_get_str(o, "log", ""), "connection lost (supervision timeout)") != NULL);
    esk_json_free(&A, o);
    esk_air_detach(&e, &sl.n);
    esk_engine_free(&e);
    esk_registry_free(&r);
}

/* WPA2-Personal: the test plays the station's supplicant through the 4-way
 * handshake, then sends and receives CCMP frames */
static void test_wifi_ap_wpa2(void)
{
    static const uint8_t bssid[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
    static const uint8_t me[6] = { 0x7c, 0xdf, 0xa1, 0x45, 0x6d, 0x62 };
    static const uint8_t auth[] = { 0, 0, 1, 0, 0, 0 };
    static const uint8_t assoc[] = { 0x31, 0x04, 10, 0, 0, 3, 'L', 'a', 'b', 48, 20, 1, 0, 0x00, 0x0f, 0xac, 4,
                                     1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 2, 0, 0 };
    static const uint8_t gw[4] = { 192, 168, 1, 1 };
    esk_engine e;
    esk_registry r;
    esk_comp *ap;
    t_node sta;
    uint8_t f[600], pmk[32], ptk[48], snonce[32], anonce[32], eap[200], mic[20], kd[64], gtk[16], plain[600];
    uint8_t data[76];
    uint32_t n, i;
    long pl;
    uint64_t pn;
    esk_json *o;
    esk_engine_init(&e, &A, &D, ESK_POLICY_FUNCTIONAL);
    esk_registry_init(&r, &A);
    ap = add(&e, &r, "AP", "wifi_access_point", NULL, NULL, 0,
             "{\"ssid\": \"Lab\", \"password\": \"correct horse\", \"channel\": 11, \"bssid\": \"02:00:00:00:00:01\"}");
    esk_engine_reset(&e);
    t_init(&sta, 2462u);
    sta.answer = 1;
    CHECK(esk_air_attach(&e, &sta.n) == ES_OK);
    esk_engine_advance(&e, 5 * ES_PS_PER_MS);
    CHECK_EQ(sta.last[34] | sta.last[35] << 8, 0x0431); /* privacy */
    CHECK(sta.last[69] == 48 && sta.last[70] == 20);    /* the RSN element: CCMP, PSK */
    t_send(&e, &sta, f, t_mgmt(f, 11, bssid, auth, sizeof auth), 10 * ES_PS_PER_MS);
    t_send(&e, &sta, f, t_mgmt(f, 0, bssid, assoc, sizeof assoc), 12 * ES_PS_PER_MS);
    esk_engine_advance(&e, 20 * ES_PS_PER_MS);
    /* message 1: ANonce, replay counter 1, ACK, no MIC */
    CHECK(sta.last[0] == 0x08 && sta.last[30] == 0x88 && sta.last[31] == 0x8E && !(sta.last[1] & 0x40u));
    memcpy(eap, sta.last + 32, sta.last_len - 32);
    CHECK(eap[1] == 3 && eap[4] == 2 && eap[5] == 0x00 && eap[6] == 0x8A && eap[16] == 1);
    memcpy(anonce, eap + 17, 32);
    /* the supplicant: PMK, SNonce, PTK; message 2 with its MIC */
    esk_pbkdf2_sha1((const uint8_t *)"correct horse", 13, (const uint8_t *)"Lab", 3, 4096, pmk, 32);
    memset(snonce, 0x5A, sizeof snonce);
    memcpy(data, bssid, 6); /* the BSSID is the smaller address */
    memcpy(data + 6, me, 6);
    if (memcmp(anonce, snonce, 32) < 0) {
        memcpy(data + 12, anonce, 32);
        memcpy(data + 44, snonce, 32);
    } else {
        memcpy(data + 12, snonce, 32);
        memcpy(data + 44, anonce, 32);
    }
    esk_prf_sha1(pmk, 32, "Pairwise key expansion", data, sizeof data, ptk, 48);
    memset(eap, 0, 99 + 22);
    eap[0] = 1;
    eap[1] = 3;
    eap[3] = 95 + 22;
    eap[4] = 2;
    eap[5] = 0x01;
    eap[6] = 0x0A; /* MIC, pairwise, version 2 */
    eap[16] = 1;
    memcpy(eap + 17, snonce, 32);
    eap[98] = 22;
    memcpy(eap + 99, assoc + 9, 22);
    esk_hmac_sha1(ptk, 16, eap, 99 + 22, mic);
    memcpy(eap + 81, mic, 16);
    t_send(&e, &sta, f, t_data(f, bssid, bssid, 0, 0x888E, eap, 99 + 22), 30 * ES_PS_PER_MS);
    esk_engine_advance(&e, 40 * ES_PS_PER_MS);
    /* message 3: install, secure, its MIC right, the GTK wrapped under the KEK */
    memcpy(eap, sta.last + 32, sta.last_len - 32);
    CHECK(eap[5] == 0x13 && eap[6] == 0xCA && eap[16] == 2);
    memcpy(mic, eap + 81, 16);
    memset(eap + 81, 0, 16);
    {
        uint8_t m2[20];
        esk_hmac_sha1(ptk, 16, eap, sta.last_len - 32, m2);
        CHECK(!memcmp(m2, mic, 16));
    }
    n = (uint32_t)eap[97] << 8 | eap[98];
    CHECK_EQ(n, 56);
    CHECK_EQ(esk_aes_unwrap(ptk + 16, eap + 99, n / 8 - 1, kd), 0);
    CHECK(kd[0] == 48 && kd[22] == 0xdd && kd[24] == 0x00 && kd[27] == 1 && kd[28] == 1);
    memcpy(gtk, kd + 30, 16);
    /* message 4 */
    memset(eap, 0, 99);
    eap[0] = 1;
    eap[1] = 3;
    eap[3] = 95;
    eap[4] = 2;
    eap[5] = 0x03;
    eap[6] = 0x0A; /* secure, MIC, pairwise */
    eap[16] = 2;
    esk_hmac_sha1(ptk, 16, eap, 99, mic);
    memcpy(eap + 81, mic, 16);
    t_send(&e, &sta, f, t_data(f, bssid, bssid, 0, 0x888E, eap, 99), 45 * ES_PS_PER_MS);
    esk_engine_advance(&e, 50 * ES_PS_PER_MS);
    o = esk_json_new_object(&A);
    ap->ops->observe(ap, o);
    CHECK(strstr(esk_json_get_str(o, "log", ""), "7c:df:a1:45:6d:62 keys installed (WPA2-PSK, CCMP)") != NULL);
    esk_json_free(&A, o);
    /* an ARP request under CCMP is answered under CCMP, PN 1 */
    {
        uint8_t arp[28], clear[64];
        size_t cl;
        memset(arp, 0, sizeof arp);
        arp[1] = 1;
        arp[2] = 8;
        arp[4] = 6;
        arp[5] = 4;
        arp[7] = 1;
        memcpy(arp + 8, me, 6);
        memcpy(arp + 24, gw, 4);
        n = t_data(clear, bssid, bssid, 0, 0x0806, arp, sizeof arp);
        memcpy(f, clear, 24);
        f[1] |= 0x40u;
        cl = esk_ccmp_encrypt(ptk + 32, f, 24, 1, 0, clear + 24, n - 24, f + 24);
        t_send(&e, &sta, f, (uint32_t)(24 + cl), 55 * ES_PS_PER_MS);
        esk_engine_advance(&e, 60 * ES_PS_PER_MS);
        CHECK(sta.last[0] == 0x08 && (sta.last[1] & 0x40u));
        pl = esk_ccmp_decrypt(ptk + 32, sta.last, 24, sta.last + 24, sta.last_len - 24, plain, &pn);
        CHECK_EQ(pl, 8 + 28);
        CHECK_EQ(pn, 1);
        CHECK(plain[6] == 0x08 && plain[7] == 0x06 && plain[15] == 2 && !memcmp(plain + 16, bssid, 6));
        /* a replayed frame (the same PN) is dropped */
        n = (uint32_t)sta.got;
        t_send(&e, &sta, f, (uint32_t)(24 + cl), 65 * ES_PS_PER_MS);
        esk_engine_advance(&e, 70 * ES_PS_PER_MS);
        CHECK_EQ(sta.got, (int)n);
        /* a frame in the clear is dropped too */
        t_send(&e, &sta, clear, (uint32_t)(24 + 8 + 28), 75 * ES_PS_PER_MS);
        esk_engine_advance(&e, 80 * ES_PS_PER_MS);
        CHECK_EQ(sta.got, (int)n);
    }
    for (n = 0, i = 0; i < 16; i++)
        n |= gtk[i];
    CHECK(n != 0); /* a group key was given */
    esk_air_detach(&e, &sta.n);
    esk_engine_free(&e);
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
RUN(test_air);
RUN(test_air_ble);
RUN(test_air_ble_collision);
RUN(test_wifi_ap);
RUN(test_wifi_ap_wpa2);
RUN(test_ble_scanner);
RUN(test_ble_active_scan);
RUN(test_ble_central);
esk_diag_free(&D);
CHECK_EQ(A.allocations, 0);
TEST_MAIN_END
