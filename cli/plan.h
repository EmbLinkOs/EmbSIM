/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* plan.h -- `embsim plan` (plan.c). */
#ifndef EMBSIM_CLI_PLAN_H
#define EMBSIM_CLI_PLAN_H

#include "embsim/session.h"

/* a pin plan on `board` for `needs` ("spi,i2c,pwm:2,uart,gpio:12"),
 * printed (or as JSON), with a C header of the pins when `header` names a
 * file: 0 when every need has pins, 1 when one has none, 2 on an error */
int embsim_plan(es_session *s, const char *board, const char *needs, const char *prefer, const char *header, int json);

#endif
