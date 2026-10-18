-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- InvenSense MPU-6050 six-axis motion sensor (accelerometer, gyroscope and
-- temperature), as its datasheet and register map describe it.
--
--   - I2C at 0x68 + AD0 (0x69 with AD0 high), register address then data,
--     reads and writes auto-incrementing; WHO_AM_I reads 0x68 whatever AD0;
--   - power-on and DEVICE_RESET: every register 0 but PWR_MGMT_1 (0x40,
--     asleep) and WHO_AM_I; the reset bit clears itself; asleep, the
--     sensor registers keep their last values (0 after reset);
--   - the sample rate: the gyroscope output rate (8 kHz with the DLPF off,
--     DLPF_CFG 0 or 7, else 1 kHz) over 1 + SMPLRT_DIV; the sensor
--     registers change at that rate, each sample the inputs as they were
--     the DLPF's delay earlier (the register map's table: 0 to 19 ms);
--   - the full-scale ranges (+-2 to 16 g at 16384 to 2048 LSB/g, +-250 to
--     2000 deg/s at 131 to 16.4 LSB/(deg/s)), readings saturating at the
--     16-bit limits; the temperature as 340 LSB/C with 0 at 36.53 C;
--   - a burst read takes all the sensor registers from one sampling instant
--     (the part updates the user registers while the bus is idle);
--   - PWR_MGMT_2's standby bits and TEMP_DIS freeze their axes;
--   - INT: DATA_RDY_EN pulses it 50 us at each sample (or latches it with
--     LATCH_INT_EN until INT_STATUS is read, or any read with INT_RD_CLEAR),
--     active high push-pull unless INT_LEVEL / INT_OPEN say otherwise;
--   - VDD below 2.375 V: no answer.
--
-- Not modelled: the FIFO (FIFO_COUNT reads 0), the auxiliary I2C master and
-- pass-through, motion detection, self-test, CYCLE mode, FSYNC and CLKIN,
-- sensor noise, offsets and their temperature drift (the readings are exact
-- to the LSB), cross-axis sensitivity, the start-up settling (30 ms gyro),
-- supply current.
local M = {}

local US = 1000000
local MS = 1000 * US
local DELAY_A = { [0] = 0, 2000, 3000, 4900, 8500, 13800, 19000, 0 }  -- us
local DELAY_G = { [0] = 980, 1900, 2800, 4800, 8300, 13400, 18600, 980 }
local A_LSB = { [0] = 16384, 8192, 4096, 2048 }
local G_LSB = { [0] = 131, 65.5, 32.8, 16.4 }
local AXES = { "x", "y", "z" }
local TOK_SAMPLE, TOK_INT_END = 1, 2

local function default_input(name)
  if name == "accel_z" then return 1 end
  if name == "temperature" then return 25 end
  return 0
end

local function powered(ctx)
  local v = ctx.analog.voltage("VDD")
  return v == nil or v >= 2.375
end

local function device_reset(ctx)
  local s = ctx.state
  s.reg = {}
  for a = 0, 0x75 do s.reg[a] = 0 end
  s.reg[0x6B], s.reg[0x75] = 0x40, 0x68
  s.int = false
  s.drive_int(ctx)
end

-- an input's value at time t: changes are kept with their times
local function at(ctx, name, t)
  local h = ctx.state.hist[name]
  local v = default_input(name)
  if not h then return v end
  for _, e in ipairs(h) do
    if e[1] <= t then v = e[2] else break end
  end
  return v
end

local function asleep(s) return (s.reg[0x6B] & 0x40) ~= 0 end

local function sample_period(s)
  local dlpf = s.reg[0x1A] & 7
  local rate = (dlpf == 0 or dlpf == 7) and 8000 or 1000
  return math.floor(1e12 * (1 + s.reg[0x19]) / rate + 0.5)
end

local function int16(x)
  x = x >= 0 and math.floor(x + 0.5) or -math.floor(-x + 0.5) -- nearest, halves away from 0
  if x > 32767 then x = 32767 elseif x < -32768 then x = -32768 end
  return x & 0xFFFF
end

-- the last sampling instant at or before now (nil asleep)
local function instant(ctx)
  local s = ctx.state
  if asleep(s) then return nil end
  local p = sample_period(s)
  return s.wake + ((ctx.now() - s.wake) // p) * p
end

-- the sensor registers as the sample at instant t sets them, over `reg`
local function compute(ctx, t, reg)
  local s = ctx.state
  local dlpf = s.reg[0x1A] & 7
  local afs, gfs = (s.reg[0x1C] >> 3) & 3, (s.reg[0x1B] >> 3) & 3
  local stby = s.reg[0x6C]
  for i, ax in ipairs(AXES) do
    if (stby & (1 << (6 - i))) == 0 then -- STBY_XA at bit 5
      local v = int16(at(ctx, "accel_" .. ax, t - DELAY_A[dlpf] * US) * A_LSB[afs])
      reg[0x3B + 2 * (i - 1)], reg[0x3C + 2 * (i - 1)] = v >> 8, v & 0xFF
    end
    if (stby & (1 << (3 - i))) == 0 then -- STBY_XG at bit 2
      local v = int16(at(ctx, "gyro_" .. ax, t - DELAY_G[dlpf] * US) * G_LSB[gfs])
      reg[0x43 + 2 * (i - 1)], reg[0x44 + 2 * (i - 1)] = v >> 8, v & 0xFF
    end
  end
  if (s.reg[0x6B] & 0x08) == 0 then
    local v = int16((at(ctx, "temperature", t) - 36.53) * 340)
    reg[0x41], reg[0x42] = v >> 8, v & 0xFF
  end
end

-- the user registers brought up to the last sample
local function sample(ctx)
  local s = ctx.state
  local t = instant(ctx)
  if t == nil or s.sampled_at == t then return end
  s.sampled_at = t
  compute(ctx, t, s.reg)
end

local function value(reg, a)
  local hi = reg[a]
  local v = (hi << 8) | reg[a + 1]
  return v >= 0x8000 and v - 0x10000 or v
end

local function schedule_int(ctx)
  local s = ctx.state
  if asleep(s) or (s.reg[0x38] & 1) == 0 then return end
  local p = sample_period(s)
  local now = ctx.now()
  s.next_int = s.wake + ((now - s.wake) // p + 1) * p
  ctx.wake_at(s.next_int, TOK_SAMPLE)
end

local function wake(ctx)
  local s = ctx.state
  s.wake, s.sampled_at = ctx.now(), nil
  schedule_int(ctx)
end

local function read_reg(ctx, a)
  local s = ctx.state
  local v = s.snap and s.snap[a] or s.reg[a] or 0
  if a == 0x3A then
    -- DATA_RDY_INT: a sample since INT_STATUS was last read
    local t = instant(ctx)
    v = (t ~= nil and t > s.status_read) and 1 or 0
    s.status_read = ctx.now()
    if s.int and (s.reg[0x37] & 0x20) ~= 0 then s.int = false; s.drive_int(ctx) end
  elseif s.int and (s.reg[0x37] & 0x30) == 0x30 then
    s.int = false; s.drive_int(ctx)
  end
  if a == 0x6B then v = v & 0x7F end
  return v
end

local WRITABLE = { [0x0D] = 1, [0x0E] = 1, [0x0F] = 1, [0x10] = 1, [0x19] = 1, [0x1A] = 1, [0x1B] = 1, [0x1C] = 1,
  [0x1F] = 1, [0x23] = 1, [0x37] = 1, [0x38] = 1, [0x67] = 1, [0x68] = 1, [0x6A] = 1, [0x6B] = 1, [0x6C] = 1 }

local function write_reg(ctx, a, v)
  local s = ctx.state
  if not WRITABLE[a] then return end
  if a == 0x6B then
    if v & 0x80 ~= 0 then device_reset(ctx) return end
    local was = asleep(s)
    sample(ctx)
    s.reg[a] = v
    s.sampled_at = nil
    if was and not asleep(s) then wake(ctx) end
    return
  end
  if a == 0x68 then return end -- SIGNAL_PATH_RESET: the paths restart; registers are kept
  sample(ctx) -- what the old settings sampled stays
  s.reg[a] = v
  s.sampled_at = nil
  if a == 0x19 or a == 0x1A or a == 0x38 then
    if not asleep(s) then wake(ctx) end
  end
end

function M.create(ctx)
  local s = ctx.state
  s.drive_int = function(ctx)
    local cfg = s.reg and s.reg[0x37] or 0
    local active = (cfg & 0x80) == 0 and 1 or 0
    local level = s.int and active or (1 - active)
    if (cfg & 0x40) ~= 0 and level == 1 then ctx.release("INT") else ctx.drive("INT", level) end
  end
  ctx.bus.i2c_slave(0x68, {
    start = function(ctx, dir)
      if not powered(ctx) then return false end
      if dir == "write" then
        s.first = true
      else
        sample(ctx)
        s.snap = {}
        for a = 0x3B, 0x48 do s.snap[a] = s.reg[a] end
      end
      return true
    end,
    write = function(ctx, b)
      if s.first then
        s.ptr, s.first = b, false
      else
        write_reg(ctx, s.ptr, b)
        s.ptr = (s.ptr + 1) & 0xFF
      end
    end,
    read = function(ctx)
      local v = read_reg(ctx, s.ptr)
      s.ptr = (s.ptr + 1) & 0xFF
      return v
    end,
    stop = function(ctx) s.snap = nil end,
  }, { address_pins = { "AD0" } })
end

function M.reset(ctx)
  local s = ctx.state
  s.hist, s.ptr, s.wake, s.status_read = {}, 0, ctx.now(), -1
  device_reset(ctx)
  s.drive_int(ctx)
end

function M.on_set(ctx, name, value)
  local s = ctx.state
  if not (name == "temperature" or name:match("^accel_[xyz]$") or name:match("^gyro_[xyz]$")) then return false end
  local h = s.hist[name] or {}
  h[#h + 1] = { ctx.now(), value }
  if #h > 64 then table.remove(h, 1) end
  s.hist[name] = h
  return true
end

function M.on_time(ctx, token)
  local s = ctx.state
  if token == TOK_SAMPLE then
    if s.next_int ~= ctx.now() or asleep(s) or (s.reg[0x38] & 1) == 0 then return end
    sample(ctx)
    s.int = true
    s.drive_int(ctx)
    if (s.reg[0x37] & 0x20) == 0 then ctx.wake_at(ctx.now() + 50 * US, TOK_INT_END) end
    schedule_int(ctx)
  elseif token == TOK_INT_END then
    if (s.reg[0x37] & 0x20) == 0 then s.int = false; s.drive_int(ctx) end
  end
end

-- what the sensor registers hold now, converted (looking changes nothing)
function M.observe(ctx)
  local s = ctx.state
  local reg = {}
  for a = 0x3B, 0x48 do reg[a] = s.reg[a] end
  local t = instant(ctx)
  if t ~= nil then compute(ctx, t, reg) end
  local afs, gfs = (s.reg[0x1C] >> 3) & 3, (s.reg[0x1B] >> 3) & 3
  local o = { asleep = asleep(s), temperature = value(reg, 0x41) / 340 + 36.53 }
  for i, ax in ipairs(AXES) do
    o["accel_" .. ax] = value(reg, 0x3B + 2 * (i - 1)) / A_LSB[afs]
    o["gyro_" .. ax] = value(reg, 0x43 + 2 * (i - 1)) / G_LSB[gfs]
  end
  return o
end

return M
