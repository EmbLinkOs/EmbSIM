-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- Vishay TSOP38238 IR receiver module (38 kHz, AGC2), as its data sheet
-- describes it, with the 21-key NEC remote sold with Arduino kits pointed
-- at it.
--
--   - OUT is active low: an NPN to GND with the part's own 30 kOhm pull-up
--     to VS; a 38 kHz burst pulls it low after the data sheet's delay
--     (7/f0 to 13/f0: 10 carrier cycles here) for the burst's length (the
--     data sheet's tpi - 5/f0 < tpo < tpi + 5/f0: tpo = tpi here);
--   - VS from 2.0 to 5.5 V; below 2.0 V OUT does not switch;
--   - the remote sends NEC frames: a 9 ms burst and a 4.5 ms space, then
--     the address, its complement (or the extended address's high byte),
--     the command and its complement, each LSB first, a bit a 562.5 us
--     burst followed by 562.5 us (0) or 1687.5 us (1) of space, and a
--     closing burst; while the key is held, a repeat code (9 ms burst,
--     2.25 ms space, burst) every 108 ms from the frame's start;
--   - the `key` input is the key held on the remote ("none" when released):
--     a press sends its frame at once, a release lets the frame or repeat
--     in progress finish;
--   - the remote's address (`address`, 0x00 on the kit remote) and its keys'
--     command codes are the kit remote's.
--
-- The inside differs where it does not show: the light is not modelled as
-- a 38 kHz carrier but as the bursts' envelope, which is all the receiver
-- passes on.
--
-- Not modelled: distance, angle and the remote's battery (every frame
-- arrives), ambient light and the AGC (no noise pulses), the delay's and
-- pulse width's spread between parts, other protocols and carriers, the
-- remote's own key bounce.
local M = {}

local US = 1000000 -- ps
local F0 = 38000
local TD = math.floor(10 / F0 * 1e12 + 0.5) -- 10 carrier cycles
local TOK_EDGE, TOK_REPEAT = 0, 1

-- the 21-key "Car MP3" kit remote
local KEYS = {
  ["ch-"] = 0x45, ch = 0x46, ["ch+"] = 0x47, prev = 0x44, next = 0x40, play = 0x43,
  ["vol-"] = 0x07, ["vol+"] = 0x15, eq = 0x09, ["0"] = 0x16, ["100+"] = 0x19, ["200+"] = 0x0D,
  ["1"] = 0x0C, ["2"] = 0x18, ["3"] = 0x5E, ["4"] = 0x08, ["5"] = 0x1C, ["6"] = 0x5A,
  ["7"] = 0x42, ["8"] = 0x52, ["9"] = 0x4A,
}

local function powered(ctx)
  local v = ctx.analog.voltage("VS")
  return v == nil or v >= 2.0
end

-- the envelope of a frame as {mark, space, mark, space...} in us
local function frame(addr, cmd)
  local t = { 9000, 4500 }
  local lo, hi = addr & 0xFF, (addr >> 8) & 0xFF
  if addr <= 0xFF then hi = (~addr) & 0xFF end
  local bytes = { lo, hi, cmd & 0xFF, (~cmd) & 0xFF }
  for _, b in ipairs(bytes) do
    for i = 0, 7 do
      t[#t + 1] = 562.5
      t[#t + 1] = ((b >> i) & 1) == 1 and 1687.5 or 562.5
    end
  end
  t[#t + 1] = 562.5
  return t
end

local REPEAT = { 9000, 2250, 562.5 }

-- plays an envelope from now: each mark pulls OUT low TD later for its length
local function play(ctx, env)
  local s = ctx.state
  s.edges, s.ei = {}, 1
  local t = ctx.now()
  for i, d in ipairs(env) do
    local mark = (i % 2) == 1
    s.edges[#s.edges + 1] = { t + TD, mark }
    t = t + math.floor(d * US + 0.5)
  end
  s.edges[#s.edges + 1] = { t + TD, false }
  s.busy_until = t
  ctx.wake_at(s.edges[1][1], TOK_EDGE)
end

local function out(ctx, low)
  if low and powered(ctx) then ctx.drive("OUT", 0) else ctx.drive("OUT", 1, "weak") end
end

local function send(ctx)
  local s = ctx.state
  local cmd = KEYS[s.key]
  if not cmd then return end
  s.frame_start = ctx.now()
  play(ctx, s.first and frame(ctx.params.address, cmd) or REPEAT)
  s.first = false
  s.frames = s.frames + 1
  ctx.wake_at(s.frame_start + 108000 * US, TOK_REPEAT)
end

function M.reset(ctx)
  local s = ctx.state
  s.key, s.first, s.frames, s.edges, s.ei = "none", true, 0, {}, 1
  out(ctx, false)
end

function M.on_set(ctx, name, value)
  if name ~= "key" then return false end
  local s = ctx.state
  if value ~= "none" and not KEYS[value] then return false end
  local was = s.key
  s.key = value
  if value ~= "none" and value ~= was then
    -- a new press: a full frame once the previous one has finished
    s.first = true
    if not s.busy_until or ctx.now() >= s.busy_until then
      ctx.cancel(TOK_REPEAT)
      send(ctx)
    else
      ctx.wake_at(s.busy_until, TOK_REPEAT)
    end
  end
  return true
end

function M.on_time(ctx, token)
  local s = ctx.state
  if token == TOK_EDGE then
    local e = s.edges[s.ei]
    if not e or e[1] ~= ctx.now() then return end
    out(ctx, e[2])
    s.ei = s.ei + 1
    if s.edges[s.ei] then ctx.wake_at(s.edges[s.ei][1], TOK_EDGE) end
  elseif token == TOK_REPEAT then
    if s.key ~= "none" then send(ctx) end
  end
end

function M.observe(ctx)
  local s = ctx.state
  return { key = s.key, frames = s.frames }
end

return M
