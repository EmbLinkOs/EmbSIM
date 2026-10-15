/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "util/num.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int esk_parse_si(const char *s, double *out)
{
    char *end;
    double v;
    double mult = 1.0;
    if (!s || !*s)
        return -1;
    v = strtod(s, &end);
    if (end == s)
        return -1;
    while (*end == ' ')
        end++;
    if (*end) {
        /* case-insensitive except M (mega) vs m (milli); "meg" is mega */
        if (!strncmp(end, "meg", 3) || !strncmp(end, "MEG", 3) || !strncmp(end, "Meg", 3)) {
            mult = 1e6;
            end += 3;
        } else {
            switch (*end) {
            case 'T': mult = 1e12; end++; break;
            case 'G': mult = 1e9; end++; break;
            case 'M': mult = 1e6; end++; break;
            case 'k': case 'K': mult = 1e3; end++; break;
            case 'm': mult = 1e-3; end++; break;
            case 'u': mult = 1e-6; end++; break;
            case 'n': mult = 1e-9; end++; break;
            case 'p': mult = 1e-12; end++; break;
            case 'f': mult = 1e-15; end++; break;
            default: return -1;
            }
        }
        /* an optional unit name follows (ohm, F, H, V, A, Hz) and is ignored */
        while (*end && (isalpha((unsigned char)*end) || *end == ' '))
            end++;
        if (*end)
            return -1;
    }
    *out = v * mult;
    return 0;
}

int esk_json_to_double(const esk_json *j, double *out)
{
    if (!j)
        return -1;
    if (j->type == ESK_JSON_NUMBER) {
        *out = j->u.number.value;
        return 0;
    }
    if (j->type == ESK_JSON_STRING)
        return esk_parse_si(j->u.string.data, out);
    return -1;
}

int esk_parse_time(const char *s, es_time_t *out)
{
    const char *p = s;
    uint64_t ip = 0;
    uint64_t frac = 0, frac_scale = 1;
    uint64_t unit;
    int digits = 0;
    if (!s || !*s)
        return -1;
    while (*p >= '0' && *p <= '9') {
        if (ip > (UINT64_MAX - 9) / 10)
            return -1;
        ip = ip * 10 + (uint64_t)(*p - '0');
        p++;
        digits++;
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            if (frac_scale > UINT64_MAX / 100)
                return -1;
            frac = frac * 10 + (uint64_t)(*p - '0');
            frac_scale *= 10;
            p++;
            digits++;
        }
    }
    if (!digits)
        return -1;
    while (*p == ' ')
        p++;
    if (!strcmp(p, "ps") || !*p) unit = 1;
    else if (!strcmp(p, "ns")) unit = ES_PS_PER_NS;
    else if (!strcmp(p, "us")) unit = ES_PS_PER_US;
    else if (!strcmp(p, "ms")) unit = ES_PS_PER_MS;
    else if (!strcmp(p, "s")) unit = ES_PS_PER_S;
    else return -1;
    if (ip > UINT64_MAX / unit)
        return -1;
    {
        uint64_t whole = ip * unit;
        uint64_t part;
        /* frac/frac_scale * unit must be an integer number of picoseconds */
        if ((frac * unit) % frac_scale != 0) {
            /* could still be exact if unit/frac_scale divides; check via gcd-free test above */
            return -1;
        }
        if (frac && unit > UINT64_MAX / frac)
            return -1;
        part = (frac * unit) / frac_scale;
        if (whole > UINT64_MAX - part)
            return -1;
        *out = whole + part;
    }
    return 0;
}

int esk_json_to_time(const esk_json *j, es_time_t *out)
{
    if (!j)
        return -1;
    if (j->type == ESK_JSON_STRING)
        return esk_parse_time(j->u.string.data, out);
    if (j->type == ESK_JSON_NUMBER && j->u.number.is_integer && j->u.number.integer >= 0) {
        *out = (es_time_t)j->u.number.integer;
        return 0;
    }
    return -1;
}

int esk_parse_u64(const char *s, uint64_t *out)
{
    char *end;
    unsigned long long v;
    if (!s || !*s)
        return -1;
    v = strtoull(s, &end, 0);
    if (end == s)
        return -1;
    if (*end == 'K' || *end == 'k') {
        if (v > UINT64_MAX >> 10) return -1;
        v <<= 10; end++;
    } else if (*end == 'M') {
        if (v > UINT64_MAX >> 20) return -1;
        v <<= 20; end++;
    } else if (*end == 'G') {
        if (v > UINT64_MAX >> 30) return -1;
        v <<= 30; end++;
    }
    if (*end)
        return -1;
    *out = (uint64_t)v;
    return 0;
}

int esk_json_to_u64(const esk_json *j, uint64_t *out)
{
    if (!j)
        return -1;
    if (j->type == ESK_JSON_NUMBER && j->u.number.is_integer && j->u.number.integer >= 0) {
        *out = (uint64_t)j->u.number.integer;
        return 0;
    }
    if (j->type == ESK_JSON_STRING)
        return esk_parse_u64(j->u.string.data, out);
    return -1;
}

size_t esk_format_time(char *buf, size_t cap, es_time_t ps)
{
    static const struct { const char *name; uint64_t div; } units[] = {
        { "s", ES_PS_PER_S }, { "ms", ES_PS_PER_MS }, { "us", ES_PS_PER_US }, { "ns", ES_PS_PER_NS }
    };
    size_t i;
    for (i = 0; i < ES_COUNTOF(units); i++) {
        if (ps >= units[i].div) {
            uint64_t whole = ps / units[i].div, rem = ps % units[i].div;
            if (rem == 0)
                return esk_snprintf(buf, cap, "%llu %s", (unsigned long long)whole, units[i].name);
            return esk_snprintf(buf, cap, "%llu.%03llu %s", (unsigned long long)whole,
                                (unsigned long long)(rem * 1000 / units[i].div), units[i].name);
        }
    }
    return esk_snprintf(buf, cap, "%llu ps", (unsigned long long)ps);
}

int esk_time_add(es_time_t a, es_time_t b, es_time_t *out)
{
    if (a > ES_TIME_MAX - b)
        return -1;
    *out = a + b;
    return 0;
}
