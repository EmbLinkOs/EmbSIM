/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Named component integration: temperature input drives a real comparator
 * package and transistor, supply loss clamps the sensor, reset retains
 * the physical ambient temperature. Invalid analog device port counts are rejected. */
#include "harness.h"
#include "analog/named.h"
#include "session/session_priv.h"

static void test_analog_shapes(void)
{
    esk_adev d;
    void *state;
    memset(&d, 0, sizeof d);
    d.nnodes = 2;
    CHECK_EQ(esk_temperature_ops.setup(&d), ES_EINVAL);
    d.nnodes = 3;
    CHECK_EQ(esk_temperature_ops.setup(&d), ES_OK);
    CHECK_EQ(d.nbranch, 1);
    d.nnodes = 4;
    CHECK_EQ(esk_comparator_ops.setup(&d), ES_EINVAL);
    d.nnodes = 5;
    CHECK_EQ(esk_comparator_ops.setup(&d), ES_OK);
    state = calloc(1, esk_timer555_ops.state_size);
    CHECK(state != NULL);
    if (state) {
        d.state = state;
        d.nnodes = 7;
        CHECK_EQ(esk_timer555_ops.setup(&d), ES_EINVAL);
        d.nnodes = 8;
        CHECK_EQ(esk_timer555_ops.setup(&d), ES_OK);
        free(state);
    }
}

static void test_temperature_alarm(void)
{
    es_session *s = NULL;
    char path[1024];
    esk_json value;
    esk_net *temp, *alarm, *base;
    CHECK_EQ(es_session_create(NULL, &s), ES_OK);
    if (!s) return;
    snprintf(path, sizeof path, "%s/examples/temperature-alarm/project.json", EMBSIM_SOURCE_DIR);
    CHECK_EQ(es_session_load_file(s, path), ES_OK);
    CHECK_EQ(es_session_advance(s, 50 * ES_PS_PER_US), ES_OK);
    CHECK_STR(esk_engine_comp(&s->eng, "S1")->kind, "emblink/lm35dz");
    temp = esk_engine_net(&s->eng, "TEMP");
    alarm = esk_engine_net(&s->eng, "ALARM");
    base = esk_engine_net(&s->eng, "BASE");
    CHECK(temp != NULL && alarm != NULL && base != NULL);
    if (temp && alarm && base) {
        CHECK_NEAR(temp->voltage, 0.25, 0.002);
        CHECK(alarm->voltage < 0.1);
        memset(&value, 0, sizeof value);
        value.type = ESK_JSON_NUMBER;
        value.u.number.value = 80.0;
        CHECK_EQ(esk_session_apply_input(s, "S1", "temperature", &value, s->eng.sched.now, NULL, NULL), ES_OK);
        CHECK_EQ(es_session_advance(s, 100 * ES_PS_PER_US), ES_OK);
        CHECK_NEAR(temp->voltage, 0.8, 0.002);
        CHECK_NEAR(alarm->voltage, (5.0 + base->voltage) * 0.5, 0.01);
        CHECK_EQ(es_session_reset(s), ES_OK);
        CHECK_EQ(es_session_advance(s, 50 * ES_PS_PER_US), ES_OK);
        CHECK_NEAR(temp->voltage, 0.8, 0.002);
        CHECK_NEAR(alarm->voltage, (5.0 + base->voltage) * 0.5, 0.01);
    }
    es_session_destroy(s);
}

TEST_MAIN_BEGIN
RUN(test_analog_shapes);
RUN(test_temperature_alarm);
TEST_MAIN_END
