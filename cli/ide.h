/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ide.h -- `embsim ide` (ide.c). */
#ifndef EMBSIM_CLI_IDE_H
#define EMBSIM_CLI_IDE_H

#include "embsim/session.h"

/* VS Code's launch configurations and their tasks for the loaded project's
 * boards, in .vscode next to it; `project_file` as the tasks name it,
 * `embsim` the command they run. 0 when written, 2 on an error */
int embsim_ide(es_session *s, const char *project_file, const char *embsim, int force);

#endif
