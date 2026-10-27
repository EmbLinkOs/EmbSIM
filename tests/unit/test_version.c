/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "harness.h"
#include "host/host.h"
#include "util/version.h"

static void test_vectors(void)
{
    esk_alloc a;
    char *text = NULL;
    size_t len = 0, i;
    esk_json *vectors;
    esk_alloc_init(&a, NULL);
    CHECK_EQ(esk_host_read_file(&a, EMBSIM_SOURCE_DIR "/tests/fixtures/version-ranges.json", &text, &len), ES_OK);
    if (!text) return;
    vectors = esk_json_parse(&a, text, len, NULL, NULL);
    CHECK(vectors != NULL);
    CHECK_STR(esk_json_get_str(esk_json_at(vectors, 0), "version", ""), "1.2.3");
    for (i = 0; i < esk_json_len(vectors); i++) {
        const esk_json *v = esk_json_at(vectors, i);
        int actual = esk_version_satisfies(esk_json_get_str(v, "version", ""), esk_json_get_str(v, "range", ""));
        if (actual != (int)esk_json_get_int(v, "expected", -2))
            fprintf(stderr, "version vector %u: %s against %s\n", (unsigned)i, esk_json_get_str(v, "version", ""), esk_json_get_str(v, "range", ""));
        CHECK_EQ(actual, esk_json_get_int(v, "expected", -2));
    }
    esk_json_free(&a, vectors);
    esk_free(&a, text, len + 1);
}
TEST_MAIN_BEGIN
    RUN(test_vectors);
    CHECK_EQ(esk_version_satisfies(NULL, "1"), -1);
    CHECK_EQ(esk_version_satisfies("1.0.0", NULL), -1);
TEST_MAIN_END
