-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- KY-040 rotary encoder module (an EC11-type mechanical incremental encoder
-- with a push switch), as the encoder's data sheet and the module's
-- schematic describe it.
--
--   - two contacts, A (CLK) and B (DT), switch to the common (GND); the
--     module pulls both up with 10 kOhm to its + pin, so with + left
--     unconnected they float; SW switches to GND with no pull-up on the
--     module (`sw_pullup` for boards that fit one), so a sketch uses
--     INPUT_PULLUP;
--   - 20 detents a revolution, each one full quadrature cycle; at rest on a
--     detent both contacts are open (high); turning clockwise A closes
--     first, then B, then A opens, then B, so A leads by a quarter cycle;
--   - the shaft turns to the `position` input's detent at a hand's pace,
--     `rate` detents a second, the four contact changes of a detent evenly
--     spread;
--   - every contact change bounces: the contact chatters through three
--     extra changes within `bounce` (1 ms) before it settles, the push
--     switch too, as the data sheet's chattering figure allows.
--
-- Not modelled: the detent torque and the hand's speed varying within a
-- turn, contact resistance (a closed contact is a strong low), the
-- switch's travel, the shaft's angle between detents.
local M = {}

local CHATTER = 3 -- extra changes before a contact settles
local TOK_STEP, TOK_BOUNCE_A, TOK_BOUNCE_B, TOK_BOUNCE_SW = 0, 1, 2, 3
local PS = 1000000000000

-- the contacts (A closed, B closed) through a detent's cycle: clockwise A
-- leads, counter-clockwise B leads; both open at the detent
local CW = { { true, false }, { true, true }, { false, true }, { false, false } }
local CCW = { { false, true }, { true, true }, { true, false }, { false, false } }

local function pulled(ctx) return ctx.pins.VCC.connected end

-- a contact's pin: closed is a strong low; open is the module's pull-up
local function set_pin(ctx, pin, closed, pullup)
  if closed then
    ctx.drive(pin, 0)
  elseif pullup then
    ctx.drive(pin, 1, "weak")
  else
    ctx.release(pin)
  end
end

local function pullup_of(ctx, pin)
  if pin == "SW" then return ctx.params.sw_pullup and pulled(ctx) end
  return pulled(ctx)
end

-- a contact goes to `closed`, chattering first
local function contact(ctx, pin, closed, token)
  local s = ctx.state
  local b = s.bounce[pin]
  b.target, b.left = closed, CHATTER
  local dt = math.floor(ctx.params.bounce * PS / (CHATTER + 1) + 0.5)
  if dt <= 0 then
    b.left = 0
    set_pin(ctx, pin, closed, pullup_of(ctx, pin))
    return
  end
  b.level = closed
  set_pin(ctx, pin, closed, pullup_of(ctx, pin))
  b.at = ctx.now() + dt
  ctx.wake_at(b.at, token)
end

local function chatter(ctx, pin, token)
  local s = ctx.state
  local b = s.bounce[pin]
  if b.left <= 0 or b.at ~= ctx.now() then return end
  b.left = b.left - 1
  if b.left == 0 then
    b.level = b.target -- settled
  else
    b.level = not b.level
  end
  set_pin(ctx, pin, b.level, pullup_of(ctx, pin))
  if b.left > 0 then
    b.at = ctx.now() + math.floor(ctx.params.bounce * PS / (CHATTER + 1) + 0.5)
    ctx.wake_at(b.at, token)
  end
end

local function step_period(ctx)
  return math.floor(PS / (ctx.params.rate * 4) + 0.5)
end

local function step(ctx)
  local s = ctx.state
  if s.pos == s.target and s.phase == 0 then s.moving = false return end
  local dir = s.target > s.pos and 1 or -1
  if s.phase == 0 then s.dir = dir end
  s.phase = s.phase + 1
  local ab = (s.dir > 0 and CW or CCW)[s.phase]
  local a_closed, b_closed = ab[1], ab[2]
  if a_closed ~= s.a_closed then s.a_closed = a_closed; contact(ctx, "CLK", a_closed, TOK_BOUNCE_A) end
  if b_closed ~= s.b_closed then s.b_closed = b_closed; contact(ctx, "DT", b_closed, TOK_BOUNCE_B) end
  if s.phase == 4 then s.phase = 0; s.pos = s.pos + s.dir end
  s.next_at = ctx.now() + step_period(ctx)
  ctx.wake_at(s.next_at, TOK_STEP)
  s.moving = true
end

function M.reset(ctx)
  local s = ctx.state
  s.pos, s.target, s.phase, s.dir, s.moving = 0, 0, 0, 1, false
  s.a_closed, s.b_closed, s.pressed = false, false, false
  s.bounce = { CLK = { left = 0 }, DT = { left = 0 }, SW = { left = 0 } }
  set_pin(ctx, "CLK", false, pullup_of(ctx, "CLK"))
  set_pin(ctx, "DT", false, pullup_of(ctx, "DT"))
  set_pin(ctx, "SW", false, pullup_of(ctx, "SW"))
end

function M.on_set(ctx, name, value)
  local s = ctx.state
  if name == "position" then
    if type(value) ~= "number" or value ~= math.floor(value) then return false end
    s.target = math.floor(value)
    if not s.moving then step(ctx) end
  elseif name == "pressed" then
    s.pressed = value and true or false
    contact(ctx, "SW", s.pressed, TOK_BOUNCE_SW)
  else
    return false
  end
  return true
end

function M.on_time(ctx, token)
  local s = ctx.state
  if token == TOK_STEP then
    if s.next_at == ctx.now() then step(ctx) end
  elseif token == TOK_BOUNCE_A then chatter(ctx, "CLK", token)
  elseif token == TOK_BOUNCE_B then chatter(ctx, "DT", token)
  elseif token == TOK_BOUNCE_SW then chatter(ctx, "SW", token)
  end
end

function M.observe(ctx)
  local s = ctx.state
  return { count = s.pos, angle = (s.pos * 18) % 360, pressed = s.pressed }
end

return M
