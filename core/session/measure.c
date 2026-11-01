/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* measure.c -- the firmware's coverage and profile in the session (SPEC-008
 * §5 analysis, SPEC-012 §4): which boards' MCUs are watched, the analyses kept
 * across a project's reload, and their reports for the CLI, the protocol and
 * a run's report. */
#include <string.h>

#include "host/host.h"
#include "mcu/analysis.h"
#include "mcu/mcu.h"
#include "session/session_priv.h"

static int wanted(const es_session *s, const esk_board_inst *bi)
{
    return bi->mcu && esk_mcu_machine(bi->mcu) && (!s->an_board || esk_str_eq(s->an_board, bi->id));
}

es_status esk_session_analyze(es_session *s, const char *board, unsigned what, char *why, size_t cap)
{
    size_t i;
    int found = 0;
    es_status r = ES_OK;
    if (board) {
        for (i = 0; i < s->boards.len; i++)
            found |= esk_str_eq(s->boards.data[i]->id, board);
        if (!found) {
            esk_snprintf(why, cap, "the project has no board %s", board);
            return ES_ENOTFOUND;
        }
    }
    esk_strfree(s->a, s->an_board);
    s->an_board = board ? esk_strdup(s->a, board) : NULL;
    s->an_what = what;
    found = 0;
    for (i = 0; i < s->boards.len; i++) {
        esk_board_inst *bi = s->boards.data[i];
        size_t len = 0;
        if (!bi->mcu || !esk_mcu_machine(bi->mcu))
            continue;
        if (!wanted(s, bi)) {
            (void)esk_mcu_analyze(bi->mcu, 0, why, cap);
            continue;
        }
        if (what && !esk_mcu_image(bi->mcu, &len))
            continue; /* a board without firmware has nothing to measure */
        r = esk_mcu_analyze(bi->mcu, what, why, cap);
        if (r != ES_OK)
            return r;
        if (what & ESK_AN_MEASURE) {
            int k;
            for (k = 0; k < s->an_nmeasure; k++) {
                r = esk_an_measure_add(esk_mcu_analysis(bi->mcu), s->an_measure[k], why, cap);
                if (r != ES_OK)
                    return r;
            }
            if (s->an_trace) {
                r = esk_an_trace(esk_mcu_analysis(bi->mcu), s->an_trace_start, s->an_trace_limit, why, cap);
                if (r != ES_OK)
                    return r;
            }
        }
        found = 1;
    }
    if (what && !found) {
        esk_snprintf(why, cap, board ? "board %s runs no firmware" : "no board of the project runs firmware", board);
        return ES_ENOTFOUND;
    }
    return ES_OK;
}

es_status esk_session_measure(es_session *s, const char *spec)
{
    if (s->an_nmeasure == (int)(sizeof s->an_measure / sizeof s->an_measure[0]))
        return ES_EINVAL;
    s->an_measure[s->an_nmeasure] = esk_strdup(s->a, spec);
    if (!s->an_measure[s->an_nmeasure])
        return ES_ENOMEM;
    s->an_nmeasure++;
    return ES_OK;
}

es_status esk_session_trace(es_session *s, const char *start, uint64_t limit)
{
    esk_strfree(s->a, s->an_trace_start);
    s->an_trace_start = start ? esk_strdup(s->a, start) : NULL;
    if (start && !s->an_trace_start)
        return ES_ENOMEM;
    s->an_trace = 1;
    s->an_trace_limit = limit;
    return ES_OK;
}

es_status esk_session_trace_write(es_session *s, const char *board, int vcd, esk_strbuf *out, char *why, size_t cap)
{
    size_t i;
    for (i = 0; i < s->boards.len; i++) {
        esk_board_inst *bi = s->boards.data[i];
        esk_an *a = bi->mcu ? esk_mcu_analysis(bi->mcu) : NULL;
        if (!a || (board && !esk_str_eq(board, bi->id)) || !(esk_an_what(a) & ESK_AN_MEASURE))
            continue;
        return esk_an_trace_write(a, vcd, out) ? ES_OK : ES_ENOMEM;
    }
    esk_snprintf(why, cap, "the trace is not on%s%s", board ? " for board " : "", board ? board : "");
    return ES_ESTATE;
}

void esk_session_analyses_apply(es_session *s)
{
    char why[160];
    if (s->an_what && esk_session_analyze(s, s->an_board, s->an_what, why, sizeof why) != ES_OK)
        esk_diag_raise(&s->diag, ES_SEV_WARNING, "analysis.unavailable", s->eng.sched.now, "board",
                       s->an_board, NULL, 0, "%s", why);
}

/* the directory of a board's firmware image, for the sources beside it */
static char *image_dir(es_session *s, const esk_board_inst *bi)
{
    const char *img = bi->firmware ? esk_json_get_str(bi->firmware, "image", NULL) : NULL;
    char *full, *dir;
    if (!img)
        return esk_strdup(s->a, s->base_dir ? s->base_dir : ".");
    full = esk_host_path_join(s->a, s->base_dir ? s->base_dir : ".", img);
    dir = full ? esk_host_dirname(s->a, full) : NULL;
    esk_strfree(s->a, full);
    return dir;
}

const char *esk_session_analysis_name(unsigned which)
{
    return which == ESK_AN_COVERAGE ? "coverage"
         : which == ESK_AN_MEASURE  ? "the measurements"
         : which == ESK_AN_STACK    ? "the stack analysis"
         : which == ESK_AN_FAULT    ? "the fault report"
                                    : "the profile";
}

es_status esk_session_analysis_text(es_session *s, const char *board, unsigned which, int fmt, esk_strbuf *out,
                                    char *why, size_t cap)
{
    size_t i;
    int n = 0, many = 0;
    for (i = 0; i < s->boards.len; i++)
        many += s->boards.data[i]->mcu && esk_mcu_analysis(s->boards.data[i]->mcu) != NULL;
    for (i = 0; i < s->boards.len; i++) {
        esk_board_inst *bi = s->boards.data[i];
        esk_an *a = bi->mcu ? esk_mcu_analysis(bi->mcu) : NULL;
        int ok;
        if (!a || (board && !esk_str_eq(board, bi->id)) ||
            !(esk_an_what(a) & which))
            continue;
        if (which == ESK_AN_MEASURE) {
            ok = esk_an_measure_text(a, bi->id, out);
        } else if (which == ESK_AN_STACK) {
            ok = esk_an_stack(a, bi->id, out);
        } else if (which == ESK_AN_FAULT) {
            ok = esk_an_faults(a, bi->id, out);
        } else if (which == ESK_AN_COVERAGE) {
            char *dir = image_dir(s, bi);
            ok = esk_an_coverage(a, fmt, bi->id, dir, out);
            esk_strfree(s->a, dir);
        } else {
            /* collapsed stacks of several boards: each board's root first */
            ok = esk_an_profile(a, fmt, many > 1 || fmt == ESK_AN_TEXT ? bi->id : NULL, out);
        }
        if (!ok)
            return ES_ENOMEM;
        n++;
    }
    if (!n) {
        esk_snprintf(why, cap, "%s is not on%s%s", esk_session_analysis_name(which), board ? " for board " : "",
                     board ? board : "");
        return ES_ESTATE;
    }
    return ES_OK;
}

esk_json *esk_session_analysis_json(es_session *s, const char *board, unsigned which, int detail)
{
    esk_json *out = esk_json_new_object(s->a);
    size_t i;
    for (i = 0; out && i < s->boards.len; i++) {
        esk_board_inst *bi = s->boards.data[i];
        esk_an *a = bi->mcu ? esk_mcu_analysis(bi->mcu) : NULL;
        esk_json *j;
        if (!a || (board && !esk_str_eq(board, bi->id)))
            continue;
        j = which == ESK_AN_MEASURE  ? esk_an_measure_json(a, s->a)
          : which == ESK_AN_COVERAGE ? esk_an_coverage_json(a, s->a, detail)
          : which == ESK_AN_STACK    ? esk_an_stack_json(a, s->a, detail ? 0 : 10)
          : which == ESK_AN_FAULT    ? esk_an_faults_json(a, s->a)
                                     : esk_an_profile_json(a, s->a, detail ? 0 : 10);
        if (j)
            esk_json_object_set(s->a, out, bi->id, j);
    }
    return out;
}

void esk_session_analysis_report(es_session *s, esk_json *report)
{
    if (!s->an_what)
        return;
    if (s->an_what & ESK_AN_COVERAGE)
        esk_json_object_set(s->a, report, "coverage", esk_session_analysis_json(s, NULL, ESK_AN_COVERAGE, 0));
    if (s->an_what & ESK_AN_PROFILE)
        esk_json_object_set(s->a, report, "profile", esk_session_analysis_json(s, NULL, ESK_AN_PROFILE, 0));
    if (s->an_what & ESK_AN_STACK)
        esk_json_object_set(s->a, report, "stack", esk_session_analysis_json(s, NULL, ESK_AN_STACK, 0));
    if (s->an_what & ESK_AN_FAULT)
        esk_json_object_set(s->a, report, "faults", esk_session_analysis_json(s, NULL, ESK_AN_FAULT, 0));
}
