-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- DHT22 (Aosong AM2302): humidity and temperature on a single open-drain
-- wire, as its datasheet describes it.
--
--   - the host holds DATA low for at least 1 ms (accepted from 800 us) and
--     lets go; 30 us later the sensor answers 80 us low, 80 us high, then 40
--     bits, each 50 us low and 26 us (0) or 70 us (1) high: humidity x10,
--     temperature x10 with its sign in the top bit, and a checksum; then
--     50 us low and the line released;
--   - it answers only while VCC is at least 3.0 V (the datasheet's range is
--     3.3 to 6 V), and not in the first second after power-up;
--   - a read returns the measurement made at the end of the previous read
--     (the datasheet's "previous" data; DHT libraries warn readings may be
--     2 s old), the first one the measurement made at power-up; a new
--     measurement comes at most every 2 s, a read sooner repeats the last;
--   - the element follows the air with the datasheet's response times, a
--     first-order lag of 5 s for humidity and 10 s for temperature, so a
--     change in the room shows in the readings over the next seconds;
--   - readings are clamped to the range, -40 to 80 C and 0 to 99.9 %RH, in
--     steps of 0.1.
--
-- Not modelled: the accuracy spread between parts (+-0.5 C, +-2 %RH), long
-- term drift, the supply current, power cycling between two reads (power
-- is looked at when a start signal comes).
local M = {}

local US = 1000000
local S = 1000000 * US
local TAU_H, TAU_T = 5 * S, 10 * S
local VMIN = 3.0

local function ambient(ctx)
  return ctx.input("humidity") or 50, ctx.input("temperature") or 25
end

local function powered(ctx)
  local v = ctx.analog.voltage("VCC")
  return v == nil or v >= VMIN
end

-- the element up to now, the air having been what it was since the last update
local function settle(ctx)
  local s = ctx.state
  local now = ctx.now()
  local dt = now - s.upd
  if dt > 0 then
    s.eh = s.eh + (s.ah - s.eh) * (1 - math.exp(-dt / TAU_H))
    s.et = s.et + (s.at - s.et) * (1 - math.exp(-dt / TAU_T))
  end
  s.upd = now
end

local function clamp(x, lo, hi) return x < lo and lo or (x > hi and hi or x) end

local function measure(ctx)
  local s = ctx.state
  settle(ctx)
  s.mh = math.floor(clamp(s.eh, 0, 99.9) * 10 + 0.5)
  s.mt = math.floor(clamp(s.et, -40, 80) * 10 + (s.et >= 0 and 0.5 or -0.5))
  s.measured = ctx.now()
end

function M.reset(ctx)
  local s = ctx.state
  s.busy, s.low_at, s.count = false, nil, 0
  s.ah, s.at = ambient(ctx)
  s.eh, s.et, s.upd = s.ah, s.at, ctx.now()
  s.reset_at = ctx.now()
  -- powered from the start (the circuit is not solved yet at a reset: a
  -- supply found missing at a start signal is what turns it off)
  s.on_at = ctx.now()
  measure(ctx) -- the power-up measurement
  ctx.release("DATA")
end

function M.on_set(ctx, name, value)
  if name ~= "humidity" and name ~= "temperature" then return end
  local s = ctx.state
  settle(ctx)
  s.ah, s.at = ambient(ctx)
  if ctx.now() == s.reset_at then
    -- the room as it was from the start: the element and its power-up
    -- measurement are already there
    s.eh, s.et = s.ah, s.at
    measure(ctx)
  end
end

local function frame(ctx)
  local s = ctx.state
  local h, t = s.mh, s.mt
  if t < 0 then t = 0x8000 | (-t) end
  local bytes = { h >> 8, h & 0xFF, t >> 8, t & 0xFF }
  bytes[5] = (bytes[1] + bytes[2] + bytes[3] + bytes[4]) & 0xFF
  local seq = { { 0, 80 }, { 1, 80 } }
  for _, b in ipairs(bytes) do
    for i = 7, 0, -1 do
      seq[#seq + 1] = { 0, 50 }
      seq[#seq + 1] = { 1, ((b >> i) & 1) == 1 and 70 or 26 }
    end
  end
  seq[#seq + 1] = { 0, 50 }
  return seq
end

function M.on_input(ctx, pin)
  local s = ctx.state
  if pin ~= "DATA" or s.busy then return end
  if ctx.pins.DATA.level == 0 then
    s.low_at = ctx.now()
    return
  end
  local started = s.low_at and ctx.now() - s.low_at >= 800 * US -- the host's start signal
  s.low_at = nil
  if not started then return end
  if not powered(ctx) then
    s.on_at = nil
    return
  end
  if not s.on_at then s.on_at = ctx.now() end
  if ctx.now() - s.on_at < S then return end -- still starting up
  s.busy, s.seq, s.i = true, frame(ctx), 0
  s.sent_h, s.sent_t = s.mh / 10, s.mt / 10
  ctx.wake_at(ctx.now() + 30 * US, 0)
end

function M.on_time(ctx)
  local s = ctx.state
  s.i = s.i + 1
  local seg = s.seq[s.i]
  if not seg then
    ctx.release("DATA")
    s.busy = false
    s.count = s.count + 1
    if ctx.now() - s.measured >= 2 * S then measure(ctx) end -- for the next read
    return
  end
  if seg[1] == 0 then ctx.drive("DATA", 0) else ctx.release("DATA") end
  ctx.wake_at(ctx.now() + seg[2] * US, 0)
end

function M.observe(ctx)
  local s = ctx.state
  -- what the last read sent
  return { readings = s.count, humidity = s.sent_h or 0, temperature = s.sent_t or 0 }
end

return M
