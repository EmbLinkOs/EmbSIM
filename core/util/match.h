/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* match.h -- a small regular-expression matcher for scenario text
 * expectations (SPEC-007), portable C99 where POSIX <regex.h> is not
 * (MSVC). It knows: literal characters, '.', classes [a-z0-9.] with
 * ranges and a leading '^' for negation, the escapes \d \s \w (and their
 * capitals) and \ before any other character, the quantifiers * + ? (greedy,
 * backtracking), the anchors ^ and $, and one capture group ( ). It has no
 * alternation, counted repeats or nested groups: a pattern using them is
 * refused, never matched as something else. */
#ifndef ESK_MATCH_H
#define ESK_MATCH_H

#include <stddef.h>

typedef struct esk_match {
    size_t start, end;         /* the whole match in the text */
    size_t group_start, group_end; /* the group's text, when the pattern has one */
    int has_group;
} esk_match;

/* 1 and `m` filled when `pattern` matches in text[0..len), at the last place
 * it does (the latest print of a value); 0 when it does not; -1 when the
 * pattern is not one this matcher knows. */
int esk_match_last(const char *pattern, const char *text, size_t len, esk_match *m);

#endif
