-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- Bosch BME280 humidity, pressure and temperature sensor, and the BMP280,
-- its pressure and temperature sibling (`part`), as their datasheets
-- describe them.
--
-- Shared by: bme280, bmp280
--
--   - I2C at 0x76 + SDO (0x77 with SDO high), register address then data
--     pairs on a write, reads auto-incrementing; SPI (modes 0 and 3 are the
--     part's; mode 0 is decoded) once CSB has gone low, the I2C interface
--     then off until power is removed, bit 7 of the control byte the R/W
--     flag;
--   - the BMP280: chip ID 0x58, no humidity (ctrl_hum, the humidity data and
--     trimming registers absent: written they are ignored, read they are
--     0), standby codes 6 and 7 of 2 s and 4 s (the BME280's 10 and 20 ms);
--   - the register map: chip ID 0x60, soft reset (0xB6 to 0xE0), ctrl_hum
--     (applied by the next ctrl_meas write), status (measuring, im_update
--     for the 2 ms start-up), ctrl_meas, config (ignored in normal mode),
--     the data registers (0x80000 / 0x8000 when a measurement is skipped)
--     and the trimming parameters of a typical part;
--   - sleep, forced (one measurement, then sleep) and normal mode (a
--     measurement every t_measure + t_standby, 0.5 ms to 1 s); t_measure
--     the datasheet's typical 1 + 2 T + (2 P + 0.5) + (2 H + 0.5) ms for
--     the oversampling ratios;
--   - the raw values are those Bosch's compensation formulas turn into the
--     air's temperature, pressure and humidity (the inputs, read at the
--     end of each measurement), at the resolution the oversampling gives
--     (16 + log2 osrs bits), through the IIR filter (coefficients 2 to 16,
--     20 bits) for temperature and pressure;
--   - data registers are shadowed: a measurement ending during a burst read
--     lands when the read ends;
--   - VDD below 1.71 V: no answer.
--
-- Not modelled: the noise the oversampling averages (the readings are
-- exact to the resolution), the accuracy spread (+-1 C, +-1 hPa, +-3 %RH),
-- the humidity response time (1 s) and self-heating, SPI mode 3 and 3-wire
-- SPI, the supply current, damage above 4.25 V.
local M = {}

local US = 1000000
local MS = 1000 * US
local T_STARTUP = 2 * MS
local T_SB = { [0] = 500 * US, 62500 * US, 125 * MS, 250 * MS, 500 * MS, 1000 * MS, 10 * MS, 20 * MS }
local T_SB_BMP = { [6] = 2000 * MS, [7] = 4000 * MS }
local OSRS = { [0] = 0, 1, 2, 4, 8, 16, 16, 16 }
local FILTER = { [0] = 1, 2, 4, 8, 16, 16, 16, 16 }

-- a typical part's trimming parameters (the datasheet's example for T and P)
local CAL = { T1 = 27504, T2 = 26435, T3 = -1000,
  P1 = 36477, P2 = -10685, P3 = 3024, P4 = 2855, P5 = 140, P6 = -7, P7 = 15500, P8 = -14600, P9 = 6000,
  H1 = 75, H2 = 370, H3 = 0, H4 = 309, H5 = 50, H6 = 30 }

local TOK_START, TOK_DONE = 1, 2

-- C's >> on a signed value (arithmetic)
local function asr(x, n) return x // (1 << n) end

-- the datasheet's integer compensation (4.2.3)
local function comp_t(adc)
  local v1 = asr((asr(adc, 3) - (CAL.T1 << 1)) * CAL.T2, 11)
  local d = asr(adc, 4) - CAL.T1
  local v2 = asr(asr(d * d, 12) * CAL.T3, 14)
  local t_fine = v1 + v2
  return asr(t_fine * 5 + 128, 8), t_fine
end

local function comp_p(adc, t_fine)
  local v1 = t_fine - 128000
  local v2 = v1 * v1 * CAL.P6
  v2 = v2 + ((v1 * CAL.P5) << 17)
  v2 = v2 + (CAL.P4 << 35)
  v1 = asr(v1 * v1 * CAL.P3, 8) + ((v1 * CAL.P2) << 12)
  v1 = asr(((1 << 47) + v1) * CAL.P1, 33)
  if v1 == 0 then return 0 end
  local p = 1048576 - adc
  local num = ((p << 31) - v2) * 3125
  p = num // v1
  if (num % v1 ~= 0) and ((num < 0) ~= (v1 < 0)) then p = p + 1 end -- C truncates
  v1 = asr(CAL.P9 * asr(p, 13) * asr(p, 13), 25)
  v2 = asr(CAL.P8 * p, 19)
  return asr(p + v1 + v2, 8) + (CAL.P7 << 4) -- Pa, Q24.8
end

local function comp_h(adc, t_fine)
  local x = t_fine - 76800
  x = asr(((adc << 14) - (CAL.H4 << 20) - (CAL.H5 * x)) + 16384, 15)
    * asr((asr(asr(x * CAL.H6, 10) * (asr(x * CAL.H3, 11) + 32768), 10) + 2097152) * CAL.H2 + 8192, 14)
  x = x - asr(asr(asr(x, 15) * asr(x, 15), 7) * CAL.H1, 4)
  if x < 0 then x = 0 elseif x > 419430400 then x = 419430400 end
  return asr(x, 12) -- %RH, Q22.10
end

-- the raw value in [0, 2^bits) on a grid of `step` whose compensation is
-- nearest `want`; f is monotonic, rising or falling
local function invert(f, want, bits, step)
  local n = (1 << bits) // step
  local lo, hi = 0, n - 1
  local rising = f(hi * step) > f(0)
  while lo < hi do
    local mid = (lo + hi) // 2
    local v = f(mid * step)
    if (v < want) == rising then lo = mid + 1 else hi = mid end
  end
  local best = lo
  if lo > 0 and math.abs(f((lo - 1) * step) - want) <= math.abs(f(lo * step) - want) then best = lo - 1 end
  return best * step
end

local function bmp(ctx) return ctx.params.part == "bmp280" end

local function clamp(x, lo, hi) return x < lo and lo or (x > hi and hi or x) end

local function calibration_bytes(r, humidity)
  local function u16(a, v) v = v & 0xFFFF; r[a], r[a + 1] = v & 0xFF, v >> 8 end
  u16(0x88, CAL.T1); u16(0x8A, CAL.T2); u16(0x8C, CAL.T3)
  u16(0x8E, CAL.P1)
  local p = { CAL.P2, CAL.P3, CAL.P4, CAL.P5, CAL.P6, CAL.P7, CAL.P8, CAL.P9 }
  for i, v in ipairs(p) do u16(0x8E + 2 * i, v) end
  if not humidity then return end
  r[0xA1] = CAL.H1
  u16(0xE1, CAL.H2)
  r[0xE3] = CAL.H3
  r[0xE4] = (CAL.H4 >> 4) & 0xFF
  r[0xE5] = (CAL.H4 & 0x0F) | ((CAL.H5 & 0x0F) << 4)
  r[0xE6] = (CAL.H5 >> 4) & 0xFF
  r[0xE7] = CAL.H6 & 0xFF
end

local function powered(ctx)
  local v = ctx.analog.voltage("VDD")
  return v == nil or v >= 1.71
end

local function soft_reset(ctx)
  local s = ctx.state
  s.reg = {}
  for a = 0, 255 do s.reg[a] = 0 end
  calibration_bytes(s.reg, not bmp(ctx))
  s.reg[0xD0] = bmp(ctx) and 0x58 or 0x60
  s.reg[0xF7], s.reg[0xFA] = 0x80, 0x80
  if not bmp(ctx) then s.reg[0xFD] = 0x80 end
  s.hum_pending = 0
  s.measuring, s.done_at, s.next_at = false, nil, nil
  s.ready_at = ctx.now() + T_STARTUP
  s.filt_t, s.filt_p, s.shadow = nil, nil, nil
end

local function mode(s) return s.reg[0xF4] & 3 end

local function t_measure(s)
  local ot, op, oh = OSRS[s.reg[0xF4] >> 5], OSRS[(s.reg[0xF4] >> 2) & 7], OSRS[s.reg[0xF2] & 7]
  local ms = 1
  if ot > 0 then ms = ms + 2 * ot end
  if op > 0 then ms = ms + 2 * op + 0.5 end
  if oh > 0 then ms = ms + 2 * oh + 0.5 end
  return math.floor(ms * MS + 0.5)
end

local function start(ctx)
  local s = ctx.state
  s.measuring = true
  s.done_at = ctx.now() + t_measure(s)
  ctx.wake_at(s.done_at, TOK_DONE)
end

local function commit(ctx)
  local s = ctx.state
  if not s.shadow or s.burst then return end
  for a, v in pairs(s.shadow) do s.reg[a] = v end
  s.shadow = nil
end

local function finish(ctx)
  local s = ctx.state
  s.measuring = false
  s.measured = s.measured + 1
  local ft, fp, fh = s.reg[0xF4] >> 5, (s.reg[0xF4] >> 2) & 7, s.reg[0xF2] & 7
  local coef = FILTER[(s.reg[0xF5] >> 2) & 7]
  local t = clamp(ctx.input("temperature") or 25, -40, 85)
  local p = clamp(ctx.input("pressure") or 1013.25, 300, 1100)
  local h = clamp(ctx.input("humidity") or 50, 0, 100)
  local out = {}
  local adc_t, adc_p, adc_h = 0x80000, 0x80000, 0x8000
  local t_fine
  if ft > 0 then
    local step = coef > 1 and 1 or (1 << (5 - math.min(ft, 5)))
    adc_t = invert(function(a) return (comp_t(a)) end, math.floor(t * 100 + 0.5), 20, step)
    if coef > 1 then
      s.filt_t = s.filt_t and (s.filt_t * (coef - 1) + adc_t) // coef or adc_t
      adc_t = s.filt_t
    end
    local t100
    t100, t_fine = comp_t(adc_t)
    s.t = t100 / 100
  end
  if fp > 0 and t_fine then
    local step = coef > 1 and 1 or (1 << (5 - math.min(fp, 5)))
    adc_p = invert(function(a) return comp_p(a, t_fine) end, math.floor(p * 100 * 256 + 0.5), 20, step)
    if coef > 1 then
      s.filt_p = s.filt_p and (s.filt_p * (coef - 1) + adc_p) // coef or adc_p
      adc_p = s.filt_p
    end
    s.p = comp_p(adc_p, t_fine) / 25600
  end
  if fh > 0 and t_fine then
    adc_h = invert(function(a) return comp_h(a, t_fine) end, math.floor(h * 1024 + 0.5), 16, 1)
    s.h = comp_h(adc_h, t_fine) / 1024
  end
  out[0xF7], out[0xF8], out[0xF9] = adc_p >> 12, (adc_p >> 4) & 0xFF, (adc_p & 0xF) << 4
  out[0xFA], out[0xFB], out[0xFC] = adc_t >> 12, (adc_t >> 4) & 0xFF, (adc_t & 0xF) << 4
  if not bmp(ctx) then out[0xFD], out[0xFE] = adc_h >> 8, adc_h & 0xFF end
  s.shadow = out
  commit(ctx)
  local m = mode(s)
  if m == 1 or m == 2 then
    s.reg[0xF4] = s.reg[0xF4] & 0xFC -- forced: back to sleep
  elseif m == 3 then
    local sb = s.reg[0xF5] >> 5
    s.next_at = ctx.now() + (bmp(ctx) and T_SB_BMP[sb] or T_SB[sb])
    ctx.wake_at(s.next_at, TOK_START)
  end
end

local function read_reg(ctx, a)
  local s = ctx.state
  if a == 0xF3 then
    return (s.measuring and 0x08 or 0) | (ctx.now() < s.ready_at and 0x01 or 0)
  end
  if a == 0xE0 then return 0 end
  return s.reg[a] or 0
end

local function write_reg(ctx, a, v)
  local s = ctx.state
  if a == 0xE0 then
    if v == 0xB6 then soft_reset(ctx) end
  elseif a == 0xF2 then
    if not bmp(ctx) then s.hum_pending = v & 7 end
  elseif a == 0xF4 then
    local was = mode(s)
    s.reg[0xF2] = s.hum_pending
    s.reg[0xF4] = v
    local m = v & 3
    if m ~= 0 and not (was == 3 and m == 3) then
      start(ctx)
    elseif m == 0 then
      s.measuring = false
    end
  elseif a == 0xF5 then
    if mode(s) ~= 3 then s.reg[0xF5] = v & 0xFD end -- spi3w_en is not decoded
  end
end

function M.create(ctx)
  local s = ctx.state
  ctx.bus.i2c_slave(0x76, {
    start = function(ctx, dir)
      if s.spi or not powered(ctx) then return false end
      if dir == "write" then s.wstate = "addr" else s.burst = true end
      return true
    end,
    write = function(ctx, b)
      if s.wstate == "addr" then
        s.ptr, s.wstate = b, "data"
      else
        write_reg(ctx, s.ptr, b)
        s.wstate = "addr"
      end
    end,
    read = function(ctx)
      local v = read_reg(ctx, s.ptr)
      s.ptr = (s.ptr + 1) & 0xFF
      return v
    end,
    stop = function(ctx) s.burst = false; commit(ctx) end,
  }, { sda = "SDI", scl = "SCK", address_pins = { "SDO" } })
  ctx.bus.spi_slave({
    select = function(ctx)
      s.spi, s.sstate = true, "ctrl"
      return 0
    end,
    transfer = function(ctx, b)
      if not powered(ctx) then return 0xFF end
      if s.sstate == "ctrl" then
        s.ptr = b | 0x80
        if b & 0x80 ~= 0 then
          s.sstate, s.burst = "read", true
          local v = read_reg(ctx, s.ptr)
          s.ptr = (s.ptr + 1) & 0xFF
          return v
        end
        s.sstate = "data"
        return 0
      elseif s.sstate == "data" then
        write_reg(ctx, s.ptr, b)
        s.sstate = "ctrl"
        return 0
      end
      local v = read_reg(ctx, s.ptr)
      s.ptr = (s.ptr + 1) & 0xFF
      return v
    end,
    deselect = function(ctx) s.burst = false; commit(ctx) end,
  }, { sck = "SCK", mosi = "SDI", miso = "SDO", cs = "CSB", mode = 0 })
end

function M.reset(ctx)
  local s = ctx.state
  s.spi, s.burst, s.measured = false, false, 0
  s.t, s.p, s.h = 0, 0, 0
  soft_reset(ctx)
end

function M.on_set(ctx, name)
  return name == "temperature" or name == "pressure" or (name == "humidity" and not bmp(ctx))
end

function M.on_time(ctx, token)
  local s = ctx.state
  -- a wake-up left over from a measurement a write cancelled or restarted
  -- finds its time not yet come, or gone
  if token == TOK_START then
    if s.next_at == ctx.now() and mode(s) == 3 and not s.measuring then start(ctx) end
  elseif s.measuring and s.done_at == ctx.now() then
    finish(ctx)
  end
end

function M.observe(ctx)
  local s = ctx.state
  local names = { [0] = "sleep", "forced", "forced", "normal" }
  return { temperature = s.t, pressure = s.p, humidity = not bmp(ctx) and s.h or nil, measurements = s.measured, mode = names[mode(s)],
    interface = s.spi and "spi" or "i2c" }
end

return M
