/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* lua_model.h -- scripted component models (SPEC-013): one Lua state per
 * engine, one sandboxed environment per instance, the ctx API over the
 * model API of SPEC-003 §5 and an instruction budget per evaluation. */
#ifndef ESK_LUA_MODEL_H
#define ESK_LUA_MODEL_H

#include "engine/engine.h"

/* the default instruction budget of one evaluation (SPEC-013 §1) */
#define ESK_LUA_BUDGET 1000000

/* whether this build has the Lua tier (EMBSIM_LUA) */
int esk_lua_available(void);
/* Registers the script of component description `kind` with the engine's
 * Lua host, creating the host at the first call. The source is compiled to
 * check it; each instance runs its own copy. A second registration of the
 * same kind is a no-op. On failure `why` holds the compiler's message. */
es_status esk_lua_register(esk_engine *e, const char *kind, const char *source, size_t len, const char *chunkname,
                           char *why, size_t cap);
/* the model ops of every scripted component */
const esk_model_ops *esk_lua_ops(void);

#endif
