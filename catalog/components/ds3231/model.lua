-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- Maxim DS3231 extremely accurate I2C real-time clock with its integrated
-- temperature-compensated crystal, as its datasheet describes it.
--
--   - I2C at 0x68, register address then data, auto-incrementing; reads of
--     the time come from user buffers synchronised at each START (and when
--     the pointer wraps to 0), so a burst read never tears across a second;
--   - timekeeping in BCD: seconds, minutes, hours (24-hour, or 12-hour
--     with AM/PM when bit 6 is written set), the day of the week (1 to 7,
--     whatever the user calls day 1, advancing at midnight), date, month
--     with the century bit toggling when the year rolls from 99, year;
--     leap years through 2099 (2100 too: the century bit carries on);
--   - writing the seconds register resets the countdown chain: the next
--     second comes a full second later; the time runs at the crystal's
--     rate, `drift_ppm` off true plus the aging offset's 0.1 ppm per LSB
--     (positive slows it);
--   - two alarms with every mask combination of the datasheet's tables
--     (once a second; seconds; minutes and seconds; hours too; date or
--     day too; alarm 2 a minute level): a match sets A1F / A2F, and with
--     INTCN and the alarm's enable pulls INT#/SQW low until the flag is
--     cleared;
--   - INTCN clear: INT#/SQW (open drain) gives the square wave RS2:RS1
--     select (1 Hz, 1.024, 4.096, 8.192 kHz); 32kHz (open drain) gives
--     32.768 kHz while EN32kHz is set; both are generated only while
--     something is wired to them;
--   - the temperature: converted at power-up and every 64 s, or when CONV
--     is set (BSY and CONV set for the 125 ms conversion), in 0.25 C
--     steps, two's complement;
--   - power-on: time 00:00:00, 01/01/00 (or `start`, the time a clock kept
--     by its battery shows), day 1; control 0x1C, status 0x88 (OSF set: the
--     oscillator was stopped; EN32kHz); RST held low 250 ms;
--     below 2.3 V on VCC no answer.
--
-- Not modelled: the battery (VBAT) switchover and timekeeping on battery
-- (EOSC, BBSQW and OSF on power loss have no effect while on VCC), the RST
-- pushbutton input and its debounce, the TCXO's frequency against
-- temperature (the drift is a parameter), supply current.
local M = {}

local PS = 1000000000000
local MS = 1000000000
local TOK_TICK, TOK_SQW, TOK_32K, TOK_CONV, TOK_RST, TOK_ALARM1, TOK_ALARM2 = 1, 2, 3, 4, 5, 6, 7

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
  return v == nil or v >= 2.3
end

-- the length of one of the oscillator's seconds, in ps
local function period(ctx)
  local s = ctx.state
  local aging = s.reg[0x10] >= 128 and s.reg[0x10] - 256 or s.reg[0x10]
  local ppm = (ctx.params.drift_ppm or 0) - 0.1 * aging
  return math.floor(PS / (1 + ppm * 1e-6) + 0.5)
end

-- seconds since 2000-01-01 00:00:00 now
local function seconds(ctx)
  local s = ctx.state
  return s.t0 + (ctx.now() - s.base) // s.period
end

local function next_second_at(ctx)
  local s = ctx.state
  return s.base + ((ctx.now() - s.base) // s.period + 1) * s.period
end

local function dow_at(s, t)
  return (s.dow - 1 + (t // 86400 - s.dow_day)) % 7 + 1
end

-- the time registers for second t
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
  local century = (y >= 2100) and 0x80 or 0
  return { [0] = bcd(sec % 60), bcd((sec // 60) % 60), hr, dow_at(s, t), bcd(d), century | bcd(m), bcd(y % 100) }
end

local function snapshot(ctx)
  local s = ctx.state
  s.buf = time_regs(s, seconds(ctx))
end

-- the time a write to time register a with value v leaves
local function write_time(ctx, a, v)
  local s = ctx.state
  local cur = seconds(ctx)
  local r = time_regs(s, cur)
  r[a] = v
  local y = 2000 + unbcd(r[6]) + ((r[5] & 0x80) ~= 0 and 100 or 0)
  local h
  if (r[2] & 0x40) ~= 0 then
    local h12 = unbcd(r[2] & 0x1F) % 12
    h = h12 + ((r[2] & 0x20) ~= 0 and 12 or 0)
  else
    h = unbcd(r[2] & 0x3F)
  end
  if a == 2 then s.mode12 = (v & 0x40) ~= 0 end
  local day = days(y, math.max(1, unbcd(r[5] & 0x1F)), math.max(1, unbcd(r[4] & 0x3F)))
  local t = day * 86400 + h * 3600 + unbcd(r[1] & 0x7F) * 60 + unbcd(r[0] & 0x7F)
  if a == 3 then
    s.dow, s.dow_day = math.max(1, math.min(7, v & 7)), t // 86400
  else
    s.dow, s.dow_day = dow_at(s, cur), cur // 86400 -- the day of the week keeps counting
    s.dow_day = s.dow_day + (t // 86400 - cur // 86400)
  end
  if a == 0 then
    s.t0, s.base = t, ctx.now() -- the countdown chain restarts
  else
    s.t0 = t - (ctx.now() - s.base) // s.period
  end
end

local function int_level(ctx)
  local s = ctx.state
  local ctrl, st = s.reg[0x0E], s.reg[0x0F]
  if (ctrl & 0x04) == 0 then return nil end -- square wave mode
  local on = ((ctrl & 1) ~= 0 and (st & 1) ~= 0) or ((ctrl & 2) ~= 0 and (st & 2) ~= 0)
  return on and 0 or 1
end

local function drive_int(ctx)
  local l = int_level(ctx)
  if l == nil then return end
  if l == 0 then ctx.drive("INT_SQW", 0) else ctx.release("INT_SQW") end
end

-- alarm n's next match after second t, or nil: the fields whose mask bit
-- is clear must equal the alarm's (alarm 2 has no seconds: it matches at 00)
local function next_match(ctx, n, t)
  local s = ctx.state
  local r = s.reg
  local b = n == 1 and 0x07 or 0x0B
  local f = n == 1 and { r[b], r[b + 1], r[b + 2], r[b + 3] } or { 0x00, r[b], r[b + 1], r[b + 2] }
  local any_sec = n == 1 and (f[1] & 0x80) ~= 0
  local S = unbcd(f[1] & 0x7F)
  local cmp_min, M = (f[2] & 0x80) == 0, unbcd(f[2] & 0x7F)
  local cmp_hr = (f[3] & 0x80) == 0
  local H
  if (f[3] & 0x40) ~= 0 then H = unbcd(f[3] & 0x1F) % 12 + ((f[3] & 0x20) ~= 0 and 12 or 0) else H = unbcd(f[3] & 0x3F) end
  local cmp_day, by_dow, D = (f[4] & 0x80) == 0, (f[4] & 0x40) ~= 0, unbcd(f[4] & 0x3F)
  if by_dow then D = f[4] & 0x0F end
  local function low_fields(base) -- the second, minute and hour the alarm wants from `base` on
    return base + (cmp_hr and H * 3600 or 0) + (cmp_min and M * 60 or 0) + (any_sec and 0 or S)
  end
  local u = t + 1
  if not any_sec then u = u + (S - u % 60) % 60 end
  for _ = 1, 4000 do
    local day, sod = u // 86400, u % 86400
    local hh, mm = sod // 3600, (sod // 60) % 60
    local _, _, date = civil(day)
    if cmp_day and ((by_dow and dow_at(s, u) ~= D) or (not by_dow and date ~= D)) then
      u = low_fields((day + 1) * 86400)
    elseif cmp_hr and hh ~= H then
      local nu = day * 86400 + H * 3600 + (cmp_min and M * 60 or 0) + (any_sec and 0 or S)
      u = nu > u and nu or nu + 86400
    elseif cmp_min and mm ~= M then
      local nu = day * 86400 + hh * 3600 + M * 60 + (any_sec and 0 or S)
      u = nu > u and nu or nu + 3600
    else
      return u
    end
  end
  return nil
end

local function arm_alarm(ctx, n)
  local s = ctx.state
  local u = next_match(ctx, n, seconds(ctx))
  s["alarm" .. n] = nil
  if u then
    local at = s.base + (u - s.t0) * s.period
    s["alarm" .. n] = at
    ctx.wake_at(at, n == 1 and TOK_ALARM1 or TOK_ALARM2)
  end
end

local function square(ctx)
  local s = ctx.state
  s.sqw_gen = (s.sqw_gen or 0) + 1
  if (s.reg[0x0E] & 0x04) ~= 0 then drive_int(ctx) return end
  if not ctx.pins.INT_SQW.connected then return end
  local hz = ({ [0] = 1, 1024, 4096, 8192 })[(s.reg[0x0E] >> 3) & 3]
  s.sqw_half, s.sqw_level, s.sqw_tag = PS // (2 * hz), 1, s.sqw_gen
  ctx.release("INT_SQW")
  ctx.wake_at(ctx.now() + s.sqw_half, TOK_SQW)
end

local function osc32k(ctx)
  local s = ctx.state
  s.k32_gen = (s.k32_gen or 0) + 1
  if (s.reg[0x0F] & 0x08) == 0 or not ctx.pins["32KHZ"].connected then
    ctx.release("32KHZ")
    return
  end
  s.k32_level, s.k32_tag = 1, s.k32_gen
  ctx.release("32KHZ")
  ctx.wake_at(ctx.now() + PS // 65536, TOK_32K)
end

local function convert(ctx)
  local s = ctx.state
  s.reg[0x0F] = s.reg[0x0F] | 0x04 -- BSY
  s.conv_at = ctx.now() + 125 * MS
  ctx.wake_at(s.conv_at, TOK_CONV)
end

local function power_on(ctx)
  local s = ctx.state
  s.reg = {}
  for a = 0, 0x12 do s.reg[a] = 0 end
  s.reg[0x0E], s.reg[0x0F] = 0x1C, 0x88
  s.period = period(ctx)
  local y, mo, d, h, mi, se = string.match(ctx.params.start or "2000-01-01 00:00:00",
    "^(%d+)-(%d+)-(%d+)[ T](%d+):(%d+):(%d+)$")
  if not y then error("start must look like 2026-01-01 00:00:00") end
  y, mo, d = tonumber(y), tonumber(mo), tonumber(d)
  if y < 2000 or y > 2199 or mo < 1 or mo > 12 or d < 1 or d > 31 then
    error("start: the DS3231 counts from 2000 to 2199")
  end
  local t = days(y, mo, d) * 86400 + tonumber(h) * 3600 + tonumber(mi) * 60 + tonumber(se)
  s.t0, s.base, s.mode12, s.dow, s.dow_day = t, ctx.now(), false, 1, t // 86400
  s.buf = time_regs(s, t)
  s.ptr = 0
  convert(ctx)
  s.next_auto = ctx.now() + 64 * PS
  ctx.wake_at(s.next_auto, TOK_TICK)
  ctx.drive("RST", 0) -- held low after power-up
  ctx.wake_at(ctx.now() + 250 * MS, TOK_RST)
  square(ctx)
  osc32k(ctx)
end

local function read_reg(ctx, a)
  local s = ctx.state
  if a <= 6 then return s.buf[a] end
  if a == 0x12 then return s.reg[a] & 0xC0 end
  return s.reg[a] or 0
end

local function write_reg(ctx, a, v)
  local s = ctx.state
  if a <= 6 then
    write_time(ctx, a, v)
    snapshot(ctx)
    arm_alarm(ctx, 1); arm_alarm(ctx, 2)
  elseif a <= 0x0D then
    s.reg[a] = v
    arm_alarm(ctx, a <= 0x0A and 1 or 2)
  elseif a == 0x0E then
    local conv = (v & 0x20) ~= 0 and (s.reg[0x0F] & 0x04) == 0
    s.reg[a] = (v & 0xDF) | (s.reg[a] & 0x20)
    if conv then s.reg[a] = s.reg[a] | 0x20; convert(ctx) end
    square(ctx)
    drive_int(ctx)
  elseif a == 0x0F then
    -- OSF, A2F and A1F clear when written 0 (a 1 leaves them); EN32kHz is
    -- writable; BSY is read-only
    local old = s.reg[a]
    s.reg[a] = (old & 0x04) | (old & v & 0x83) | (v & 0x08)
    osc32k(ctx)
    drive_int(ctx)
  elseif a == 0x10 then
    -- the rate changes from here on; the second under way keeps its start
    local t = seconds(ctx)
    local frac = (ctx.now() - s.base) % s.period
    s.reg[a] = v
    s.period = period(ctx)
    s.t0, s.base = t, ctx.now() - frac
    arm_alarm(ctx, 1); arm_alarm(ctx, 2)
  end
end

function M.create(ctx)
  local s = ctx.state
  ctx.bus.i2c_slave(0x68, {
    start = function(ctx, dir)
      if not powered(ctx) then return false end
      snapshot(ctx) -- the user buffers follow the time at each START
      if dir == "write" then s.first = true end
      return true
    end,
    write = function(ctx, b)
      if s.first then
        s.ptr, s.first = b % 0x13, false
      else
        write_reg(ctx, s.ptr, b)
        s.ptr = (s.ptr + 1) % 0x13
      end
    end,
    read = function(ctx)
      local v = read_reg(ctx, s.ptr)
      s.ptr = (s.ptr + 1) % 0x13
      if s.ptr == 0 then snapshot(ctx) end
      return v
    end,
  })
end

function M.reset(ctx)
  power_on(ctx)
end

function M.on_set(ctx, name) return name == "temperature" end

function M.on_time(ctx, token)
  local s = ctx.state
  local now = ctx.now()
  if token == TOK_TICK then
    if now ~= s.next_auto then return end
    convert(ctx)
    s.next_auto = now + 64 * PS
    ctx.wake_at(s.next_auto, TOK_TICK)
  elseif token == TOK_CONV then
    if now ~= s.conv_at then return end
    local t = ctx.input("temperature") or 25
    t = math.max(-40, math.min(85, t))
    local q = math.floor(t * 4 + 0.5) & 0x3FF -- 10 bits, two's complement
    s.reg[0x11], s.reg[0x12] = (q >> 2) & 0xFF, (q & 3) << 6
    s.reg[0x0F] = s.reg[0x0F] & ~0x04
    s.reg[0x0E] = s.reg[0x0E] & ~0x20
  elseif token == TOK_RST then
    ctx.release("RST")
  elseif token == TOK_ALARM1 or token == TOK_ALARM2 then
    local n = token == TOK_ALARM1 and 1 or 2
    if s["alarm" .. n] ~= now then return end
    s.reg[0x0F] = s.reg[0x0F] | (n == 1 and 1 or 2)
    drive_int(ctx)
    arm_alarm(ctx, n) -- the next match
  elseif token == TOK_SQW then
    if s.sqw_tag ~= s.sqw_gen or (s.reg[0x0E] & 0x04) ~= 0 then return end
    s.sqw_level = 1 - s.sqw_level
    if s.sqw_level == 0 then ctx.drive("INT_SQW", 0) else ctx.release("INT_SQW") end
    ctx.wake_at(now + s.sqw_half, TOK_SQW)
  elseif token == TOK_32K then
    if s.k32_tag ~= s.k32_gen then return end
    s.k32_level = 1 - s.k32_level
    if s.k32_level == 0 then ctx.drive("32KHZ", 0) else ctx.release("32KHZ") end
    ctx.wake_at(now + PS // 65536, TOK_32K)
  end
end

function M.observe(ctx)
  local s = ctx.state
  local t = seconds(ctx)
  local y, m, d = civil(t // 86400)
  local sec = t % 86400
  local tr = (s.reg[0x11] << 2) | (s.reg[0x12] >> 6)
  if tr >= 512 then tr = tr - 1024 end
  return {
    time = string.format("%04d-%02d-%02d %02d:%02d:%02d", y, m, d, sec // 3600, (sec // 60) % 60, sec % 60),
    weekday = dow_at(s, t), temperature = tr / 4,
    lost_power = (s.reg[0x0F] & 0x80) ~= 0,
    alarm1 = (s.reg[0x0F] & 1) ~= 0, alarm2 = (s.reg[0x0F] & 2) ~= 0,
  }
end

return M
