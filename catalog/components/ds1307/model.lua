-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- Maxim DS1307 64 x 8 serial I2C real-time clock with its 32.768 kHz
-- crystal, as its data sheet (19-5215) describes it.
--
--   - I2C at 0x68 (standard mode), a register address then data,
--     auto-incrementing through the 64 registers and wrapping from 3Fh to
--     00h; reads of the time come from secondary (user) buffers refreshed
--     at each START and when the pointer wraps to 00h, so a burst read
--     never tears across a second;
--   - 00h-06h, BCD: seconds with the clock-halt bit CH (bit 7: set, the
--     oscillator is stopped and the time stands still), minutes, hours
--     (24-hour, or 12-hour with AM/PM when bit 6 is written set), the day
--     of the week (1 to 7, whatever the user calls day 1, advancing at
--     midnight), date, month, year 00-99 with leap years to 2100;
--   - writing the seconds register resets the countdown chain: the next
--     second comes a full second later; the time runs at the crystal's
--     rate, `drift_ppm` off true;
--   - 07h control: SQWE with RS1:RS0 gives 1 Hz, 4.096, 8.192 or
--     32.768 kHz on SQW/OUT (open drain); with SQWE clear the pin holds OUT;
--     generated only while something is wired to it;
--   - 08h-3Fh: 56 bytes of RAM, kept by the battery;
--   - power-on (no battery: a new part): CH set, time 00:00:00 01/01/00,
--     day 1, control 03h (OUT 0, SQWE 0, RS 11), the RAM zeros; the data
--     sheet leaves the registers undefined at first power-up and tells the
--     user to clear CH; `start` gives the time a battery kept running;
--   - below VPF (1.25 x the 3 V battery, 3.75 V) on VCC it does not answer.
--
-- Not modelled: the battery switchover and timekeeping on battery (VBAT
-- is not read), the crystal's frequency against temperature and load
-- capacitance (the drift is a parameter), supply current.
local M = {}

local PS = 1000000000000
local TOK_SQW = 1
local NREG = 64

local function bcd(n) return ((n // 10) << 4) | (n % 10) end
local function unbcd(b) return ((b >> 4) & 15) * 10 + (b & 15) end

-- days from 2000-01-01 and back (proleptic Gregorian, after H. Hinnant)
local function days(y, m, d)
  y = m <= 2 and y - 1 or y
  local era = y // 400
  local yoe = y - era * 400
  local doy = (153 * (m + (m > 2 and -3 or 9)) + 2) // 5 + d - 1
  return era * 146097 + yoe * 365 + yoe // 4 - yoe // 100 + doy - 730425
end
local function civil(z)
  z = z + 730425
  local era = z // 146097
  local doe = z - era * 146097
  local yoe = (doe - doe // 1460 + doe // 36524 - doe // 146096) // 365
  local doy = doe - (365 * yoe + yoe // 4 - yoe // 100)
  local mp = (5 * doy + 2) // 153
  local d = doy - (153 * mp + 2) // 5 + 1
  local m = mp + (mp < 10 and 3 or -9)
  local y = yoe + era * 400
  return m <= 2 and y + 1 or y, m, d
end

local function powered(ctx)
  local v = ctx.analog.voltage("VCC")
  return v == nil or v >= 3.75
end

-- seconds since 2000-01-01 00:00:00 now (the time stands while halted)
local function seconds(ctx)
  local s = ctx.state
  if s.halted then return s.t0 end
  return s.t0 + (ctx.now() - s.base) // s.period
end

local function dow_at(s, t)
  return (s.dow - 1 + (t // 86400 - s.dow_day)) % 7 + 1
end

local function time_regs(s, t)
  local day, sec = t // 86400, t % 86400
  local y, m, d = civil(day)
  local h = sec // 3600
  local hr
  if s.mode12 then
    local h12 = h % 12 == 0 and 12 or h % 12
    hr = 0x40 | (h >= 12 and 0x20 or 0) | bcd(h12)
  else
    hr = bcd(h)
  end
  return { [0] = (s.halted and 0x80 or 0) | bcd(sec % 60), bcd((sec // 60) % 60), hr, dow_at(s, t), bcd(d), bcd(m), bcd(y % 100) }
end

local function snapshot(ctx)
  local s = ctx.state
  s.buf = time_regs(s, seconds(ctx))
end

local function write_time(ctx, a, v)
  local s = ctx.state
  local cur = seconds(ctx)
  local r = time_regs(s, cur)
  r[a] = v
  local y = 2000 + unbcd(r[6])
  local h
  if (r[2] & 0x40) ~= 0 then
    h = unbcd(r[2] & 0x1F) % 12 + ((r[2] & 0x20) ~= 0 and 12 or 0)
  else
    h = unbcd(r[2] & 0x3F)
  end
  if a == 2 then s.mode12 = (v & 0x40) ~= 0 end
  local day = days(y, math.max(1, math.min(12, unbcd(r[5] & 0x1F))), math.max(1, unbcd(r[4] & 0x3F)))
  local t = day * 86400 + h * 3600 + unbcd(r[1] & 0x7F) * 60 + unbcd(r[0] & 0x7F)
  if a == 3 then
    s.dow, s.dow_day = math.max(1, math.min(7, v & 7)), t // 86400
  else
    s.dow, s.dow_day = dow_at(s, cur), cur // 86400
    s.dow_day = s.dow_day + (t // 86400 - cur // 86400)
  end
  if a == 0 then
    -- the countdown chain restarts; CH starts or stops the oscillator
    s.halted = (v & 0x80) ~= 0
    s.t0, s.base = t, ctx.now()
  elseif s.halted then
    s.t0 = t
  else
    s.t0 = t - (ctx.now() - s.base) // s.period
  end
end

local function sqw(ctx)
  local s = ctx.state
  s.sqw_gen = (s.sqw_gen or 0) + 1
  local ctrl = s.reg[7]
  if (ctrl & 0x10) == 0 then
    -- SQWE clear: the pin is OUT's level (open drain: 1 releases it)
    if (ctrl & 0x80) ~= 0 then ctx.release("SQW") else ctx.drive("SQW", 0) end
    return
  end
  if not ctx.pins.SQW.connected or s.halted then
    ctx.release("SQW")
    return
  end
  local hz = ({ [0] = 1, 4096, 8192, 32768 })[ctrl & 3]
  s.sqw_half, s.sqw_level, s.sqw_tag = PS // (2 * hz), 1, s.sqw_gen
  ctx.release("SQW")
  ctx.wake_at(ctx.now() + s.sqw_half, TOK_SQW)
end

local function power_on(ctx)
  local s = ctx.state
  s.reg = {}
  for a = 0, NREG - 1 do s.reg[a] = 0 end
  s.reg[7] = 0x03
  s.period = math.floor(PS / (1 + (ctx.params.drift_ppm or 0) * 1e-6) + 0.5)
  local kept = ctx.params.start ~= nil and ctx.params.start ~= ""
  local y, mo, d, h, mi, se = string.match(kept and ctx.params.start or "2000-01-01 00:00:00",
    "^(%d+)-(%d+)-(%d+)[ T](%d+):(%d+):(%d+)$")
  if not y then error("start must look like 2026-01-01 00:00:00") end
  y, mo, d = tonumber(y), tonumber(mo), tonumber(d)
  if y < 2000 or y > 2099 or mo < 1 or mo > 12 or d < 1 or d > 31 then
    error("start: the DS1307 counts from 2000 to 2099")
  end
  local t = days(y, mo, d) * 86400 + tonumber(h) * 3600 + tonumber(mi) * 60 + tonumber(se)
  s.t0, s.base, s.mode12, s.dow, s.dow_day = t, ctx.now(), false, 1, t // 86400
  s.halted = not kept -- a new part's oscillator is stopped until CH is cleared
  s.buf = time_regs(s, t)
  s.ptr = 0
  sqw(ctx)
end

local function read_reg(ctx, a)
  local s = ctx.state
  if a <= 6 then return s.buf[a] end
  return s.reg[a] or 0
end

local function write_reg(ctx, a, v)
  local s = ctx.state
  if a <= 6 then
    write_time(ctx, a, v)
    snapshot(ctx)
    if a == 0 then sqw(ctx) end
  elseif a == 7 then
    s.reg[7] = v & 0x93 -- bits 6, 5, 3 and 2 read 0
    sqw(ctx)
  else
    s.reg[a] = v
  end
end

function M.create(ctx)
  local s = ctx.state
  ctx.bus.i2c_slave(0x68, {
    start = function(ctx, dir)
      if not powered(ctx) then return false end
      snapshot(ctx)
      if dir == "write" then s.first = true end
      return true
    end,
    write = function(ctx, b)
      if s.first then
        s.ptr, s.first = b % NREG, false
      else
        write_reg(ctx, s.ptr, b)
        s.ptr = (s.ptr + 1) % NREG
      end
    end,
    read = function(ctx)
      local v = read_reg(ctx, s.ptr)
      s.ptr = (s.ptr + 1) % NREG
      if s.ptr == 0 then snapshot(ctx) end
      return v
    end,
  })
end

function M.reset(ctx)
  power_on(ctx)
end

function M.on_time(ctx, token)
  local s = ctx.state
  if token == TOK_SQW then
    if s.sqw_tag ~= s.sqw_gen or (s.reg[7] & 0x10) == 0 then return end
    s.sqw_level = 1 - s.sqw_level
    if s.sqw_level == 0 then ctx.drive("SQW", 0) else ctx.release("SQW") end
    ctx.wake_at(ctx.now() + s.sqw_half, TOK_SQW)
  end
end

function M.observe(ctx)
  local s = ctx.state
  local t = seconds(ctx)
  local y, m, d = civil(t // 86400)
  local sec = t % 86400
  return {
    time = string.format("%04d-%02d-%02d %02d:%02d:%02d", y, m, d, sec // 3600, (sec // 60) % 60, sec % 60),
    weekday = dow_at(s, t),
    running = not s.halted,
  }
end

return M
