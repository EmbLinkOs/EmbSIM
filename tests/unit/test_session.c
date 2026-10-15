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
    esk_alloc_init(&a, NULL);
    esk_strbuf_init(&b, &a);
    esk_strbuf_append(&b, tmpl, (size_t)(p - tmpl));
    esk_strbuf_puts(&b, lib_dir);
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
                if (i == 5 || i == 6)
                    CHECK(esk_json_get_int(esk_json_get(j, "error"), "code", 0) == -32601);
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
RUN(test_bad_project);
TEST_MAIN_END
