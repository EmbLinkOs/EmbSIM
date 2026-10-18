-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- WS2812B intelligent control LED (an RGB LED with its driver inside), and
-- strips and rings of them chained DOUT to DIN, as Worldsemi's datasheet
-- describes the part.
-- Shared by: ws2812b, ws2812b_strip
--
--   - DIN at 800 kbit/s, NRZ: each bit a high pulse then a low, the high
--     0.4 us for a 0 and 0.8 us for a 1 (+-150 ns); the part tells them
--     apart at 0.6 us, between the datasheet's longest 0 (0.55 us) and
--     shortest 1 (0.65 us);
--   - each LED keeps the first 24 bits it sees after a reset, green, red,
--     blue, most significant bit first, and passes the rest on at DOUT,
--     reshaped to its own 0.4 / 0.8 us highs, so a chain of `count` LEDs
--     takes 24 x count bits and forwards what follows;
--   - a low lasting `t_reset` (50 us for the WS2812B; 280 us for the
--     WS2812B-V5) latches the received colours into the LEDs and readies
--     them for the next frame; until then they keep showing the old ones;
--   - at power-up the LEDs are off; below 3.5 V on VDD they stay dark and
--     take no data.
--
-- Not modelled: the light itself (intensity per channel, the colours
-- reported as the 8-bit PWM levels), the 400 Hz PWM flicker, the supply
-- current (up to 60 mA per LED at full white) and its voltage drop along a
-- strip, the input threshold's dependence on VDD (a 3.3 V host on a 5 V
-- strip is decided by the pin model's levels), the reshaping's 0.2 us delay.
local M = {}

local US = 1000000
local T_THRESHOLD = 600000 -- ps: 0.6 us
local T0H, T1H = 400000, 800000
local TOK_LATCH, TOK_OUT_LOW = 1, 2

local function powered(ctx)
  local v = ctx.analog.voltage("VDD")
  return v == nil or v >= 3.5
end

local function count(ctx) return math.tointeger(ctx.params.count) or 1 end

function M.reset(ctx)
  local s = ctx.state
  s.shown, s.recv = {}, {}
  for i = 1, count(ctx) do s.shown[i], s.recv[i] = 0, 0 end
  s.bits, s.din, s.rise, s.fall = 0, ctx.pins.DIN.value, nil, nil
  s.out_high, s.frames = false, 0
  ctx.drive("DOUT", 0)
end

local function latch(ctx)
  local s = ctx.state
  if s.bits > 0 then
    for i = 1, count(ctx) do s.shown[i] = s.recv[i] end
    s.frames = s.frames + 1
  end
  s.bits = 0
end

function M.on_input(ctx, pin)
  local s = ctx.state
  if pin ~= "DIN" then return end
  local v = ctx.pins.DIN.value
  if v == s.din then return end
  s.din = v
  local now = ctx.now()
  if v == 1 then
    s.rise = now
    if s.bits >= 24 * count(ctx) and powered(ctx) then
      ctx.drive("DOUT", 1) -- a bit for the next LED: its high starts now
      s.out_high = true
    end
  else
    s.fall = now
    ctx.wake_at(now + math.floor(ctx.params.t_reset * 1e12 + 0.5), TOK_LATCH)
    if not s.rise or not powered(ctx) then return end
    local bit = (now - s.rise) >= T_THRESHOLD and 1 or 0
    local n = count(ctx)
    if s.bits < 24 * n then
      local led = s.bits // 24 + 1
      local k = 23 - s.bits % 24 -- GRB, MSB first
      s.recv[led] = s.recv[led] | (bit << k)
      if k == 23 then s.recv[led] = bit << 23 end -- a new LED's first bit
    elseif s.out_high then
      -- reshaped: the high lasts the part's own 0.4 or 0.8 us
      local t = s.rise + (bit == 1 and T1H or T0H)
      if t <= now then
        ctx.drive("DOUT", 0)
        s.out_high = false
      else
        s.out_at = t
        ctx.wake_at(t, TOK_OUT_LOW)
      end
    end
    s.bits = s.bits + 1
  end
end

function M.on_time(ctx, token)
  local s = ctx.state
  if token == TOK_LATCH then
    -- still low since the last falling edge, for t_reset: the frame ends
    if s.din ~= 1 and s.fall and ctx.now() - s.fall >= math.floor(ctx.params.t_reset * 1e12 + 0.5) then
      latch(ctx)
    end
  elseif token == TOK_OUT_LOW and s.out_at == ctx.now() then
    ctx.drive("DOUT", 0)
    s.out_high = false
  end
end

local function rgb(grb)
  local g, r, b = (grb >> 16) & 0xFF, (grb >> 8) & 0xFF, grb & 0xFF
  return string.format("%02X%02X%02X", r, g, b)
end

function M.observe(ctx)
  local s = ctx.state
  local n = count(ctx)
  local list, lit = {}, 0
  for i = 1, n do
    local on = powered(ctx) and s.shown[i] or 0
    list[i] = rgb(on)
    if on ~= 0 then lit = lit + 1 end
  end
  local o = { colors = table.concat(list, " "), lit = lit, frames = s.frames }
  if n == 1 then o.color = "#" .. list[1] end
  return o
end

return M
