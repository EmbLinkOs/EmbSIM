# EmbSIM Coding Standard

**Applies to:** every C file under `include/`, `core/`, `cli/`, `tests/` and
the C parts of `tools/`. TypeScript follows `ui/README.md`; Python follows
PEP 8 with type hints on public functions. Rules are numbered `CS-<n>` so a
review comment can name one. `tools/repository/check_banned.py` enforces the
mechanical ones.

## 1. Language

- **CS-1** C99, plus the C11 subset behind `include/embsim/compiler.h`
  (`ES_STATIC_ASSERT`, `ES_INLINE`, `ES_NORETURN`, `ES_ALIGNAS`). Code
  builds warning-free under `-std=c99 -Wall -Wextra -Wpedantic -Wconversion
  -Wshadow -Werror` on GCC and Clang, under TinyCC, and under MSVC `/W4 /WX`
  in C11 mode.
- **CS-2** Banned: variable-length arrays, `alloca`, `setjmp`/`longjmp`,
  thread-local storage, `long double`, computed `goto`, bit-fields in any
  structure that crosses a module boundary, flexible array members in public
  headers, `volatile` outside `core/host`.
- **CS-3** Compiler-specific attributes and pragmas appear only in
  `include/embsim/compiler.h` and `core/host/`.
- **CS-4** The library never calls `exit`, `abort`, `printf`, `puts`,
  `putchar` or writes to `stdout`/`stderr`; the CLI and tests may.
- **CS-5** No host clock, environment or filesystem call outside `core/host`.

## 2. Undefined and implementation-defined behaviour

- **CS-6** Signed overflow never occurs: wrap-around arithmetic uses
  unsigned types; range-check or widen first otherwise. Time arithmetic
  uses the checked helpers of `core/time`.
- **CS-7** Shifts: unsigned left operand, count proven below the width;
  masks as `(UINT32_C(1) << n)`.
- **CS-8** No strict-aliasing violation: byte views use `unsigned char *`;
  type punning uses `memcpy`.
- **CS-9** Every variable is initialised before it is read; structures are
  zero-initialised then filled.
- **CS-10** Division has a proven nonzero divisor.
- **CS-11** Relied-upon implementation-defined behaviour is asserted in
  `core/util/sanity.c`: two's complement, `CHAR_BIT == 8`, IEEE 754 doubles,
  `sizeof(void *) >= 4`.

## 3. Types and conversions

- **CS-12** Stored data uses fixed-width types; `size_t` for sizes; `int`
  for statuses and short-lived locals; `double` only in the analog engine,
  pin-model parameters and observation values.
- **CS-13** Every narrowing conversion is an explicit cast with a reason
  (comment or range check); builds use `-Wconversion`.
- **CS-14** Enumerations stored in structures are `int32_t` or `uint8_t`
  fields with a range check at the API boundary.
- **CS-15** `bool` from `<stdbool.h>` holds only `true` or `false`.

## 4. Memory and bounds

- **CS-16** All allocation goes through `esk_alloc`/`esk_free` (the
  session's allocator hooks). No allocation in a model after `create`
  except through the session's bounded pools.
- **CS-17** Every loop over a structure has a declared bound; recursion is
  allowed only in the JSON parser and the composite expander, each with a
  depth limit.
- **CS-18** File-scope mutable state is forbidden in `core/`. Registries
  are built per session or are `const` tables.
- **CS-19** Buffers passed across modules carry their lengths; string
  functions take `(ptr, len)`; `strcpy`, `sprintf` and `strcat` are not
  used (`snprintf` with checked results is).

## 5. Errors and diagnostics

- **CS-20** Every fallible function returns `es_status`; outputs are
  written only on success; partial updates are rolled back.
- **CS-21** Diagnostics are produced through `esk_diag` with a stable code
  from `docs/user/diagnostics.md`; messages name the subject (net, pin,
  register, node) and the virtual time.
- **CS-22** Internal invariants use `ESK_ASSERT`, which reports
  `ES_EINTERNAL` through the diagnostic path in release and aborts only in
  the test build.

## 6. Structure and style

- **CS-23** One module per directory with a `*.h` interface and private
  `*_priv.h` headers; public headers are in `include/embsim/` only.
- **CS-24** Functions are short enough to review; a file header comment
  states what the module is and the specification section it implements.
- **CS-25** Formatting by `.clang-format` (LLVM base, 4-space indent,
  100 columns); `scripts/format.sh` applies it; CI checks.
- **CS-26** Comments explain why; identifiers explain what. Units appear in
  identifiers of physical quantities (`delay_ps`, `vout_v`, `rout_ohm`).

## 7. Tests

- **CS-27** Every module has unit tests under `tests/unit/` using the
  project's minimal test harness (`tests/harness.h`); every fixture-driven
  behaviour (analog, digital, protocol) has fixtures under `tests/`.
- **CS-28** A bug fix adds the test that would have caught it.
