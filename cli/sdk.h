/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* sdk.h -- `embsim catalog new` and `embsim catalog install` (SPEC-014 §4-5). */
#ifndef EMBSIM_CLI_SDK_H
#define EMBSIM_CLI_SDK_H

#include "embsim/session.h"

/* runs `sub` ("new" or "install") with the arguments after it; `deps`
 * resolves the dependencies of a library being installed. -1 when `sub` is
 * neither, else the exit code. */
int embsim_catalog_sdk(const char *sub, int argc, char **argv, es_catalog *deps);

#endif
