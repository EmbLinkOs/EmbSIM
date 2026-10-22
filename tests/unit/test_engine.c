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

static uint32_t t_tuned(void *ctx, int kind) { return kind == ESK_AIR_WIFI ? ((t_node *)ctx)->mhz : 0u; }
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
             "{\"ssid\": \"Lab\", \"password\": \"correct horse\", \"channel\": 11, \"bssid\": \"02:00:00:00:00:01\"}");
    esk_engine_reset(&e);
    t_init(&sta, 2462u);
    sta.answer = 1;
    CHECK(esk_air_attach(&e, &sta.n) == ES_OK);
    /* its first beacon (at 1 TU + its airtime): the SSID, rates, channel,
     * TIM, ERP and the RSN element of WPA2-Personal */
    esk_engine_advance(&e, 5 * ES_PS_PER_MS);
    CHECK_EQ(sta.got, 1);
    CHECK_EQ(sta.last[0], 0x80);
    CHECK(!memcmp(sta.last + 4, bcast, 6) && !memcmp(sta.last + 10, bssid, 6) && !memcmp(sta.last + 16, bssid, 6));
    CHECK_EQ(sta.last[32] | sta.last[33] << 8, 100);               /* the interval */
    CHECK_EQ(sta.last[34] | sta.last[35] << 8, 0x0431);            /* ESS, privacy, short preamble and slot */
    CHECK(sta.last[36] == 0 && sta.last[37] == 3 && !memcmp(sta.last + 38, "Lab", 3));
    CHECK_EQ(sta.last_len, 24 + 12 + 5 + 10 + 6 + 3 + 6 + 3 + 22);
    CHECK(sta.last[41] == 1 && sta.last[51] == 50 && sta.last[57] == 3 && sta.last[59] == 11);
    CHECK(sta.last[60] == 5 && sta.last[66] == 42 && sta.last[69] == 48);
    /* a probe request for any SSID is answered; one for another is not */
    t_send(&e, &sta, f, t_mgmt(f, 4, bcast, probe, sizeof probe), 10 * ES_PS_PER_MS);
    esk_engine_advance(&e, 15 * ES_PS_PER_MS);
    CHECK_EQ(sta.got, 2);
    CHECK_EQ(sta.last[0], 0x50);
    CHECK(!memcmp(sta.last + 4, f + 10, 6));
    CHECK_EQ(sta.last_len, 24 + 12 + 5 + 10 + 6 + 3 + 3 + 22); /* no TIM */
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

TEST_MAIN_BEGIN
esk_alloc_init(&A, NULL);
esk_diag_init(&D, &A, 64);
RUN(test_resolution);
RUN(test_floating);
RUN(test_gates_and_delay);
RUN(test_switch_gate);
RUN(test_virtual_terminal);
RUN(test_air);
RUN(test_wifi_ap);
esk_diag_free(&D);
CHECK_EQ(A.allocations, 0);
TEST_MAIN_END
