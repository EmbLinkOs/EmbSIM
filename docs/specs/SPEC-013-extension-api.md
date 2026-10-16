# SPEC-013 - Extension API (Lua Behavioural Models)

**Status:** Accepted in outline 2026-10-15; completed before M3. **Builds
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

## 3. Electrical scripted models

A script may not stamp matrices. Electrical behaviour comes from a
composite (SPEC-005 §5) whose controlled sources take their values from the
script's `observe` outputs (`"value": "$out.vout"`), evaluated at scheduler
events; this keeps the analog engine deterministic and the script simple.

## 4. Conformance

The same fixtures (SPEC-005 §9) run against scripted and primitive models;
a scripted model declares fidelity like any other. Scripts are part of the
library and are hashed into recordings.
