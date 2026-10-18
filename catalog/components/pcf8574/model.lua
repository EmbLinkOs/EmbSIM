-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- PCF8574 / PCF8574A remote 8-bit I/O expander for I2C, as NXP's datasheet
-- describes it.
--
--   - the address is 0100 A2 A1 A0 (0x20..0x27; the PCF8574A 0111 A2 A1 A0,
--     0x38..0x3F) from the levels on A0..A2 when the address byte comes, so
--     a backpack with the three pulled up answers at 0x27;
--   - quasi-bidirectional ports: a written 1 is a weak current source
--     (about 100 uA) that anything outside can pull low and read through, a
--     written 0 a strong low; at power-on every port is a 1;
--   - a written byte reaches the port at the acknowledge clock, and a 1 has
--     a strong pull-up during that clock's high time, as the part speeds
--     the low-to-high edge; each byte of a longer write does the same;
--   - a read returns the port's levels, taken at the acknowledge, a byte
--     each time the master acknowledges;
--   - INT_N (open drain) goes low 4 us after a port's level departs from
--     what the last read or write left, and lets go 4 us after the levels
--     come back, or at once on a read or write;
--   - below 2.5 V on VCC it does not acknowledge its address.
--
-- Not modelled: the supply current and the outputs' current limits (the
-- 100 uA source and 25 mA sink are drive strengths), an unconnected A pin
-- reads low (the datasheet wants them tied), the 400 kHz limit (faster
-- clocks are decoded too), the power-on reset's voltage threshold.
local M = {}

local US = 1000000
local T_INT = 4 * US
local VMIN = 2.5

-- the port's levels; a pin wired to nothing reads what the part drives
local function inputs(ctx)
  local b = 0
  for i = 0, 7 do
    local pin = ctx.pins["P" .. i]
    local one
    if pin.connected then one = pin.level == 1 else one = (ctx.state.latch >> i) & 1 == 1 end
    if one then b = b | (1 << i) end
  end
  return b
end

-- drives the latch: strong during the acknowledge's high time, then the
-- quasi-bidirectional weak 1
local function apply(ctx, strong)
  local s = ctx.state
  for i = 0, 7 do
    local p = "P" .. i
    if (s.latch >> i) & 1 == 1 then
      ctx.drive(p, 1, strong and "strong" or "weak")
    else
      ctx.drive(p, 0)
    end
  end
end

local function powered(ctx)
  local v = ctx.analog.voltage("VCC")
  return v == nil or v >= VMIN
end

local function interrupt(ctx, on)
  local s = ctx.state
  s.int = on
  if on then ctx.drive("INT_N", 0) else ctx.release("INT_N") end
end

function M.create(ctx)
  local s = ctx.state
  local base = ctx.params.variant == "PCF8574A" and 0x38 or 0x20
  ctx.bus.i2c_slave(base, {
    start = function(ctx) return powered(ctx) end,
    write = function(ctx, b)
      s.latch, s.ack = b, "byte" -- reaches the port at the acknowledge
    end,
    read = function(ctx)
      local b = inputs(ctx)
      s.ref = b
      interrupt(ctx, false)
      return b
    end,
  }, { address_pins = { "A0", "A1", "A2" } })
end

function M.reset(ctx)
  local s = ctx.state
  s.latch, s.ack, s.ref, s.int = 0xFF, nil, 0xFF, false
  s.scl = ctx.pins.SCL.value
  apply(ctx, false)
  ctx.release("INT_N")
end

function M.on_input(ctx, pin)
  local s = ctx.state
  if pin == "SCL" then
    local l = ctx.pins.SCL.value
    if s.ack == "byte" and s.scl == 1 and l ~= 1 then
      s.ack = "wait"
    elseif s.ack == "wait" and s.scl ~= 1 and l == 1 then
      -- the acknowledge clock's high time: the byte goes out, 1s strongly
      apply(ctx, true)
      s.ack = "high"
      interrupt(ctx, false)
    elseif s.ack == "high" and s.scl == 1 and l ~= 1 then
      apply(ctx, false)
      s.ack = nil
      s.ref = inputs(ctx)
    end
    s.scl = l
    return
  end
  if pin:match("^P%d$") and s.ack == nil then
    ctx.wake_at(ctx.now() + T_INT, 0)
  end
end

function M.on_time(ctx)
  local s = ctx.state
  if s.ack ~= nil then return end
  local changed = inputs(ctx) ~= s.ref
  if changed ~= s.int then interrupt(ctx, changed) end
end

function M.observe(ctx)
  local s = ctx.state
  return { latch = s.latch, port = inputs(ctx), interrupt = s.int }
end

return M
