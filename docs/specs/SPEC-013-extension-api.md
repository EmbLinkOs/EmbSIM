# SPEC-013 - Extension API (Lua Behavioural Models)

**Status:** Accepted 2026-10-15; §2.1 records the M3 implementation. **Builds
on:** ADR-009; SPEC-005 §5. **Requirements:** `docs/requirements/EXT.md`.

---

## 1. Runtime

Lua 5.4 vendored under `third_party/lua/` (MIT), built into `libembsim`
when `EMBSIM_LUA=ON` (default on). One Lua state per session; each scripted
model instance has its own environment table; the standard libraries
`io`, `os`, `debug`, `package`, `load` and `require` are removed;
`string`, `table`, `math` and `utf8` remain. An instruction-count hook
aborts an evaluation that exceeds `lua_budget` (default 1 000 000
instructions) with `ext.budget_exceeded`.

## 2. Model shape

```lua
local M = {}

function M.create(ctx)             -- ctx.params, ctx.pins, ctx.state (persisted across evaluations)
  ctx.state.count = 0
end

function M.reset(ctx) ctx.state.count = 0 end

function M.on_input(ctx, pin)      -- a declared input pin changed
  if pin == "CLK" and ctx.pins.CLK.value == 1 then
    ctx.state.count = (ctx.state.count + 1) % 16
    ctx.drive("Q0", ctx.state.count & 1, "strong", ctx.params.tpd_ps)
  end
end

function M.on_time(ctx, token) end  -- a wake-up scheduled by ctx.wake_at(time_ps, token)

function M.on_set(ctx, name, value) end  -- an interactive input (SPEC-005 §7)

function M.observe(ctx) return {count = ctx.state.count} end  -- outputs for artwork and probes

return M
```

`ctx` API: `ctx.now()` (picoseconds as an integer, 64-bit), `ctx.drive(pin,
value, strength, delay_ps)`, `ctx.release(pin)`, `ctx.pins[name].value`,
`ctx.wake_at(time_ps, token)`, `ctx.cancel(token)`, `ctx.diag(severity,
code, message)`, `ctx.bus` helpers for I²C and SPI slave behaviour
(`ctx.bus.i2c_slave(address, handlers)`, `ctx.bus.spi_slave(handlers)`)
implemented in C over the pins so a scripted device never bit-bangs a
protocol itself, `ctx.analog` read-only node voltages for behavioural
sensors whose electrical side is a composite.

### 2.1 As implemented (M3)

- **Levels.** `ctx.pins[name].value` reads the pin (with the floating and
  unknown diagnostics of `esk_pin_read`); `.level` reads it without them;
  both are `0`, `1`, `"z"` (nothing drives the net) or `"x"` (unknown).
  `.name` and `.connected` describe the pin. `ctx.drive` accepts `0`, `1`,
  `true`, `false`, `"0"`, `"1"`, `"z"` (the same as `ctx.release`) and
  `"x"`; strengths are `"strong"` (default), `"weak"` and `"supply"`; the
  delay defaults to 0.
- **Wake-ups.** Tokens are 0 to 7; a new `wake_at` with a token replaces
  the pending one; a time in the past is an error.
- **Lifecycle.** At every reset the instance gets a fresh `ctx.state`,
  then `M.create(ctx)` and `M.reset(ctx)` run (either may be absent), so a
  model drives its initial outputs in either. Every digital input and
  bidirectional pin is watched; `M.on_input(ctx, pin)` gets the pin's name.
- **Inputs and outputs.** `M.on_set(ctx, name, value)` returning `false`
  refuses the value (the input is not set); a missing `on_set` refuses
  every input. `ctx.input(name)` reads an input's current value.
  `M.observe(ctx)` returns a table whose string keys become outputs.
- **Isolation.** The chunk runs once per instance with its own environment
  table, so file-scope locals and globals belong to the instance; the
  environment reads through to a shared base holding the allowed globals and
  the `string`, `table`, `math` and `utf8` libraries (`string.dump`
  removed). Only text chunks are loaded. `print(...)` is an `ext.print`
  information diagnostic.
- **Determinism.** The interpreter's string-hash seed and `table.sort`
  pivot are constants and `math.random` is seeded with 0 when the state is
  created, so a session replays identically. Iteration with `pairs` over
  tables keyed by tables or functions depends on addresses; a model that
  needs a stable order sorts its keys.
- **Failures.** A syntax error refuses the load (`ext.error`). A runtime
  error, or an evaluation past `lua_budget` (1 000 000 instructions,
  `ext.budget_exceeded`), is an error diagnostic naming the component; that
  instance stops until the next reset and the rest of the circuit runs on.
  The Lua state's memory is capped at 64 MiB.
- **Buses.** `ctx.bus.i2c_slave(address, handlers, pins?)` decodes I²C
  from the pins' edges: 7-bit address, open drain (the model's SDA is
  released for 1 and pulled up outside), repeated START, no clock stretching.
  Handlers: `start(ctx, "read"|"write")`, `write(ctx, byte)` (returning
  `false` sends a NACK), `read(ctx) -> byte`, `stop(ctx)`. Pins default to
  `SDA` and `SCL`. `ctx.bus.spi_slave(handlers, options?)` decodes 8-bit
  frames, MSB first, in modes 0 to 3 (`options.mode`), CS active low,
  releasing MISO while not selected. Handlers: `select(ctx) -> first byte`,
  `transfer(ctx, byte) -> next byte`, `deselect(ctx)`. Pins default to
  `SCK`/`SCLK`/`CLK`, `MOSI`/`SDI`/`DIN`/`SI`, `MISO`/`SDO`/`DOUT`/`SO` (optional)
  and `CS`/`SS`/`CE`/`NSS`; `options` may name them (`sck`, `mosi`, `miso`, `cs`).
  A model registers its devices in `M.create`; each reset registers them
  again.
- **Not yet implemented:** `ctx.analog`; the `lua_budget` setting (the
  default applies).

## 3. Electrical scripted models

A script may not stamp matrices. Electrical behaviour comes from a
composite (SPEC-005 §5) whose controlled sources take their values from the
script's `observe` outputs (`"value": "$out.vout"`), evaluated at scheduler
events; this keeps the analog engine deterministic and the script simple.

## 4. Conformance

The same fixtures (SPEC-005 §9) run against scripted and primitive models;
a scripted model declares fidelity like any other. Scripts are part of the
library and are hashed into recordings.
