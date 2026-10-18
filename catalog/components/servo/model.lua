-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- RC hobby servo (TowerPro SG90 class), as its datasheet describes it.
--
--   - each pulse on SIG (0.4 to 3 ms are taken; others are ignored) sets
--     the horn's target, linear from min_pulse (0 deg) to max_pulse (the
--     full range) and clamped;
--   - a pulse within the dead band (10 us) of the one the servo is holding
--     changes nothing, as its comparator ignores it;
--   - the horn turns toward the target at the datasheet's speed, 0.1 s per
--     60 deg at 4.8 V (`speed`, 600 deg/s), faster in proportion to the
--     supply (0.08 s per 60 deg at 6 V);
--   - the electronics drive the motor for a frame after each pulse: when
--     the pulses stop, the horn stays where it is;
--   - below 3.5 V on V+ the motor does not turn.
--
-- Not modelled: a load on the horn (torque, 1.8 kgf cm at 4.8 V, and
-- stall), inertia and overshoot, the supply current, the spread of the
-- pulse-to-angle mapping between parts.
local M = {}

local PS = 1e12
local FRAME = 25e9 -- ps the motor runs after a pulse

local function volts(ctx)
  local v = ctx.analog.voltage("V+")
  return v or 5.0
end

-- the horn up to now: it has turned toward the target while driven
local function advance(ctx)
  local s = ctx.state
  local now = ctx.now()
  local stop = math.min(now, s.drive_until)
  if stop > s.t and s.angle ~= s.target then
    local step = s.rate * (stop - s.t) / PS
    if s.target > s.angle then
      s.angle = math.min(s.target, s.angle + step)
    else
      s.angle = math.max(s.target, s.angle - step)
    end
  end
  s.t = now
end

function M.reset(ctx)
  local s, p = ctx.state, ctx.params
  s.angle, s.target, s.t, s.drive_until, s.rate = p.initial, p.initial, ctx.now(), 0, 0
  s.width, s.held, s.lvl, s.rise = 0, nil, ctx.pins.SIG.value, nil
end

function M.on_input(ctx, pin)
  local s, p = ctx.state, ctx.params
  if pin ~= "SIG" then return end
  local l = ctx.pins.SIG.value
  if s.lvl ~= 1 and l == 1 then
    s.rise = ctx.now()
  elseif s.lvl == 1 and l ~= 1 and s.rise then
    local w = (ctx.now() - s.rise) / 1e6 -- microseconds
    if w >= 400 and w <= 3000 then
      advance(ctx)
      s.width = w
      local v = volts(ctx)
      if v >= 3.5 then
        if not s.held or math.abs(w - s.held) >= p.dead_band * 1e6 then
          local a = (w - p.min_pulse * 1e6) / ((p.max_pulse - p.min_pulse) * 1e6) * p.range
          s.target, s.held = math.max(0, math.min(p.range, a)), w
        end
        s.rate = p.speed * v / 4.8
        s.drive_until = ctx.now() + FRAME
      end
    end
  end
  s.lvl = l
end

function M.observe(ctx)
  local s = ctx.state
  advance(ctx)
  return { angle = s.angle, target = s.target, pulse_us = s.width }
end

return M
