-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- HC-SR04 ultrasonic distance module, as its datasheet describes it.
--
--   - a TRIG pulse of at least 10 us starts a measurement on its falling
--     edge: the module sends eight 40 kHz cycles and raises ECHO
--     `burst_delay` later (250 us; modules vary);
--   - ECHO stays high for the sound's round trip, 2 d / c, with the speed
--     of sound in air c = 331.3 + 0.606 T m/s at the air's temperature T
--     (58.24 us per cm at 20 C); a target nearer than 2 cm or beyond 400 cm
--     gives no echo, and ECHO falls after the 38 ms timeout;
--   - a trigger while a measurement is under way is ignored;
--   - it needs its 5 V supply: below 4.0 V it does not answer.
--
-- Not modelled: the 15 degree beam and targets beside it or several
-- targets (the distance is to the one in front), the target's material and
-- angle, humidity's small effect on the speed of sound, the supply current.
local M = {}

local US = 1000000

local function powered(ctx)
  local v = ctx.analog.voltage("VCC")
  return v == nil or v >= 4.0
end

function M.reset(ctx)
  local s = ctx.state
  s.trig, s.rise, s.busy, s.echo_us = ctx.pins.TRIG.value, nil, false, 0
  ctx.drive("ECHO", 0)
end

function M.on_input(ctx, pin)
  local s = ctx.state
  if pin ~= "TRIG" then return end
  local t = ctx.pins.TRIG.value
  if s.trig ~= 1 and t == 1 then
    s.rise = ctx.now()
  elseif s.trig == 1 and t ~= 1 and s.rise and ctx.now() - s.rise >= 10 * US and not s.busy and powered(ctx) then
    s.busy = true
    ctx.wake_at(ctx.now() + math.floor(ctx.params.burst_delay * 1e12 + 0.5), 0)
  end
  s.trig = t
end

function M.on_time(ctx, token)
  local s = ctx.state
  if token == 0 then
    local d = ctx.input("distance_cm") or 100
    local temp = ctx.input("temperature") or 20
    local c = 331.3 + 0.606 * temp -- m/s
    local us
    if d >= 2 and d <= 400 then
      us = 2 * d / 100 / c * 1e6
    else
      us = 38000 -- no echo: the timeout
    end
    s.echo_us = math.floor(us + 0.5)
    ctx.drive("ECHO", 1)
    ctx.wake_at(ctx.now() + math.floor(us * US + 0.5), 1)
  else
    ctx.drive("ECHO", 0)
    s.busy = false
  end
end

function M.observe(ctx) return { echo_us = ctx.state.echo_us } end

return M
