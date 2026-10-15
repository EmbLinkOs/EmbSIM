# SPEC-001 - Core API Conventions

**Status:** Accepted 2026-10-15 with the baseline. **Builds on:** 02 §4,
ADR-001, ADR-007. **Requirements:** `docs/requirements/API.md`.

---

## 1. Scope

The public C API of `libembsim` (`include/embsim/*.h`), its naming, error
handling, ownership, threading and versioning rules, and the rule that the
session protocol (SPEC-008) is the functional surface while the C functions
are its in-process transport plus a few primitives.

## 2. Naming

| Prefix | Use |
|---|---|
| `es_` | public functions, types, constants (`es_session_create`, `es_time_t`, `ES_OK`) |
| `esk_` | core-private functions and types; never in a public header |
| `ES_` | public macros and enumerators |
| `embsim.` / `embsim_` | protocol method families and file-name prefixes |

Types are `snake_case` with a `_t` suffix for value types; opaque handles are
`typedef struct es_x es_x;` without suffix. Functions are `es_<object>_<verb>`.

## 3. Status codes

Every fallible function returns `es_status` (an `int`): `ES_OK` (0) or a
negative code. Outputs are written only on `ES_OK`; a failed call changes no
observable state (strong exception safety at the API boundary).

| Code | Meaning |
|---|---|
| `ES_EINVAL` | invalid argument or identifier |
| `ES_ENOMEM` | allocation failed |
| `ES_ELIMIT` | a configured capacity was exceeded |
| `ES_ETIME` | time would move backwards or past the horizon |
| `ES_ENOTFOUND` | a named entity does not exist |
| `ES_EPARSE` | text could not be parsed (JSON, HEX, ELF) |
| `ES_ESCHEMA` | a description failed validation; details in the diagnostic |
| `ES_EUNSUPPORTED` | behaviour not implemented under the session policy |
| `ES_ECONVERGE` | the analog engine failed to converge |
| `ES_ESTATE` | the call is not valid in the current state (e.g. running) |
| `ES_EIO` | host I/O failed |
| `ES_EPROTO` | protocol message malformed or method unknown |
| `ES_EINTERNAL` | an invariant failed; always a bug |

`const char *es_strerror(es_status)` names a code. Details (file, node,
register, net names) are reported through the diagnostic channel (§7), never
through `errno`, never through process termination. No public function calls
`exit`, `abort` or writes to `stdout`/`stderr`.

## 4. Ownership and memory

- Handles are created by `es_<object>_create` and destroyed by
  `es_<object>_destroy`; destroying a session destroys everything it owns.
- Strings returned by the library are allocated by it and released with
  `es_free`; strings passed in are borrowed for the duration of the call
  unless the documentation says the object keeps a copy (it always copies).
- The library allocates through one hook table (`es_allocator_t`) that an
  embedder may replace at process start; defaults are `malloc`/`free`.
- Capacities (nets, components, events, probe memory) are configured per
  session; exceeding one returns `ES_ELIMIT` with a diagnostic naming the
  limit, never an allocation failure inside a model.

## 5. Threading

All calls on one session are serialized by the caller. The library creates
no threads except in the optional server subsystem (`es_server_*`), which
serializes protocol requests onto the session itself. Two sessions may be
used from two threads concurrently (ADR-016). No function blocks on I/O
except the explicitly blocking `es_server_serve` and the host layer.

## 6. The protocol is the API

`es_session_rpc(session, request, request_len, &response, &response_len)`
executes one JSON-RPC 2.0 request in process. Every protocol method of
SPEC-008 is available through it. The remaining C functions exist for what
JSON cannot do efficiently (binary sample buffers, callbacks) and for the
embedder's convenience:

```c
es_status es_session_create(const es_session_config_t *cfg, es_session **out);
void      es_session_destroy(es_session *s);
es_status es_session_load_file(es_session *s, const char *path);
es_status es_session_load_json(es_session *s, const char *json, size_t len, const char *base_dir);
es_status es_session_reset(es_session *s);
es_status es_session_advance(es_session *s, es_time_t until);
es_status es_session_step(es_session *s);                 /* one scheduler event */
es_time_t es_session_now(const es_session *s);
es_status es_session_rpc(es_session *s, const char *req, size_t len, char **res, size_t *res_len);
es_status es_session_set_diagnostic_handler(es_session *s, es_diagnostic_fn fn, void *ctx);
es_status es_session_set_notification_handler(es_session *s, es_notification_fn fn, void *ctx);
es_status es_session_probe_read(es_session *s, uint32_t probe, es_sample_t *buf, size_t cap, size_t *n);
es_status es_catalog_create(es_catalog **out);
void      es_catalog_destroy(es_catalog *c);
es_status es_catalog_add_library(es_catalog *c, const char *path);
es_status es_catalog_validate(es_catalog *c, const char *path, char **report_json);
es_version_t es_version(void);
int       es_abi_version(void);
const char *es_strerror(es_status st);
void      es_free(void *p);
```

The exact header is the contract; this list is its summary.

## 7. Diagnostics

A diagnostic is a structured record `{severity, code, message, time_ps,
subject: {kind, name}, location: {file, line, node}}` with severity
`info | warning | error | fatal`. The library delivers it to the session's
handler and the protocol's `diagnostics` notification; `fatal` also stops the
run and makes `es_session_advance` return the matching status. Codes are
stable identifiers documented in `docs/user/diagnostics.md` (e.g.
`net.contention`, `net.floating_input`, `sched.delta_overflow`,
`mcu.unsupported_access`, `analog.no_convergence`, `schema.unknown_field`).

## 8. Versioning

`es_version()` is the semantic version of the library; `es_abi_version()` is
an integer incremented on any incompatible change to a public header; the
protocol has its own version (SPEC-008 §2); description schemas have
`schema_version` (ADR-006). Within a major version, functions are only added.
Deprecated functions keep working for one minor release with a diagnostic.

## 9. Language subset and portability

C99 with these C11 features through macros in `include/embsim/compiler.h`:
`ES_STATIC_ASSERT`, `ES_INLINE`, `ES_NORETURN` (used only in tests and the
CLI), `ES_ALIGNAS`. Not used anywhere: variable-length arrays, `alloca`,
`setjmp`, threads in the core, `long double`, bit-fields in any exported
structure, flexible array members in public headers. Public headers compile
as C99, C11, C++17 and under MSVC's C11 mode. All public symbols are marked
with `ES_API` for shared-library export on Windows.

## 10. Coding standard

`docs/CODING-STANDARD.md` binds the whole core: undefined-behaviour policy,
integer conversions, no recursion in engines without a declared bound, one
`static` state struct per module, no `volatile` except in the host layer, no
floating point outside the analog engine and documented conversions.
