-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- HX711 24-bit load-cell ADC module with a strain-gauge load cell on its
-- channel A (the common kit), as Avia Semiconductor's HX711 data sheet and
-- the module's schematic describe it.
--
--   - DOUT high while no data is ready; it goes low when a conversion ends,
--     at the data rate (`rate`: 10 Hz, the module's RATE pin low, or 80 Hz);
--   - each PD_SCK pulse shifts out one bit, MSB first, DOUT changing within
--     0.1 us of the rising edge; 24 bits of two's complement, saturating at
--     800000h and 7FFFFFh; the 25th pulse pulls DOUT high again; 25, 26 or
--     27 pulses in all select channel A at gain 128, B at 32 or A at 64 for
--     the next conversion;
--   - PD_SCK held high for more than 60 us powers the chip down (DOUT
--     high); taken low again it resets to channel A, gain 128;
--   - after power-up, a reset or a change of channel or gain, the first
--     data is ready after the data sheet's settling time (4 conversion
--     periods: 400 ms at 10 Hz, 50 ms at 80 Hz);
--   - the module's regulator makes AVDD 4.3 V (1.25 V x (20k + 8.2k) /
--     8.2k), lower when VCC leaves it no headroom; nothing works below the
--     data sheet's 2.6 V;
--   - the load cell is a bridge excited from AVDD: its output is the load
--     over `capacity` times `sensitivity` (mV/V), plus its zero balance,
--     ratiometric, so the code does not depend on AVDD; full scale is
--     +-0.5 AVDD / gain; the data sheet's input noise (50 nV rms at 10 Hz,
--     90 nV at 80 Hz, gain 128) is added (`noise` scales it, 0 for an
--     ideal part);
--   - channel B (B+ and B- on the module) is left open: it reads 0 plus
--     noise.
--
-- Not modelled: the load cell's creep, hysteresis, nonlinearity and
-- temperature drift, the ADC's offset and gain drift, the internal
-- oscillator's spread (the rate is exact), an external crystal, the
-- module's E+/E-/A+/A- terminals as wires (the cell is part of the kit).
local M = {}

local US = 1000000
local MS = 1000 * US
local TOK_READY, TOK_PD = 0, 1
local GAIN = { [25] = 128, [26] = 32, [27] = 64 }

local function vcc(ctx)
  local v = ctx.analog.voltage("VCC")
  return v or 5.0
end

local function period(ctx) return ctx.params.rate == "80" and 12500 * US or 100 * MS end

-- one gaussian sample (Box-Muller); math.random is seeded per session
local function gauss()
  local u1, u2 = math.random(), math.random()
  if u1 < 1e-12 then u1 = 1e-12 end
  return math.sqrt(-2 * math.log(u1)) * math.cos(2 * math.pi * u2)
end

local function convert(ctx)
  local s = ctx.state
  local p = ctx.params
  local gain = s.gain
  local v = 0.0 -- volts per volt of excitation
  if s.channel == "A" then
    local load = ctx.input("load") or 0
    v = (load / p.capacity * p.sensitivity + p.zero_balance) * 1e-3
  end
  local avdd = math.min(4.3, vcc(ctx) - 0.15)
  local nv = (p.rate == "80" and 90 or 50) * p.noise * 1e-9 * math.sqrt(128 / gain)
  local volts = v * avdd + nv * gauss()
  local code = math.floor(volts * gain * (1 << 24) / avdd + 0.5)
  if code > 0x7FFFFF then code = 0x7FFFFF elseif code < -0x800000 then code = -0x800000 end
  return code & 0xFFFFFF
end

local function schedule(ctx, settle)
  local s = ctx.state
  local per = period(ctx)
  s.ready_at = ctx.now() + (settle and 4 * per or per)
  ctx.wake_at(s.ready_at, TOK_READY)
end

local function restart(ctx)
  local s = ctx.state
  s.channel, s.gain = "A", 128
  s.clocks, s.reading, s.down = 0, false, false
  ctx.drive("DT", 1)
  schedule(ctx, true)
end

local function powered(ctx) return vcc(ctx) >= 2.6 end

function M.reset(ctx)
  local s = ctx.state
  s.data, s.ready, s.sck = 0, false, ctx.pins.SCK.level == 1
  s.conversions = 0
  restart(ctx)
end

function M.on_input(ctx, pin)
  if pin ~= "SCK" then return end
  local s = ctx.state
  local high = ctx.pins.SCK.level == 1
  if high == s.sck then return end
  s.sck = high
  if s.down then
    if not high then restart(ctx) end -- back from power-down: a reset
    return
  end
  if high then
    s.high_at = ctx.now()
    ctx.wake_at(ctx.now() + 60 * US, TOK_PD)
    if not powered(ctx) then return end
    s.clocks = s.clocks + 1
    if s.clocks == 1 and s.ready then s.reading, s.shift = true, s.data end
    if s.reading and s.clocks <= 24 then
      local bit = (s.shift >> (24 - s.clocks)) & 1
      ctx.drive("DT", bit, "strong", ctx.policy == "functional" and 0 or 100000)
    elseif s.clocks == 25 then
      ctx.drive("DT", 1)
      s.ready = false
    end
  else
    ctx.cancel(TOK_PD)
  end
end

-- the pulses after the 24th set the next conversion's channel and gain
local function settle_gain(ctx)
  local s = ctx.state
  if s.clocks >= 25 then
    local n = math.min(s.clocks, 27)
    local gain = GAIN[n]
    local channel = n == 26 and "B" or "A"
    local changed = gain ~= s.gain or channel ~= s.channel
    s.gain, s.channel = gain, channel
    if changed then schedule(ctx, true) end
  end
  s.clocks, s.reading = 0, false
end

function M.on_time(ctx, token)
  local s = ctx.state
  if token == TOK_PD then
    if s.sck and ctx.now() - s.high_at >= 60 * US then
      s.down, s.ready, s.reading = true, false, false
      ctx.cancel(TOK_READY)
      ctx.drive("DT", 1)
    end
  elseif token == TOK_READY then
    if s.down or s.ready_at ~= ctx.now() then return end
    if s.reading and s.clocks < 25 then
      schedule(ctx, false) -- a read in progress keeps its data
      return
    end
    if s.clocks > 0 then settle_gain(ctx) end
    if s.ready_at ~= ctx.now() then return end -- a gain change restarted settling
    schedule(ctx, false)
    if not powered(ctx) then return end
    s.data, s.ready = convert(ctx), true
    s.conversions = s.conversions + 1
    ctx.drive("DT", 0)
  end
end

function M.on_set(ctx, name, value)
  return name == "load" and type(value) == "number"
end

function M.observe(ctx)
  local s = ctx.state
  local d = s.data
  if d >= 0x800000 then d = d - 0x1000000 end
  return { raw = d, conversions = s.conversions, gain = s.gain, channel = s.channel, powered_down = s.down }
end

return M
