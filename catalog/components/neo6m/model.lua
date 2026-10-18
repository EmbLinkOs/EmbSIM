-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- u-blox NEO-6M GPS module (as on the common GY-NEO6MV2 board), as
-- u-blox's NEO-6 data sheet and the u-blox 6 receiver description describe
-- its NMEA output.
--
--   - TXD sends NMEA 0183 at `baud` (9600, the default; another rate as
--     u-center would have saved it), 8N1, idle high: once a second, from
--     the top of the second, RMC, VTG, GGA, GSA, the GSV group and GLL,
--     each with its *checksum and CR LF, in that order, as the NEO-6 does
--     by default;
--   - without a fix the sentences carry empty fields (RMC status V, GGA
--     quality 0 with HDOP 99.99, GSA mode 1), as a module indoors prints;
--   - the first fix comes the data sheet's time to first fix after power-up
--     (`start`: cold or warm 27 s, hot 1 s) while the antenna sees the sky
--     (the `sky` input); without it, or with fewer than four satellites,
--     there is no fix and an existing one is lost (regained after the data
--     sheet's 1 s reacquisition);
--   - with a fix: UTC time and date (counting from `utc_start`), latitude
--     and longitude in ddmm.mmmmm, speed in knots and km/h, course, the fix
--     quality, satellites used, HDOP and the altitude above mean sea level
--     with the geoid separation; GSV lists the satellites in view with
--     elevation, azimuth and signal strength;
--   - TIMEPULSE: 1 Hz, 100 ms high from the top of each second, only while
--     fixed (the board's LED blinks then);
--   - below 2.7 V on VCC (the board's regulator input: 3 to 5 V) it sends
--     nothing.
--
-- Not modelled: UBX binary messages and configuration commands on RXD (the
-- rate and messages stay as set), the start-up $GPTXT banner, the
-- position's noise and accuracy (2.5 m CEP), time known before the fix,
-- the satellites' real orbits (the sky in view is a fixed, plausible set),
-- the backup battery's effect on the start (`start` sets it).
local M = {}

local PS = 1000000000000
local MS = 1000000000
local TOK_SECOND, TOK_BIT, TOK_PPS_LOW = 1, 2, 3
local TTFF = { cold = 27, warm = 27, hot = 1 }

local function powered(ctx)
  local v = ctx.analog.voltage("VCC")
  return v == nil or v >= 2.7
end

local function input(ctx, name, def)
  local v = ctx.input(name)
  if v == nil then return def end
  return v
end

local function checksum(body)
  local c = 0
  for i = 1, #body do c = c ~ body:byte(i) end
  return string.format("$%s*%02X\r\n", body, c)
end

-- days from 1970-01-01 and back (after H. Hinnant)
local function days(y, m, d)
  y = m <= 2 and y - 1 or y
  local era = y // 400
  local yoe = y - era * 400
  local doy = (153 * (m + (m > 2 and -3 or 9)) + 2) // 5 + d - 1
  return era * 146097 + yoe * 365 + yoe // 4 - yoe // 100 + doy - 719468
end
local function civil(z)
  z = z + 719468
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

local function epoch(iso)
  local y, mo, d, h, mi, s = tostring(iso):match("^(%d+)-(%d+)-(%d+)T(%d+):(%d+):(%d+)")
  if not y then return 0 end
  return days(tonumber(y), tonumber(mo), tonumber(d)) * 86400 + tonumber(h) * 3600 + tonumber(mi) * 60 + tonumber(s)
end

local function coord(v, width)
  local a = math.abs(v)
  local deg = math.floor(a)
  local min = (a - deg) * 60
  if string.format("%08.5f", min) == "60.00000" then deg, min = deg + 1, 0 end
  return string.format("%0" .. width .. "d%08.5f", deg, min)
end

-- a plausible sky: PRN, elevation, azimuth, SNR for each satellite in view
local SKY = {
  { 2, 62, 112, 41 }, { 5, 48, 287, 38 }, { 7, 33, 51, 36 }, { 9, 71, 203, 44 }, { 13, 25, 318, 31 },
  { 16, 17, 146, 29 }, { 20, 54, 9, 40 }, { 23, 39, 241, 35 }, { 26, 12, 77, 26 }, { 29, 66, 168, 42 },
  { 30, 21, 265, 30 }, { 31, 8, 333, 22 },
}

local function sentences(ctx, t)
  local s = ctx.state
  local fixed = s.fixed
  local day, sod = t // 86400, t % 86400
  local y, mo, d = civil(day)
  local time = string.format("%02d%02d%02d.00", sod // 3600, (sod // 60) % 60, sod % 60)
  local date = string.format("%02d%02d%02d", d, mo, y % 100)
  local nview = math.max(0, math.min(#SKY, math.floor(input(ctx, "satellites", 8))))
  if not input(ctx, "sky", true) then nview = 0 end
  local out = {}
  if fixed then
    local lat, lon = input(ctx, "latitude", 0), input(ctx, "longitude", 0)
    local la = coord(lat, 2) .. "," .. (lat < 0 and "S" or "N")
    local lo = coord(lon, 3) .. "," .. (lon < 0 and "W" or "E")
    local kmh = math.max(0, input(ctx, "speed", 0))
    local kn = kmh / 1.852
    local course = kmh >= 0.5 and string.format("%.2f", input(ctx, "course", 0) % 360) or ""
    local used = math.min(nview, 12)
    local hdop = used >= 8 and 0.92 or (used >= 6 and 1.21 or (used >= 4 and 2.05 or 99.99))
    local alt = input(ctx, "altitude", 0)
    out[#out + 1] = string.format("GPRMC,%s,A,%s,%s,%.3f,%s,%s,,,A", time, la, lo, kn, course, date)
    out[#out + 1] = string.format("GPVTG,%s,T,,M,%.3f,N,%.3f,K,A", course, kn, kmh)
    out[#out + 1] = string.format("GPGGA,%s,%s,%s,1,%02d,%.2f,%.1f,M,%.1f,M,,", time, la, lo, used, hdop, alt, ctx.params.geoid_separation)
    local prns = {}
    for i = 1, used do prns[i] = string.format("%02d", SKY[i][1]) end
    for i = used + 1, 12 do prns[i] = "" end
    out[#out + 1] = string.format("GPGSA,A,3,%s,%.2f,%.2f,%.2f", table.concat(prns, ","), hdop * 1.6, hdop, hdop * 1.3)
  else
    out[#out + 1] = "GPRMC,,V,,,,,,,,,,N"
    out[#out + 1] = "GPVTG,,,,,,,,,N"
    out[#out + 1] = "GPGGA,,,,,,0,00,99.99,,,,,,"
    out[#out + 1] = "GPGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99"
  end
  local msgs = math.max(1, (nview + 3) // 4)
  for m = 1, msgs do
    local parts = { string.format("GPGSV,%d,%d,%02d", msgs, m, nview) }
    for i = (m - 1) * 4 + 1, math.min(nview, m * 4) do
      local sat = SKY[i]
      parts[#parts + 1] = string.format("%02d,%02d,%03d,%s", sat[1], sat[2], sat[3], fixed and string.format("%02d", sat[4]) or "")
    end
    out[#out + 1] = table.concat(parts, ",")
  end
  if fixed then
    local lat, lon = input(ctx, "latitude", 0), input(ctx, "longitude", 0)
    out[#out + 1] = string.format("GPGLL,%s,%s,%s,%s,A,A", coord(lat, 2), lat < 0 and "S" or "N", coord(lon, 3), lon < 0 and "W" or "E", time)
  else
    out[#out + 1] = "GPGLL,,,,,,V,N"
  end
  local text = {}
  for _, b in ipairs(out) do text[#text + 1] = checksum(b) end
  return table.concat(text)
end

-- the UART: bits at the frame's own times, so the rate never drifts
local function send(ctx, text)
  local s = ctx.state
  s.queue = (s.queue or "") .. text
  if s.tx_busy then return end
  s.tx_busy, s.qpos = true, 1
  s.frame_start = ctx.now()
  s.bit = 0
  ctx.drive("TXD", 0) -- the first start bit
  ctx.wake_at(s.frame_start + PS // ctx.params.baud, TOK_BIT)
end

local function next_bit(ctx)
  local s = ctx.state
  local baud = ctx.params.baud
  s.bit = s.bit + 1
  local byte = s.queue:byte(s.qpos)
  if s.bit <= 8 then
    ctx.drive("TXD", (byte >> (s.bit - 1)) & 1)
  elseif s.bit == 9 then
    ctx.drive("TXD", 1) -- the stop bit
  else
    s.qpos = s.qpos + 1
    if s.qpos > #s.queue then
      s.tx_busy, s.queue = false, ""
      return
    end
    s.frame_start = s.frame_start + (10 * PS) // baud
    s.bit = 0
    ctx.drive("TXD", 0)
  end
  ctx.wake_at(s.frame_start + ((s.bit + 1) * PS) // baud, TOK_BIT)
end

local function sees_enough(ctx)
  return input(ctx, "sky", true) and math.floor(input(ctx, "satellites", 8)) >= 4
end

function M.reset(ctx)
  local s = ctx.state
  s.t0 = epoch(ctx.params.utc_start)
  s.boot, s.n, s.fixed, s.had_fix, s.searching_since = ctx.now(), 0, false, false, ctx.now()
  s.sentences, s.tx_busy, s.queue = 0, false, ""
  ctx.drive("TXD", 1)
  ctx.drive("TIMEPULSE", 0)
  ctx.wake_at(s.boot + PS, TOK_SECOND)
end

function M.on_set(ctx, name)
  return name == "latitude" or name == "longitude" or name == "altitude" or name == "speed" or name == "course"
    or name == "satellites" or name == "sky"
end

function M.on_time(ctx, token)
  local s = ctx.state
  if token == TOK_SECOND then
    s.n = s.n + 1
    ctx.wake_at(s.boot + (s.n + 1) * PS, TOK_SECOND)
    if not powered(ctx) then return end
    -- the fix: the time to first fix of searching with the sky in view
    if sees_enough(ctx) then
      -- after a fix has been lost, the data sheet's reacquisition: 1 s
      local need = s.had_fix and 1 or TTFF[ctx.params.start]
      if not s.fixed and ctx.now() - s.searching_since >= need * PS then s.fixed, s.had_fix = true, true end
    else
      s.fixed, s.searching_since = false, ctx.now() + PS
    end
    if s.fixed then
      ctx.drive("TIMEPULSE", 1)
      ctx.wake_at(ctx.now() + 100 * MS, TOK_PPS_LOW)
    end
    send(ctx, sentences(ctx, s.t0 + s.n))
    s.sentences = s.sentences + 1
  elseif token == TOK_BIT then
    next_bit(ctx)
  elseif token == TOK_PPS_LOW then
    ctx.drive("TIMEPULSE", 0)
  end
end

function M.observe(ctx)
  local s = ctx.state
  return { fix = s.fixed, bursts = s.sentences }
end

return M
