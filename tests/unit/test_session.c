/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Session tests: a library written to a temporary directory, a project
 * loaded through the loader (SPEC-005/007), inputs through the command
 * path, snapshots, a scenario with assertions, and the in-process protocol. */
#include "harness.h"
#include "host/host.h"
#include "session/session_priv.h"
#include "util/num.h"

static char *lib_dir;
static void show_diag(void *ctx, const es_diagnostic_t *d)
{
    ES_UNUSED(ctx);
    if (d->severity >= ES_SEV_ERROR)
        fprintf(stderr, "  diag %s: %s\n", d->code, d->message);
}

static void write_file(const char *rel, const char *text)
{
    esk_alloc a;
    char *path;
    esk_alloc_init(&a, NULL);
    path = esk_host_path_join(&a, lib_dir, rel);
    CHECK(path != NULL);
    if (path) {
        char *dir = esk_host_dirname(&a, path);
        CHECK(dir && esk_host_mkdir_p(dir) == ES_OK);
        CHECK(esk_host_write_file(path, text, strlen(text)) == ES_OK);
        esk_strfree(&a, dir);
        esk_strfree(&a, path);
    }
}

static const char *VCC = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"t/vcc\",\"version\":\"1.0.0\",\"name\":\"VCC\",\"category\":\"power\","
    "\"pins\":[{\"name\":\"+\",\"number\":1,\"model\":{\"kind\":\"power\",\"role\":\"supply\",\"nominal_v\":\"$voltage\"}}],"
    "\"params\":{\"voltage\":{\"type\":\"number\",\"unit\":\"V\",\"default\":3.3,\"min\":0,\"max\":60}},"
    "\"model\":{\"primitive\":\"wire\"},\"functional\":{\"primitive\":\"wire\"},"
    "\"symbol\":{\"svg\":\"symbol.svg\",\"width\":20,\"height\":20,\"pins\":{\"+\":{\"x\":10,\"y\":20,\"side\":\"bottom\"}}},"
    "\"fidelity\":{\"functional\":{\"claims\":\"supply level\",\"omissions\":[]}},\"provenance\":{\"author\":\"test\",\"license\":\"Apache-2.0\"}}";
static const char *BUTTON = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"t/button\",\"version\":\"1.0.0\",\"name\":\"Button\",\"category\":\"switches\","
    "\"family\":\"cmos_3v3\",\"pins\":[{\"name\":\"1\",\"number\":1,\"model\":{\"kind\":\"digital_io\"}},{\"name\":\"2\",\"number\":2,\"model\":{\"kind\":\"digital_io\"}}],"
    "\"params\":{},\"inputs\":[{\"name\":\"pressed\",\"type\":\"boolean\",\"default\":false}],\"outputs\":[{\"name\":\"closed\",\"type\":\"boolean\"}],"
    "\"model\":{\"primitive\":\"switch\"},\"functional\":{\"primitive\":\"push_button_digital\"},"
    "\"symbol\":{\"svg\":\"symbol.svg\",\"width\":60,\"height\":30,\"pins\":{\"1\":{\"x\":0,\"y\":15,\"side\":\"left\"},\"2\":{\"x\":60,\"y\":15,\"side\":\"right\"}}},"
    "\"fidelity\":{\"functional\":{\"claims\":\"open/closed\",\"omissions\":[]}},\"provenance\":{\"author\":\"test\",\"license\":\"Apache-2.0\"}}";
static const char *PULL = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"t/pull_down\",\"version\":\"1.0.0\",\"name\":\"Pull-down\",\"category\":\"passive\","
    "\"family\":\"cmos_3v3\",\"pins\":[{\"name\":\"1\",\"number\":1,\"model\":{\"kind\":\"digital_out\",\"strength\":\"weak\"}}],"
    "\"params\":{\"resistance\":{\"type\":\"number\",\"unit\":\"ohm\",\"default\":10000}},"
    "\"model\":{\"primitive\":\"pull\",\"params\":{\"level\":0},\"bind\":{\"r\":\"resistance\"}},\"functional\":{\"primitive\":\"pull\",\"params\":{\"level\":0}},"
    "\"symbol\":{\"svg\":\"symbol.svg\",\"width\":20,\"height\":40,\"pins\":{\"1\":{\"x\":10,\"y\":0,\"side\":\"top\"}}},"
    "\"fidelity\":{\"functional\":{\"claims\":\"weak 0\",\"omissions\":[]}},\"provenance\":{\"author\":\"test\",\"license\":\"Apache-2.0\"}}";
static const char *LED = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"t/led\",\"version\":\"1.0.0\",\"name\":\"LED\",\"category\":\"optoelectronics\","
    "\"family\":\"cmos_3v3\",\"pins\":[{\"name\":\"A\",\"number\":1,\"model\":{\"kind\":\"digital_in\"}},{\"name\":\"K\",\"number\":2,\"model\":{\"kind\":\"digital_in\"},\"required\":false}],"
    "\"params\":{\"color\":{\"type\":\"enum\",\"enum\":[\"red\",\"green\"],\"default\":\"red\"}},\"outputs\":[{\"name\":\"lit\",\"type\":\"boolean\"}],"
    "\"model\":{\"primitive\":\"led\"},\"functional\":{\"primitive\":\"indicator\"},"
    "\"symbol\":{\"svg\":\"symbol.svg\",\"width\":40,\"height\":20,\"pins\":{\"A\":{\"x\":0,\"y\":10,\"side\":\"left\"},\"K\":{\"x\":40,\"y\":10,\"side\":\"right\"}}},"
    "\"fidelity\":{\"functional\":{\"claims\":\"lit on 1\",\"omissions\":[]}},\"provenance\":{\"author\":\"test\",\"license\":\"Apache-2.0\"}}";
static const char *GND = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"t/gnd\",\"version\":\"1.0.0\",\"name\":\"GND\",\"category\":\"power\","
    "\"pins\":[{\"name\":\"-\",\"number\":1,\"model\":{\"kind\":\"power\",\"role\":\"ground\"}}],\"params\":{},"
    "\"model\":{\"primitive\":\"wire\"},\"functional\":{\"primitive\":\"wire\"},"
    "\"symbol\":{\"svg\":\"symbol.svg\",\"width\":20,\"height\":20,\"pins\":{\"-\":{\"x\":10,\"y\":0,\"side\":\"top\"}}},"
    "\"fidelity\":{\"functional\":{\"claims\":\"ground\",\"omissions\":[]}},\"provenance\":{\"author\":\"test\",\"license\":\"Apache-2.0\"}}";
static const char *INVERTER = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"t/inverter\",\"version\":\"1.0.0\",\"name\":\"Inverter\",\"category\":\"logic\","
    "\"family\":\"cmos_3v3\",\"pins\":[{\"name\":\"A\",\"number\":1,\"model\":{\"kind\":\"digital_in\"}},{\"name\":\"Y\",\"number\":2,\"model\":{\"kind\":\"digital_out\"}}],"
    "\"params\":{},\"model\":{\"primitive\":\"inverter\"},\"functional\":{\"primitive\":\"inverter\"},"
    "\"symbol\":{\"svg\":\"symbol.svg\",\"width\":40,\"height\":20,\"pins\":{\"A\":{\"x\":0,\"y\":10,\"side\":\"left\"},\"Y\":{\"x\":40,\"y\":10,\"side\":\"right\"}}},"
    "\"fidelity\":{\"functional\":{\"claims\":\"not\",\"omissions\":[]}},\"provenance\":{\"author\":\"test\",\"license\":\"Apache-2.0\"}}";
/* a composite: two inverters in series, with a parameter passed down */
static const char *DOUBLE_INV = "{\"schema_version\":1,\"kind\":\"component\",\"id\":\"t/double_inv\",\"version\":\"1.0.0\",\"name\":\"Double inverter\",\"category\":\"logic\","
    "\"family\":\"cmos_3v3\",\"pins\":[{\"name\":\"IN\",\"number\":1,\"model\":{\"kind\":\"digital_in\"}},{\"name\":\"OUT\",\"number\":2,\"model\":{\"kind\":\"digital_out\"}}],"
    "\"params\":{},\"model\":{\"composite\":{\"components\":[{\"id\":\"I1\",\"component\":\"t/inverter\"},{\"id\":\"I2\",\"component\":\"t/inverter\"}],"
    "\"nets\":{\"M\":[\"I1.Y\",\"I2.A\"]},\"ports\":{\"IN\":\"I1.A\",\"OUT\":\"I2.Y\"}}},"
    "\"symbol\":{\"svg\":\"symbol.svg\",\"width\":60,\"height\":20,\"pins\":{\"IN\":{\"x\":0,\"y\":10,\"side\":\"left\"},\"OUT\":{\"x\":60,\"y\":10,\"side\":\"right\"}}},"
    "\"fidelity\":{\"functional\":{\"claims\":\"buffer\",\"omissions\":[]}},\"provenance\":{\"author\":\"test\",\"license\":\"Apache-2.0\"}}";
static const char *COMPOSITE_PROJECT = "{\"schema_version\":1,\"kind\":\"project\",\"name\":\"composite\",\"policy\":\"functional\",\"libraries\":[\"LIBDIR\"],"
    "\"components\":[{\"id\":\"V1\",\"component\":\"t/vcc\"},{\"id\":\"X1\",\"component\":\"t/double_inv\"},{\"id\":\"D1\",\"component\":\"t/led\"}],"
    "\"nets\":{\"IN\":[\"V1.+\",\"X1.IN\"],\"OUT\":[\"X1.OUT\",\"D1.A\"]}}";

static const char *PROJECT = "{\"schema_version\":1,\"kind\":\"project\",\"name\":\"button-led\",\"policy\":\"functional\",\"libraries\":[\"LIBDIR\"],"
    "\"components\":[{\"id\":\"V1\",\"component\":\"t/vcc\",\"params\":{\"voltage\":5}},{\"id\":\"SW1\",\"component\":\"t/button\"},"
    "{\"id\":\"R1\",\"component\":\"t/pull_down\"},{\"id\":\"D1\",\"component\":\"t/led\",\"params\":{\"color\":\"green\"}},{\"id\":\"G1\",\"component\":\"t/gnd\"}],"
    "\"nets\":{\"VCC\":[\"V1.+\",\"SW1.1\"],\"SIG\":[\"SW1.2\",\"R1.1\",\"D1.A\"],\"GND\":[\"G1.-\",\"D1.K\"]},"
    "\"probes\":[{\"id\":\"sig\",\"target\":\"net:SIG\",\"kind\":\"digital\"},{\"id\":\"lit\",\"target\":\"output:D1.lit\",\"kind\":\"value\",\"rate_hz\":100000}]}";
static const char *SCENARIO = "{\"schema_version\":1,\"kind\":\"scenario\",\"name\":\"press\",\"until\":\"50ms\","
    "\"inputs\":[{\"at\":\"10ms\",\"target\":\"SW1\",\"input\":\"pressed\",\"value\":true},{\"at\":\"30ms\",\"target\":\"SW1\",\"input\":\"pressed\",\"value\":false}],"
    "\"assertions\":[{\"id\":\"low-before\",\"at\":\"5ms\",\"probe\":\"sig\",\"expect\":{\"logic\":\"0\"}},"
    "{\"id\":\"high-while\",\"between\":[\"11ms\",\"29ms\"],\"probe\":\"sig\",\"expect\":{\"logic\":\"1\"}},"
    "{\"id\":\"lit\",\"at\":\"20ms\",\"probe\":\"lit\",\"expect\":{\"equals\":1}},"
    "{\"id\":\"edges\",\"between\":[\"0ms\",\"50ms\"],\"probe\":\"sig\",\"expect\":{\"edges\":2}},"
    "{\"id\":\"clean\",\"always\":true,\"diagnostics\":{\"forbid\":[\"net.contention\"]}}]}";

static char *subst(const char *tmpl)
{
    esk_alloc a;
    esk_strbuf b;
    const char *p = strstr(tmpl, "LIBDIR");
    const char *c;
    esk_alloc_init(&a, NULL);
    esk_strbuf_init(&b, &a);
    esk_strbuf_append(&b, tmpl, (size_t)(p - tmpl));
    /* the path goes inside a JSON string: escape Windows backslashes and quotes */
    for (c = lib_dir; *c; c++) {
        if (*c == '\\' || *c == '"')
            esk_strbuf_putc(&b, '\\');
        esk_strbuf_putc(&b, *c);
    }
    esk_strbuf_puts(&b, p + 6);
    return esk_strbuf_detach(&b, NULL);
}

static void setup_library(void)
{
    write_file("library.json", "{\"schema_version\":1,\"kind\":\"library\",\"name\":\"t\",\"version\":\"0.0.1\",\"license\":\"Apache-2.0\"}");
    write_file("components/vcc/component.json", VCC);
    write_file("components/button/component.json", BUTTON);
    write_file("components/pull_down/component.json", PULL);
    write_file("components/led/component.json", LED);
    write_file("components/gnd/component.json", GND);
    write_file("components/inverter/component.json", INVERTER);
    write_file("components/double_inv/component.json", DOUBLE_INV);
    write_file("components/led/symbol.svg", "<svg xmlns='http://www.w3.org/2000/svg'/>");
}

static void test_load_and_run(void)
{
    es_session *s = NULL;
    es_session_config_t cfg;
    char *proj = subst(PROJECT);
    esk_json *snap;
    es_time_t at;
    uint64_t seq;
    esk_json v;
    memset(&cfg, 0, sizeof cfg);
    CHECK(es_session_create(&cfg, &s) == ES_OK);
    es_session_set_diagnostic_handler(s, show_diag, NULL);
    CHECK(es_session_load_json(s, proj, strlen(proj), lib_dir) == ES_OK);
    CHECK(s->eng.comps.len == 5);
    CHECK(s->eng.nets.len == 3);
    CHECK(esk_net_value(esk_engine_net(&s->eng, "SIG")) == ESK_L0);
    CHECK(esk_net_value(esk_engine_net(&s->eng, "VCC")) == ESK_L1);
    CHECK(esk_engine_comp(&s->eng, "V1")->pins[0].model.nominal_v == 5.0); /* $voltage substituted */
    memset(&v, 0, sizeof v); v.type = ESK_JSON_BOOL; v.u.boolean = 1;
    CHECK(esk_session_apply_input(s, "SW1", "pressed", &v, 10 * ES_PS_PER_MS, &at, &seq) == ES_OK);
    CHECK(seq == 1);
    CHECK(es_session_advance(s, 5 * ES_PS_PER_MS) == ES_OK);
    CHECK(esk_net_value(esk_engine_net(&s->eng, "SIG")) == ESK_L0);
    CHECK(es_session_advance(s, 20 * ES_PS_PER_MS) == ES_OK);
    CHECK(esk_net_value(esk_engine_net(&s->eng, "SIG")) == ESK_L1);
    snap = esk_session_snapshot(s, NULL);
    CHECK(snap != NULL);
    if (snap) {
        const esk_json *d1 = esk_json_get(esk_json_get(snap, "components"), "D1");
        CHECK(esk_json_get_bool(esk_json_get(d1, "outputs"), "lit", 0) == 1);
        CHECK_STR(esk_json_get_str(esk_json_get(esk_json_get(snap, "nets"), "SIG"), "logic", ""), "1");
        esk_json_free(s->a, snap);
    }
    /* past input is refused */
    CHECK(esk_session_apply_input(s, "SW1", "pressed", &v, 1, NULL, NULL) == ES_ETIME);
    /* probes captured the transition */
    {
        esk_probe *p = esk_session_probe_find(s, "sig");
        CHECK(p != NULL && p->len == 2);
        if (p) CHECK(p->ring[1].time_ps == 10 * ES_PS_PER_MS);
    }
    es_session_destroy(s);
    free(proj);
}

static void test_scenario_and_rpc(void)
{
    es_session *s = NULL;
    es_session_config_t cfg;
    char *proj = subst(PROJECT);
    esk_json *sc, *report;
    int line;
    const char *err;
    memset(&cfg, 0, sizeof cfg);
    CHECK(es_session_create(&cfg, &s) == ES_OK);
    CHECK(es_session_load_json(s, proj, strlen(proj), lib_dir) == ES_OK);
    sc = esk_json_parse(s->a, SCENARIO, strlen(SCENARIO), &line, &err);
    CHECK(sc != NULL);
    report = esk_scenario_run(s, sc, lib_dir, 0);
    CHECK(report != NULL);
    if (report) {
        const esk_json *as = esk_json_get(report, "assertions");
        size_t i;
        CHECK(esk_json_get_bool(report, "ok", 0) == 1);
        CHECK(esk_json_len(as) == 5);
        for (i = 0; i < esk_json_len(as); i++)
            if (!esk_json_get_bool(esk_json_at(as, i), "ok", 0))
                fprintf(stderr, "assertion %s failed: %s\n", esk_json_get_str(esk_json_at(as, i), "id", ""), esk_json_get_str(esk_json_at(as, i), "reason", ""));
        esk_json_free(s->a, report);
    }
    esk_json_free(s->a, sc);
    /* the protocol in process */
    {
        const char *reqs[] = {
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"session.open\",\"params\":{\"client\":\"test\",\"protocol\":\"1.0\"}}",
            "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"control.reset\",\"params\":{}}",
            "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"input.press\",\"params\":{\"target\":\"SW1\"}}",
            "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"control.advance\",\"params\":{\"by\":\"1ms\"}}",
            "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"observe.snapshot\",\"params\":{\"include\":[\"nets\"]}}",
            "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"nope\",\"params\":{}}",
            "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"debug.registers\",\"params\":{\"board\":\"B1\"}}",
            "not json",
        };
        size_t i;
        for (i = 0; i < ES_COUNTOF(reqs); i++) {
            char *res = NULL;
            size_t len = 0;
            esk_json *j;
            CHECK(es_session_rpc(s, reqs[i], strlen(reqs[i]), &res, &len) == ES_OK);
            j = esk_json_parse(s->a, res, len, &line, &err);
            CHECK(j != NULL);
            if (j) {
                if (i == 4)
                    CHECK_STR(esk_json_get_str(esk_json_get(esk_json_get(esk_json_get(j, "result"), "nets"), "SIG"), "logic", ""), "1");
                if (i == 3)
                    CHECK_STR(esk_json_get_str(esk_json_get(j, "result"), "now_ps", ""), "1000000000");
                if (i == 5)
                    CHECK(esk_json_get_int(esk_json_get(j, "error"), "code", 0) == -32601);
                if (i == 6) /* this project has no board B1 */
                    CHECK(esk_json_get_int(esk_json_get(j, "error"), "code", 0) == -32602);
                if (i == 7)
                    CHECK(esk_json_get_int(esk_json_get(j, "error"), "code", 0) == -32700);
                if (i < 5)
                    CHECK(esk_json_get(j, "error") == NULL);
                esk_json_free(s->a, j);
            }
            es_free(res);
        }
    }
    es_session_destroy(s);
    free(proj);
}

static void test_determinism(void)
{
    /* the same scenario twice in one session, then in a fresh session: identical reports (SPEC-002 §7) */
    es_session *s = NULL, *s2 = NULL;
    es_session_config_t cfg;
    char *proj = subst(PROJECT);
    esk_json *sc, *r1, *r2, *r3;
    int line;
    const char *err;
    char *t1, *t2, *t3;
    size_t l1, l2, l3;
    memset(&cfg, 0, sizeof cfg);
    CHECK(es_session_create(&cfg, &s) == ES_OK);
    CHECK(es_session_create(&cfg, &s2) == ES_OK);
    CHECK(es_session_load_json(s, proj, strlen(proj), lib_dir) == ES_OK);
    CHECK(es_session_load_json(s2, proj, strlen(proj), lib_dir) == ES_OK);
    sc = esk_json_parse(s->a, SCENARIO, strlen(SCENARIO), &line, &err);
    r1 = esk_scenario_run(s, sc, lib_dir, 0);
    r2 = esk_scenario_run(s, sc, lib_dir, 0);
    r3 = esk_scenario_run(s2, sc, lib_dir, 0);
    CHECK(r1 && r2 && r3);
    if (r1 && r2 && r3) {
        /* wall time differs by nature */
        esk_json_object_set(s->a, esk_json_get(r1, "statistics") ? (esk_json *)esk_json_get(r1, "statistics") : r1, "wall_time_s", esk_json_new_number(s->a, 0));
        esk_json_object_set(s->a, (esk_json *)esk_json_get(r2, "statistics"), "wall_time_s", esk_json_new_number(s->a, 0));
        esk_json_object_set(s2->a, (esk_json *)esk_json_get(r3, "statistics"), "wall_time_s", esk_json_new_number(s2->a, 0));
        t1 = esk_json_format(s->a, r1, 0, &l1);
        t2 = esk_json_format(s->a, r2, 0, &l2);
        t3 = esk_json_format(s2->a, r3, 0, &l3);
        CHECK(t1 && t2 && t3 && !strcmp(t1, t2) && !strcmp(t1, t3));
        if (t1 && t2 && strcmp(t1, t2)) { size_t i = 0; while (t1[i] && t1[i] == t2[i]) i++; fprintf(stderr, "run1 vs run2 differ at %zu:\n  %.120s\n  %.120s\n", i, t1 + (i > 40 ? i - 40 : 0), t2 + (i > 40 ? i - 40 : 0)); }
        if (t1 && t3 && strcmp(t1, t3)) { size_t i = 0; while (t1[i] && t1[i] == t3[i]) i++; fprintf(stderr, "run1 vs fresh differ at %zu:\n  %.120s\n  %.120s\n", i, t1 + (i > 40 ? i - 40 : 0), t3 + (i > 40 ? i - 40 : 0)); }
        esk_free(s->a, t1, l1 + 1);
        esk_free(s->a, t2, l2 + 1);
        esk_free(s2->a, t3, l3 + 1);
    }
    esk_json_free(s->a, r1);
    esk_json_free(s->a, r2);
    esk_json_free(s2->a, r3);
    esk_json_free(s->a, sc);
    es_session_destroy(s);
    es_session_destroy(s2);
    free(proj);
}

static void test_composite(void)
{
    es_session *s = NULL;
    es_session_config_t cfg;
    char *proj = subst(COMPOSITE_PROJECT);
    memset(&cfg, 0, sizeof cfg);
    CHECK(es_session_create(&cfg, &s) == ES_OK);
    es_session_set_diagnostic_handler(s, show_diag, NULL);
    CHECK(es_session_load_json(s, proj, strlen(proj), lib_dir) == ES_OK);
    CHECK(esk_engine_comp(&s->eng, "X1.I1") != NULL);
    CHECK(esk_engine_comp(&s->eng, "X1.I2") != NULL);
    CHECK(esk_engine_net(&s->eng, "X1.M") != NULL);
    CHECK(es_session_advance(s, 1000) == ES_OK);
    CHECK(esk_net_value(esk_engine_net(&s->eng, "IN")) == ESK_L1);
    CHECK(esk_net_value(esk_engine_net(&s->eng, "X1.M")) == ESK_L0);
    CHECK(esk_net_value(esk_engine_net(&s->eng, "OUT")) == ESK_L1);
    /* the external net and the internal port net are one node: OUT is also reachable as X1.I2 pin Y's net */
    CHECK(esk_comp_pin(esk_engine_comp(&s->eng, "X1.I2"), "Y")->net == esk_engine_net(&s->eng, "OUT"));
    es_session_destroy(s);
    free(proj);
}

/* one request; the parsed response, or NULL */
static esk_json *call(es_session *s, const char *method, const char *params)
{
    char req[4096], *res = NULL;
    size_t len = 0;
    int line;
    const char *err;
    esk_json *j = NULL;
    snprintf(req, sizeof req, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":%s}", method, params);
    if (es_session_rpc(s, req, strlen(req), &res, &len) == ES_OK && res)
        j = esk_json_parse(s->a, res, len, &line, &err);
    es_free(res);
    return j;
}

static int64_t err_code(const esk_json *j) { return esk_json_get_int(esk_json_get(j, "error"), "code", 0); }

static size_t net_pins(es_session *s, const char *net)
{
    return esk_json_len(esk_json_get(esk_json_get(s->project, "nets"), net));
}

static void test_edit_transactions(void)
{
    es_session *s = NULL;
    es_session_config_t cfg;
    char *proj = subst(PROJECT);
    esk_json *j;
    char params[2048];
    int64_t tx;
    memset(&cfg, 0, sizeof cfg);
    CHECK(es_session_create(&cfg, &s) == ES_OK);
    CHECK(es_session_load_json(s, proj, strlen(proj), lib_dir) == ES_OK);
    j = call(s, "control.advance", "{\"by\":\"2ms\"}");
    esk_json_free(s->a, j);

    /* no transaction open */
    j = call(s, "project.transaction", "{\"op\":\"edit\",\"tx\":9,\"edits\":[]}");
    CHECK(err_code(j) == -32602);
    esk_json_free(s->a, j);

    /* moving a part keeps the run */
    j = call(s, "project.transaction", "{\"op\":\"begin\"}");
    tx = atoi(esk_json_get_str(esk_json_get(j, "result"), "tx", "0"));
    CHECK(tx > 0);
    esk_json_free(s->a, j);
    j = call(s, "project.transaction", "{\"op\":\"begin\"}");
    CHECK(err_code(j) == -32000 + ES_ESTATE); /* one at a time */
    esk_json_free(s->a, j);
    snprintf(params, sizeof params, "{\"op\":\"edit\",\"tx\":\"%d\",\"edits\":[{\"set_layout\":{\"id\":\"D1\",\"layout\":{\"x\":120,\"y\":40}}},"
             "{\"set_layout\":{\"wires\":[{\"net\":\"SIG\",\"points\":[[0,0],[10,0]]}],"
             "\"buses\":[{\"name\":\"B\",\"nets\":[\"SIG\",\"VCC\"],\"points\":[[0,20],[40,20]]}]}}]}", (int)tx);
    j = call(s, "project.transaction", params);
    CHECK(j && esk_json_get(j, "error") == NULL);
    esk_json_free(s->a, j);
    snprintf(params, sizeof params, "{\"op\":\"commit\",\"tx\":\"%d\"}", (int)tx);
    j = call(s, "project.transaction", params);
    CHECK(esk_json_get_bool(esk_json_get(j, "result"), "applied", 0) == 1);
    CHECK(esk_json_get_bool(esk_json_get(j, "result"), "restarted", 1) == 0);
    esk_json_free(s->a, j);
    CHECK(s->eng.sched.now == (es_time_t)2000000000);
    {
        const esk_json *comps = esk_json_get(s->project, "components");
        size_t i;
        for (i = 0; i < esk_json_len(comps); i++)
            if (esk_str_eq(esk_json_get_str(esk_json_at(comps, i), "id", ""), "D1"))
                CHECK(esk_json_get_int(esk_json_get(esk_json_at(comps, i), "layout"), "x", 0) == 120);
        CHECK(esk_json_len(esk_json_get(esk_json_get(s->project, "layout"), "wires")) == 1);
    }

    /* adding and wiring a part rebuilds the circuit */
    j = call(s, "project.transaction", "{\"op\":\"begin\"}");
    tx = atoi(esk_json_get_str(esk_json_get(j, "result"), "tx", "0"));
    esk_json_free(s->a, j);
    snprintf(params, sizeof params, "{\"op\":\"edit\",\"tx\":\"%d\",\"edits\":[{\"add_component\":{\"id\":\"D2\",\"component\":\"t/led\"}},"
             "{\"connect\":{\"net\":\"SIG\",\"pin\":\"D2.A\"}},{\"rename_net\":{\"from\":\"SIG\",\"to\":\"BUTTON\"}},"
             "{\"set_param\":{\"id\":\"D1\",\"param\":\"color\",\"value\":\"red\"}}]}", (int)tx);
    j = call(s, "project.transaction", params);
    CHECK(j && esk_json_get(j, "error") == NULL);
    esk_json_free(s->a, j);
    snprintf(params, sizeof params, "{\"op\":\"commit\",\"tx\":\"%d\"}", (int)tx);
    j = call(s, "project.transaction", params);
    CHECK(esk_json_get_bool(esk_json_get(j, "result"), "restarted", 0) == 1);
    esk_json_free(s->a, j);
    CHECK(s->eng.sched.now == 0);
    CHECK(net_pins(s, "BUTTON") == 4);
    CHECK(net_pins(s, "SIG") == 0);
    CHECK_STR(esk_json_get_str(esk_json_at(esk_json_get(s->project, "probes"), 0), "target", ""), "net:BUTTON");
    CHECK_STR(esk_json_get_str(esk_json_at(esk_json_get(esk_json_get(s->project, "layout"), "wires"), 0), "net", ""), "BUTTON");
    CHECK_STR(esk_json_str(esk_json_at(esk_json_get(esk_json_at(esk_json_get(esk_json_get(s->project, "layout"), "buses"), 0), "nets"), 0)), "BUTTON");
    j = call(s, "input.press", "{\"target\":\"SW1\"}");
    esk_json_free(s->a, j);
    j = call(s, "control.advance", "{\"by\":\"1ms\"}");
    esk_json_free(s->a, j);
    j = call(s, "observe.snapshot", "{\"include\":[\"nets\"]}");
    CHECK_STR(esk_json_get_str(esk_json_get(esk_json_get(esk_json_get(j, "result"), "nets"), "BUTTON"), "logic", ""), "1");
    esk_json_free(s->a, j);

    /* a part the catalog does not have: the commit fails, nothing changes */
    j = call(s, "project.transaction", "{\"op\":\"begin\"}");
    tx = atoi(esk_json_get_str(esk_json_get(j, "result"), "tx", "0"));
    esk_json_free(s->a, j);
    snprintf(params, sizeof params, "{\"op\":\"edit\",\"tx\":\"%d\",\"edits\":[{\"add_component\":{\"id\":\"X9\",\"component\":\"t/nothing\"}}]}", (int)tx);
    esk_json_free(s->a, call(s, "project.transaction", params));
    snprintf(params, sizeof params, "{\"op\":\"commit\",\"tx\":\"%d\"}", (int)tx);
    j = call(s, "project.transaction", params);
    CHECK(err_code(j) == -32000 + ES_ESCHEMA);
    esk_json_free(s->a, j);
    CHECK(esk_json_len(esk_json_get(s->project, "components")) == 6);

    /* removing a part drops its pins; an aborted edit changes nothing */
    j = call(s, "project.transaction", "{\"op\":\"begin\"}");
    tx = atoi(esk_json_get_str(esk_json_get(j, "result"), "tx", "0"));
    esk_json_free(s->a, j);
    snprintf(params, sizeof params, "{\"op\":\"edit\",\"tx\":\"%d\",\"edits\":[{\"remove_component\":{\"id\":\"D2\"}},{\"disconnect\":{\"pin\":\"G1.-\"}}]}", (int)tx);
    esk_json_free(s->a, call(s, "project.transaction", params));
    snprintf(params, sizeof params, "{\"op\":\"abort\",\"tx\":\"%d\"}", (int)tx);
    esk_json_free(s->a, call(s, "project.transaction", params));
    CHECK(net_pins(s, "BUTTON") == 4);
    j = call(s, "project.transaction", "{\"op\":\"begin\"}");
    tx = atoi(esk_json_get_str(esk_json_get(j, "result"), "tx", "0"));
    esk_json_free(s->a, j);
    snprintf(params, sizeof params, "{\"op\":\"edit\",\"tx\":\"%d\",\"edits\":[{\"remove_component\":{\"id\":\"D2\"}},{\"disconnect\":{\"pin\":\"G1.-\"}}]}", (int)tx);
    esk_json_free(s->a, call(s, "project.transaction", params));
    snprintf(params, sizeof params, "{\"op\":\"commit\",\"tx\":\"%d\"}", (int)tx);
    esk_json_free(s->a, call(s, "project.transaction", params));
    CHECK(net_pins(s, "BUTTON") == 3);
    CHECK(net_pins(s, "GND") == 1);

    /* save writes the project as it now is */
    {
        esk_alloc a;
        char *dir, *path, *text = NULL;
        size_t len = 0;
        esk_alloc_init(&a, NULL);
        dir = esk_host_temp_dir(&a);
        path = esk_host_path_join(&a, dir ? dir : ".", "embsim-test-saved.json");
        snprintf(params, sizeof params, "{\"path\":\"");
        {
            size_t n = strlen(params);
            const char *c;
            for (c = path; c && *c && n + 3 < sizeof params; c++) {
                if (*c == '\\' || *c == '"')
                    params[n++] = '\\';
                params[n++] = *c;
            }
            params[n] = 0;
        }
        strcat(params, "\"}");
        j = call(s, "project.save", params);
        CHECK(j && esk_json_get(j, "error") == NULL);
        esk_json_free(s->a, j);
        CHECK(esk_host_read_file(&a, path, &text, &len) == ES_OK);
        if (text) {
            int line;
            const char *err;
            esk_json *saved = esk_json_parse(&a, text, len, &line, &err);
            CHECK(saved != NULL);
            CHECK(net_pins(s, "BUTTON") == esk_json_len(esk_json_get(esk_json_get(saved, "nets"), "BUTTON")));
            esk_json_free(&a, saved);
            esk_free(&a, text, len + 1);
        }
        esk_strfree(&a, path);
        esk_strfree(&a, dir);
    }
    es_session_destroy(s);
    free(proj);
}


static int organization_edit(esk_alloc *a, esk_json *project, const char *text, int *structural)
{
    const char *parse_error;
    int line, result;
    char why[256];
    esk_json *edit = esk_json_parse(a, text, strlen(text), &line, &parse_error);
    CHECK(edit != NULL);
    result = esk_project_apply_edit(a, project, edit, structural, why, sizeof why);
    esk_json_free(a, edit);
    return result;
}

static void test_layout_organization(void)
{
    esk_alloc a;
    const char *parse_error;
    int line, structural = 0;
    char why[256];
    const char *text = "{\"components\":[{\"id\":\"A\",\"component\":\"t/led\"},{\"id\":\"B\",\"component\":\"t/led\"}],\"boards\":[{\"id\":\"MCU\",\"board\":\"t/board\"}],\"nets\":{\"N\":[\"A.A\",\"B.A\"]}}";
    esk_json *project, *layout;
    esk_alloc_init(&a, NULL);
    project = esk_json_parse(&a, text, strlen(text), &line, &parse_error);
    CHECK(project != NULL);
    CHECK(organization_edit(&a, project, "{\"set_layout\":{\"groups\":[{\"id\":\"input\",\"name\":\"Input\",\"members\":[\"A\",\"MCU\"]}]}}", &structural) == 0);
    CHECK(structural == 0);
    CHECK(esk_project_validate_organization(project, why, sizeof why) == 0);
    CHECK(organization_edit(&a, project, "{\"set_layout\":{\"id\":\"A\",\"layout\":{\"x\":\"oops\",\"y\":0}}}", &structural) != 0);
    CHECK(organization_edit(&a, project, "{\"set_layout\":{\"id\":\"A\",\"layout\":{\"x\":0,\"y\":0,\"locked\":\"true\"}}}", &structural) != 0);
    CHECK(organization_edit(&a, project, "{\"set_layout\":{\"id\":\"A\",\"layout\":{\"x\":0,\"y\":0,\"rotation\":45}}}", &structural) != 0);
    CHECK(organization_edit(&a, project, "{\"set_layout\":{\"id\":\"A\",\"layout\":{\"x\":0,\"y\":0,\"mirror\":1}}}", &structural) != 0);

    CHECK(organization_edit(&a, project, "{\"set_layout\":{\"groups\":[{\"id\":\"bad\",\"name\":\"Bad\",\"members\":[\"missing\"]}]}}", &structural) != 0);
    CHECK(organization_edit(&a, project, "{\"set_layout\":{\"groups\":[{\"id\":\"one\",\"name\":\"One\",\"members\":[\"A\",\"A\"]}]}}", &structural) != 0);
    CHECK(organization_edit(&a, project, "{\"set_layout\":{\"groups\":[{\"id\":\"one\",\"name\":\"One\",\"members\":[\"A\"]},{\"id\":\"two\",\"name\":\"Two\",\"members\":[\"A\"]}]}}", &structural) != 0);
    CHECK(organization_edit(&a, project, "{\"set_layout\":{\"id\":\"A\",\"layout\":{\"x\":40,\"y\":50,\"locked\":true}}}", &structural) == 0);
    CHECK(organization_edit(&a, project, "{\"set_layout\":{\"id\":\"A\",\"layout\":{\"x\":80,\"y\":50}}}", &structural) != 0);
    CHECK(organization_edit(&a, project, "{\"set_layout\":{\"id\":\"A\",\"layout\":{\"x\":40,\"y\":50,\"rotation\":90}}}", &structural) != 0);
    CHECK(organization_edit(&a, project, "{\"set_layout\":{\"id\":\"A\",\"layout\":{\"x\":40,\"y\":50}}}", &structural) == 0);
    layout = esk_json_get_mut(esk_json_at_mut(esk_json_get_mut(project, "components"), 0), "layout");
    CHECK(esk_json_get_bool(layout, "locked", 0) == 1);
    CHECK(organization_edit(&a, project, "{\"set_layout\":{\"id\":\"A\",\"layout\":{\"x\":80,\"y\":50,\"locked\":false}}}", &structural) == 0);
    CHECK(structural == 0);
    CHECK(organization_edit(&a, project, "{\"remove_component\":{\"id\":\"A\"}}", &structural) == 0);
    CHECK(esk_json_len(esk_json_get(esk_json_at(esk_json_get(esk_json_get(project, "layout"), "groups"), 0), "members")) == 1);
    CHECK(organization_edit(&a, project, "{\"remove_board\":{\"id\":\"MCU\"}}", &structural) == 0);
    CHECK(esk_json_len(esk_json_get(esk_json_get(project, "layout"), "groups")) == 0);
    CHECK(esk_json_len(esk_json_get(esk_json_get(project, "nets"), "N")) == 1);
    esk_json_free(&a, project);
}

static void test_bad_project(void)
{
    es_session *s = NULL;
    es_session_config_t cfg;
    const char *bad = "{\"schema_version\":1,\"kind\":\"project\",\"name\":\"x\",\"components\":[{\"id\":\"A\",\"component\":\"t/nothing\"}],\"nets\":{}}";
    memset(&cfg, 0, sizeof cfg);
    CHECK(es_session_create(&cfg, &s) == ES_OK);
    CHECK(es_session_load_json(s, bad, strlen(bad), lib_dir) != ES_OK);
    CHECK(esk_diag_count(&s->diag, ES_SEV_ERROR) >= 1);
    es_session_destroy(s);
}

/* Callback reservation failure must not destroy the old capture or callback. */
static void test_probe_move_limit(void)
{
    es_session *s = NULL;
    es_session_config_t cfg;
    esk_probe *p = NULL;
    es_sample_t *ring;
    esk_event_handle event;
    uint32_t epoch;
    size_t max_events, count, samples;
    char *proj = subst(PROJECT);
    memset(&cfg, 0, sizeof cfg);
    CHECK(es_session_create(&cfg, &s) == ES_OK);
    CHECK(es_session_load_json(s, proj, strlen(proj), lib_dir) == ES_OK);
    CHECK(esk_session_probe_add(s, "sample", "net:SIG", "analog", 100000, &p) == ES_OK);
    if (p) {
        event = p->sample_event;
        ring = p->ring;
        epoch = p->capture_epoch;
        samples = p->len;
        count = s->probes.len;
        max_events = s->eng.sched.max;
        s->eng.sched.max = s->eng.sched.len;
        CHECK(esk_session_probe_move(s, p, "net:VCC") == ES_ELIMIT);
        CHECK_STR(p->target, "net:SIG");
        CHECK(p->sample_event == event && p->ring == ring);
        CHECK(p->capture_epoch == epoch && p->len == samples && s->probes.len == count);
        s->eng.sched.max = max_events;
        CHECK(esk_session_probe_move(s, p, "net:VCC") == ES_OK);
        CHECK_STR(p->target, "net:VCC");
        CHECK(p->capture_epoch == epoch + 1 && p->sample_event != 0);
    }
    es_session_destroy(s);
    free(proj);
}

/* the host port's bridge at another baud rate than the sketch's: what a
 * UART reads from frames sent at the other rate (hostport.c) */
static void test_uart_rebaud(void)
{
    unsigned char out[16];
    const unsigned char hello[] = "Hello", u = 0x55, a = 'A';
    CHECK(esk_uart_rebaud(hello, 5, 9600, 9600, out, sizeof out) == 5 && !memcmp(out, hello, 5));
    /* 'U' at 19200 read at 9600: each sample lands on bit 2, 4, 6, then the stop bit and idle */
    CHECK(esk_uart_rebaud(&u, 1, 19200, 9600, out, sizeof out) == 1 && out[0] == 0xff);
    /* 'A' at 9600 read at 19200: two bytes, the second from the falling edge of bit 7 */
    CHECK(esk_uart_rebaud(&a, 1, 9600, 19200, out, sizeof out) == 2 && out[0] == 0x06 && out[1] == 0xfe);
}

TEST_MAIN_BEGIN
{
    esk_alloc a;
    char *tmp;
    esk_alloc_init(&a, NULL);
    tmp = esk_host_temp_dir(&a);
    CHECK(tmp != NULL);
    lib_dir = esk_host_path_join(&a, tmp ? tmp : ".", "embsim-test-lib");
    esk_strfree(&a, tmp);
}
setup_library();
RUN(test_load_and_run);
RUN(test_scenario_and_rpc);
RUN(test_determinism);
RUN(test_probe_move_limit);
RUN(test_composite);
RUN(test_bad_project);
RUN(test_edit_transactions);
RUN(test_layout_organization);
RUN(test_uart_rebaud);
TEST_MAIN_END
