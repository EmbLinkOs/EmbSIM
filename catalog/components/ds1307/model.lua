local M = {}
local function v(ctx, p) return ctx.pins[p].value end
local function high(ctx, p) return ctx.pins[p].value == 1 end
local function low(ctx, p) return ctx.pins[p].value == 0 end
-- the datasheet delay in picoseconds; none under the functional policy
local function delay(ctx)
  if ctx.policy == "functional" or not ctx.params.tpd then return 0 end
  return math.floor(ctx.params.tpd * 1e12 + 0.5)
end
local function out(ctx, p, bit) ctx.drive(p, bit and 1 or 0, "strong", delay(ctx)) end

local PS = 1000000000000
local function bcd(n) return ((n // 10) << 4) | (n % 10) end
local function unbcd(b) return ((b >> 4) & 15) * 10 + (b & 15) end
-- days from 1970-01-01 (proleptic Gregorian), after H. Hinnant's days_from_civil
local function days(y, m, d)
  y = m <= 2 and y - 1 or y
  local era = (y >= 0 and y or y - 399) // 400
  local yoe = y - era * 400
  local doy = (153 * (m + (m > 2 and -3 or 9)) + 2) // 5 + d - 1
  local doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
  return era * 146097 + doe - 719468
end
local function civil(z)
  z = z + 719468
  local era = (z >= 0 and z or z - 146096) // 146097
  local doe = z - era * 146097
  local yoe = (doe - doe // 1460 + doe // 36524 - doe // 146096) // 365
  local y = yoe + era * 400
  local doy = doe - (365 * yoe + yoe // 4 - yoe // 100)
  local mp = (5 * doy + 2) // 153
  local d = doy - (153 * mp + 2) // 5 + 1
  local m = mp + (mp < 10 and 3 or -9)
  return m <= 2 and y + 1 or y, m, d
end
local function seconds_now(ctx)
  local s = ctx.state
  if s.halted then return s.base end
  return s.base + (ctx.now() - s.base_ps) // PS
end
local function regs(ctx)
  local t = seconds_now(ctx)
  local day, sec = t // 86400, t % 86400
  local y, m, d = civil(day)
  local wd = (day + 3) % 7 + 1                -- 1970-01-01 was a Thursday: Monday = 1
  return { [0] = bcd(sec % 60) | (ctx.state.halted and 0x80 or 0), bcd((sec // 60) % 60), bcd(sec // 3600), wd, bcd(d), bcd(m), bcd(y % 100) }, t
end
local function set_time(ctx, r)
  local s = ctx.state
  local y = 2000 + unbcd(r[6])
  local t = days(y, unbcd(r[5] & 0x1F), unbcd(r[4] & 0x3F)) * 86400 + unbcd(r[2] & 0x3F) * 3600 + unbcd(r[1] & 0x7F) * 60 + unbcd(r[0] & 0x7F)
  s.base, s.base_ps, s.halted = t, ctx.now(), (r[0] & 0x80) ~= 0
end
local function sqw(ctx)
  local s = ctx.state
  ctx.cancel(0)
  if (s.ctrl & 0x10) ~= 0 and not s.halted then
    local hz = ({ [0] = 1, 4096, 8192, 32768 })[s.ctrl & 3]
    s.half = PS // (2 * hz)
    s.level = 1
    ctx.release("SQW")
    ctx.wake_at(ctx.now() + s.half, 0)
  elseif (s.ctrl & 0x80) ~= 0 then ctx.release("SQW") else ctx.drive("SQW", 0) end
end
function M.create(ctx)
  local s = ctx.state
  local y, mo, d, h, mi, se = string.match(ctx.params.start, "(%d+)-(%d+)-(%d+)[ T](%d+):(%d+):(%d+)")
  if not y then error("start must look like 2026-01-01 00:00:00") end
  s.base = days(tonumber(y), tonumber(mo), tonumber(d)) * 86400 + tonumber(h) * 3600 + tonumber(mi) * 60 + tonumber(se)
  s.base_ps, s.halted, s.ctrl, s.ram, s.ptr = 0, false, 0x03, {}, 0
  for i = 8, 63 do s.ram[i] = 0 end
  ctx.bus.i2c_slave(0x68, {
    start = function(ctx, dir) s.n, s.shadow = 0, nil end,
    write = function(ctx, b)
      s.n = s.n + 1
      if s.n == 1 then s.ptr = b & 63 return end
      if s.ptr <= 6 then s.shadow = s.shadow or regs(ctx); s.shadow[s.ptr] = b
      elseif s.ptr == 7 then s.ctrl = b; sqw(ctx)
      else s.ram[s.ptr] = b end
      s.ptr = (s.ptr + 1) & 63
    end,
    read = function(ctx)
      local r = regs(ctx)
      local b = s.ptr <= 6 and r[s.ptr] or (s.ptr == 7 and s.ctrl or s.ram[s.ptr])
      s.ptr = (s.ptr + 1) & 63
      return b
    end,
    stop = function(ctx) if s.shadow then set_time(ctx, s.shadow); s.shadow = nil; sqw(ctx) end end })
end
function M.reset(ctx) sqw(ctx) end
function M.on_time(ctx)
  local s = ctx.state
  s.level = 1 - s.level
  if s.level == 1 then ctx.release("SQW") else ctx.drive("SQW", 0) end
  ctx.wake_at(ctx.now() + s.half, 0)
end
function M.observe(ctx)
  local t = seconds_now(ctx)
  local y, m, d = civil(t // 86400)
  local sec = t % 86400
  return { time = string.format("%04d-%02d-%02d %02d:%02d:%02d", y, m, d, sec // 3600, (sec // 60) % 60, sec % 60),
           running = not ctx.state.halted }
end
return M
