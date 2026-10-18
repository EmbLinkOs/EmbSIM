-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- 28BYJ-48 5 V geared stepper motor, as its data sheet and its construction
-- describe it.
--
--   - four half windings on a common (red): orange and pink are the two
--     halves of one winding, yellow and blue of the other; a half carries
--     current while its lead is pulled below red (a ULN2003 sinking it), so
--     each makes a field a quarter cycle on from the last in the order
--     orange, yellow, pink, blue, the data sheet's switching sequence;
--   - with red left open the windings work bipolar (orange to pink, yellow
--     to blue, through both halves), as from an H-bridge;
--   - 50 Ohm a half (5 V gives 100 mA), 3 mH, the current rising and
--     falling with L/R and opposed by the back EMF;
--   - a permanent-magnet rotor of 8 pole pairs, 32 full steps (64 half
--     steps of 5.625 degrees, the data sheet's stride angle) a turn, with a
--     detent torque; geared down 63.684:1 (the gears' 32/9, 22/11, 26/9,
--     31/10; the data sheet rounds it to 1/64), so 2037.9 full steps turn
--     the output shaft once;
--   - the rotor follows the field as a mass: its inertia, the gearbox's
--     friction and damping and the windings' torque are integrated in 20 us
--     steps, so stepped faster than it can follow it misses steps or stalls
--     and hums, as a real one does: calibrated to the data sheet's idle
--     in-traction (pull-in) above 600 steps a second from standstill and
--     out-traction above 1000 with a ramp;
--   - two opposite halves of one winding on together cancel (a lead order
--     the sketch does not match makes the rotor shudder in place);
--   - a load on the output shaft (the `load` input, against the turning
--     direction) reaches the rotor through the gears.
--
-- The inside differs from the motor where it does not show: the voltages
-- across the halves are logic levels at `supply` (or the analog nets' own
-- voltages), sampled 5 us after a lead changes and every millisecond while
-- the rotor moves; the rotor's model state is advanced lazily at those
-- times and read without being advanced further by observe.
--
-- Each lead is pulled up through its half winding: 50 Ohm to the 5 V rail
-- (a weak 1 under the functional policy), so an undriven lead carries no
-- current and a driver's saturation voltage shows on the analog nets.
--
-- Not modelled: the gears' backlash and their self-locking against a load
-- that back-drives the shaft (the data sheet's 600 to 1200 gf.cm), the
-- windings' pull going to red's own voltage (it goes to the 5 V rail), the
-- coupling between the two halves of a winding (each decays alone), the
-- supply current drawn on the nets, heating (the data sheet's 40 K rise),
-- the sound it makes.
local M = {}

local PS = 1000000000000
local DT = 20e-6 -- the integration step, s
local DT_PS = 20000000
local TICK_PS = 1000000000 -- 1 ms while the rotor moves
local SAMPLE_PS = 5000000 -- 5 us after a lead changes
local TOK_TICK, TOK_SAMPLE = 0, 1

local P = 8 -- pole pairs
local J = 1.3e-8 -- rotor and reflected gear inertia, kg m^2
local K = 0.6e-3 -- one half's torque at 100 mA, N m (34.3 mN m at the shaft)
local KD = 0.05e-3 -- detent torque, N m
local TC = 0.15e-3 -- gearbox friction at the rotor, N m
local B = 1e-6 -- viscous damping at the rotor, N m s
local OPEN_V = 0.05 -- below this a half carries no driven current

local HALF = { "ORANGE", "YELLOW", "PINK", "BLUE" } -- fields at 0, 90, 180, 270 degrees

local function volts(ctx, pin)
  local l = ctx.pins[pin].level
  if l ~= 0 and l ~= 1 then return nil end
  local v = ctx.analog.voltage(pin)
  if v ~= nil then return v end
  return l * ctx.params.supply
end

-- the voltage across each half (unipolar) or winding (bipolar) now
local function sample(ctx)
  local s = ctx.state
  local u = {}
  local red = ctx.pins.RED.connected and ctx.pins.RED.level or "z"
  if red ~= 0 and red ~= 1 then red = "z" end
  local bipolar = red == "z"
  if bipolar ~= s.bipolar then s.i = { 0.0, 0.0, 0.0, 0.0 } end
  s.bipolar = bipolar
  if not bipolar then
    local vc = volts(ctx, "RED")
    for j = 1, 4 do
      local vx = volts(ctx, HALF[j])
      u[j] = (vc and vx) and vc - vx or 0
    end
  else
    for k = 1, 2 do
      local va, vb = volts(ctx, HALF[k]), volts(ctx, HALF[k + 2])
      u[k] = (va and vb) and va - vb or 0
    end
  end
  s.u = u
  s.settled = false
end

-- advance a state table by dt seconds of constant applied voltages; a1 and
-- a2 are the half's and the winding's current factors for dt
local function substep(c, s, dt, a1, a2)
  local R, kt = c.R, c.kt
  local e = P * s.th
  local sn, cs = math.sin(e), math.cos(e)
  local w = s.w
  local T = 0.0
  local i, u = s.i, s.u
  local done = true
  if not s.bipolar then
    local g1, g2, g3, g4 = -sn, cs, sn, -cs -- sin(field - e) of each half
    for j = 1, 4 do
      local g = (j == 1 and g1) or (j == 2 and g2) or (j == 3 and g3) or g4
      local uj = u[j]
      local tgt = 0.0
      if uj > OPEN_V or uj < -OPEN_V then
        tgt = (uj - kt * w * g) / R
        if (uj > 0 and tgt < 0) or (uj < 0 and tgt > 0) then tgt = 0.0 end -- a driver sinks or sources only
      end
      local d = (tgt - i[j]) * a1
      if d > 1e-7 or d < -1e-7 then done = false end
      i[j] = i[j] + d
      T = T + kt * i[j] * g
    end
  else
    for k = 1, 2 do
      local g = k == 1 and 2 * sn or -2 * cs -- a winding's current through both halves
      local uk = u[k]
      local tgt = 0.0
      if uk > OPEN_V or uk < -OPEN_V then tgt = (uk - kt * w * g) / (2 * R) end
      local d = (tgt - i[k]) * a2
      if d > 1e-7 or d < -1e-7 then done = false end
      i[k] = i[k] + d
      T = T + kt * i[k] * g
    end
  end
  T = T - KD * math.sin(4 * e) - s.load / c.ratio
  if w == 0.0 and T <= TC and T >= -TC then
    -- held by the gearbox's friction
    if done then s.settled = true end
    return
  end
  local f = (w > 0 or (w == 0 and T > 0)) and -TC or TC
  local nw = w + (T + f - B * w) / J * dt
  if w ~= 0 and nw * w < 0 then nw = 0.0 end
  s.w = nw
  s.th = s.th + nw * dt
end

-- advance s from s.t to t (ps)
local function advance(ctx, s, t)
  local c = ctx.state.c
  while s.t < t and not s.settled do
    local step = t - s.t
    if step >= DT_PS then
      substep(c, s, DT, c.a1, c.a2)
      s.t = s.t + DT_PS
    else
      local dt = step / PS
      substep(c, s, dt, 1 - math.exp(-dt / c.tau), 1 - math.exp(-dt / (2 * c.tau)))
      s.t = t
    end
  end
  s.t = t
end

local function keep_ticking(ctx)
  local s = ctx.state
  if not s.settled and not s.ticking then
    s.ticking = true
    ctx.wake_at(ctx.now() + TICK_PS, TOK_TICK)
  end
end

function M.reset(ctx)
  local s = ctx.state
  local R, L = ctx.params.resistance, ctx.params.inductance
  local tau = L / R
  s.c = { R = R, kt = K * R / 5.0, ratio = ctx.params.ratio, tau = tau, -- kt: K at 5 V across 50 Ohm
    a1 = 1 - math.exp(-DT / tau), a2 = 1 - math.exp(-DT / (2 * tau)) } -- a winding: 4L over 2R
  s.th, s.w, s.t = 0.0, 0.0, ctx.now()
  s.i = { 0.0, 0.0, 0.0, 0.0 }
  s.load, s.ticking, s.bipolar = 0.0, false, nil
  sample(ctx)
  s.settled = false
  keep_ticking(ctx)
end

function M.on_input(ctx, pin)
  local s = ctx.state
  if not s.c then return end -- before the first reset
  advance(ctx, s, ctx.now())
  sample(ctx)
  ctx.wake_at(ctx.now() + SAMPLE_PS, TOK_SAMPLE)
  keep_ticking(ctx)
end

function M.on_time(ctx, token)
  local s = ctx.state
  if not s.c then return end
  advance(ctx, s, ctx.now())
  if token == TOK_SAMPLE then
    sample(ctx)
    keep_ticking(ctx)
  elseif token == TOK_TICK then
    s.ticking = false
    if not s.settled then sample(ctx) end
    keep_ticking(ctx)
  end
end

function M.on_set(ctx, name, value)
  if name ~= "load" or type(value) ~= "number" then return false end
  local s = ctx.state
  if not s.c then return false end
  advance(ctx, s, ctx.now())
  s.load = value
  s.settled = false
  keep_ticking(ctx)
  return true
end

function M.observe(ctx)
  local s = ctx.state
  if not s.c then return { angle = 0, steps = 0, rpm = 0, current = 0 } end
  -- read the rotor as it is now without moving the model's own state
  local c = { th = s.th, w = s.w, t = s.t, settled = s.settled, bipolar = s.bipolar, u = s.u, load = s.load,
    i = { s.i[1], s.i[2], s.i[3], s.i[4] } }
  advance(ctx, c, ctx.now())
  local ratio = s.c.ratio
  local amps = 0.0
  for j = 1, 4 do amps = amps + math.abs(c.i[j]) end
  local steps = c.th * P / (math.pi / 2)
  return {
    angle = math.floor(c.th / ratio * 180 / math.pi * 1000 + 0.5) / 1000,
    steps = math.floor(steps * 100 + 0.5) / 100,
    rpm = math.floor(c.w / ratio * 60 / (2 * math.pi) * 100 + 0.5) / 100,
    current = math.floor(amps * 1000 + 0.5) / 1000,
  }
end

return M
