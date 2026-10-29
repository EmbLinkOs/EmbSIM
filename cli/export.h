/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* export.h -- `embsim export svg` (export.c). */
#ifndef EMBSIM_CLI_EXPORT_H
#define EMBSIM_CLI_EXPORT_H

#include "embsim/session.h"

/* the loaded project's schematic sheet to `path` ("-": stdout), in the
 * light theme or the dark: 1 when written */
int embsim_export_sheet(es_session *s, const char *path, int dark);
/* what display part `id` shows now, `scale` screen pixels a pixel */
int embsim_export_display(es_session *s, const char *id, const char *path, int scale);

#endif
