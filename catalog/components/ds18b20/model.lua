-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- DS18B20 programmable-resolution 1-Wire digital thermometer, as Maxim's
-- datasheet describes it.
--
--   - 1-Wire signalling on DQ (open drain, the bus pulled up outside): a
--     reset (the master holding the bus low; the part takes 456 us, the
--     datasheet's 480 us minimum less 5 %, as one) answered 27 us after the
--     bus rises with a 120 us presence pulse; write slots sampled 30 us
--     after the master's falling edge; read slots answered on the falling
--     edge, a 0 holding the bus low for 28 us (the presence and the 0 as two
--     real parts did, from sigrok's capture of them);
--   - the 64-bit ROM code: family 0x28, the 48-bit `serial`, the Maxim CRC;
--     READ ROM, MATCH ROM, SKIP ROM, SEARCH ROM and ALARM SEARCH (the part
--     answers only while its alarm flag is set), the search sending each
--     bit and its complement and dropping out when the master picks the
--     other branch;
--   - CONVERT T taking the datasheet's maximum time at the resolution (9 to
--     12 bits: 93.75, 187.5, 375, 750 ms), read slots returning 0 until it
--     is done when the part has its own supply; the temperature input then
--     stored at the resolution (the undefined low bits as 0), clamped to
--     -55..125 C, and the alarm flag set when its whole degrees reach TH or
--     fall to TL;
--   - the scratchpad: the temperature (85 C at power-up), TH, TL, the
--     configuration (0x1F with R1 R0), 0xFF, byte 6 (0x0C at power-up, then
--     0x10 minus the low nibble of the temperature, as genuine parts read),
--     0x10 and its CRC; WRITE SCRATCHPAD (TH, TL, configuration), READ
--     SCRATCHPAD (then 1s), COPY SCRATCHPAD to the EEPROM (10 ms, read slots
--     0 meanwhile), RECALL E2, READ POWER SUPPLY (0 when parasite powered);
--   - power: VDD at 3.0 V or more is its own supply; VDD grounded (or left
--     open) is parasite power from DQ; between 0.5 and 3.0 V it does not
--     answer.
--
-- Not modelled: the accuracy spread (+-0.5 C from -10 to 85 C), the
-- package's thermal lag and self-heating, the strong pull-up a parasite
-- powered conversion needs (not checked: it converts with the resistor
-- alone), the EEPROM keeping its values between runs (factory TH 75, TL
-- 70, 12 bits each time), overdrive (the DS18B20 has none).
local M = {}

local US = 1000000
local MS = 1000 * US
local T_RESET = 456 * US
-- the presence 27 us after the reset and a 0 held 28 us from the slot's
-- start: two real parts' times (sigrok's capture, tests/captures/ds18b20),
-- inside the datasheet's 15-60 us and 15 us minimum
local T_PDHIGH, T_PDLOW = 27 * US, 120 * US
local T_SAMPLE, T_HOLD = 30 * US, 28 * US
local T_WR = 10 * MS
local T_CONV = { [9] = 93750 * US, [10] = 187500 * US, [11] = 375 * MS, [12] = 750 * MS }

local TOK_PRESENCE_ON, TOK_PRESENCE_OFF, TOK_SAMPLE, TOK_RELEASE, TOK_CONVERT = 1, 2, 3, 4, 5

local function crc8(bytes)
  local crc = 0
  for _, b in ipairs(bytes) do
    for _ = 1, 8 do
      local mix = (crc ~ b) & 1
      crc = crc >> 1
      if mix == 1 then crc = crc ~ 0x8C end
      b = b >> 1
    end
  end
  return crc
end

local function rom(ctx)
  local r = { 0x28 }
  local sn = math.tointeger(ctx.params.serial) or 0
  for i = 0, 5 do r[#r + 1] = (sn >> (8 * i)) & 0xFF end
  r[8] = crc8(r)
  return r
end

-- "external", "parasite" or nil (not working)
local function supply(ctx)
  if not ctx.pins.VDD.connected then return "parasite" end
  local v = ctx.analog.voltage("VDD")
  if v == nil then return "external" end
  if v < 0.5 then return "parasite" end
  if v >= 3.0 then return "external" end
  return nil
end

local function resolution(s) return 9 + ((s.config >> 5) & 3) end

local function signed8(b) return b >= 128 and b - 256 or b end

local function scratchpad(s)
  local p = { s.temp & 0xFF, (s.temp >> 8) & 0xFF, s.th, s.tl, s.config, 0xFF, s.byte6, 0x10 }
  p[9] = crc8(p)
  return p
end

local function bits_of(bytes)
  local q = {}
  for _, b in ipairs(bytes) do
    for i = 0, 7 do q[#q + 1] = (b >> i) & 1 end
  end
  return q
end

local function idle(s) s.mode, s.tx, s.status = "idle", nil, nil end

local function receive(s, after)
  s.mode, s.rx, s.nbits, s.wn, s.after = "rx", 0, 0, 0, after
end

local function send(s, bytes)
  s.mode, s.tx, s.ti = "tx", bits_of(bytes), 0
end

-- read slots answered by a function of the time (busy or done)
local function status(s, fn) s.mode, s.status = "status", fn end

local function busy_until(ctx, t) return function() return ctx.now() >= t and 1 or 0 end end

local function convert_done(ctx)
  local s = ctx.state
  s.converting = nil
  local t = ctx.input("temperature") or 25
  if t < -55 then t = -55 elseif t > 125 then t = 125 end
  local q = 1 << (12 - resolution(s))
  local raw = math.floor(t * 16 / q + 0.5) * q
  s.temp = raw & 0xFFFF
  s.byte6 = 0x10 - (raw & 0x0F)
  local whole = raw // 16
  s.alarm = whole >= signed8(s.th) or whole <= signed8(s.tl)
  s.conversions = s.conversions + 1
  s.last = raw / 16
end

local function function_command(ctx, c)
  local s = ctx.state
  if c == 0x44 then -- CONVERT T
    local done = ctx.now() + T_CONV[resolution(s)]
    s.converting = done
    ctx.wake_at(done, TOK_CONVERT)
    if s.power == "external" then status(s, busy_until(ctx, done)) else status(s, function() return 1 end) end
  elseif c == 0x4E then -- WRITE SCRATCHPAD: TH, TL, configuration
    receive(s, function(b, n)
      if n == 1 then s.th = b elseif n == 2 then s.tl = b else s.config = (b & 0x60) | 0x1F end
      if n == 3 then idle(s) end
    end)
  elseif c == 0xBE then -- READ SCRATCHPAD
    send(s, scratchpad(s))
  elseif c == 0x48 then -- COPY SCRATCHPAD
    local done = ctx.now() + T_WR
    s.ee = { s.th, s.tl, s.config }
    if s.power == "external" then status(s, busy_until(ctx, done)) else status(s, function() return 1 end) end
  elseif c == 0xB8 then -- RECALL E2
    s.th, s.tl, s.config = s.ee[1], s.ee[2], s.ee[3]
    status(s, function() return 1 end)
  elseif c == 0xB4 then -- READ POWER SUPPLY
    status(s, function() return s.power == "external" and 1 or 0 end)
  else
    idle(s)
  end
end

local function rom_command(ctx, c)
  local s = ctx.state
  local after = function(b) function_command(ctx, b) end
  if c == 0x33 then -- READ ROM
    send(s, rom(ctx))
    s.then_rx = after
  elseif c == 0x55 then -- MATCH ROM
    local code, got = rom(ctx), {}
    receive(s, function(b)
      got[#got + 1] = b
      if b ~= code[#got] then idle(s) return end
      if #got == 8 then receive(s, after) end
    end)
  elseif c == 0xCC then -- SKIP ROM
    receive(s, after)
  elseif c == 0xF0 or (c == 0xEC and s.alarm) then -- SEARCH ROM, ALARM SEARCH
    s.mode, s.sbits, s.si, s.sphase = "search", bits_of(rom(ctx)), 1, 0
  else
    idle(s)
  end
end

function M.reset(ctx)
  local s = ctx.state
  s.temp, s.byte6 = 0x0550, 0x0C -- 85 C
  s.ee = { 0x4B, 0x46, 0x7F }    -- the factory EEPROM: TH 75, TL 70, 12 bits
  s.th, s.tl, s.config = s.ee[1], s.ee[2], s.ee[3]
  s.alarm, s.conversions, s.last = false, 0, 85
  s.fall, s.driving = nil, false
  idle(s)
  ctx.release("DQ")
end

-- the room's temperature, read when a conversion ends
function M.on_set(ctx, name) return name == "temperature" end

local function drive_low(ctx, hold, token)
  local s = ctx.state
  s.driving = true
  ctx.drive("DQ", 0)
  ctx.wake_at(ctx.now() + hold, token)
end

-- the next bit the part sends in a read slot
local function next_tx(ctx)
  local s = ctx.state
  if s.mode == "tx" then
    s.ti = s.ti + 1
    local b = s.tx[s.ti]
    if b == nil then return 1 end
    if s.ti == #s.tx and s.then_rx then
      local f = s.then_rx
      s.then_rx = nil
      receive(s, f)
    end
    return b
  elseif s.mode == "status" then
    return s.status()
  elseif s.mode == "search" then
    local b = s.sbits[s.si]
    if s.sphase == 0 then s.sphase = 1 return b end
    s.sphase = 2
    return 1 - b
  end
  return nil
end

function M.on_input(ctx, pin)
  local s = ctx.state
  if pin ~= "DQ" then return end
  local low = ctx.pins.DQ.level == 0
  if low then
    if s.driving or s.fall then return end
    s.fall = ctx.now() -- the master's falling edge: a slot or a reset
    if not s.power then return end
    if s.mode == "rx" or (s.mode == "search" and s.sphase == 2) then
      ctx.wake_at(ctx.now() + T_SAMPLE, TOK_SAMPLE)
    elseif s.mode ~= "idle" and s.mode ~= "presence" then
      local b = next_tx(ctx)
      if b == 0 then drive_low(ctx, T_HOLD, TOK_RELEASE) end
    end
  else
    if s.driving then return end
    local fall = s.fall
    s.fall = nil
    if fall and ctx.now() - fall >= T_RESET then
      s.power = supply(ctx)
      idle(s)
      if s.power then
        s.mode = "presence"
        ctx.wake_at(ctx.now() + T_PDHIGH, TOK_PRESENCE_ON)
      end
    end
  end
end

function M.on_time(ctx, token)
  local s = ctx.state
  if token == TOK_PRESENCE_ON then
    if s.mode == "presence" then drive_low(ctx, T_PDLOW, TOK_PRESENCE_OFF) end
  elseif token == TOK_PRESENCE_OFF or token == TOK_RELEASE then
    s.driving = false
    ctx.release("DQ")
    if token == TOK_PRESENCE_OFF and s.mode == "presence" then
      receive(s, function(b) rom_command(ctx, b) end)
    end
  elseif token == TOK_SAMPLE then
    local bit = ctx.pins.DQ.level == 0 and 0 or 1
    if s.mode == "search" then
      if bit ~= s.sbits[s.si] then idle(s) return end
      s.si, s.sphase = s.si + 1, 0
      if s.si > 64 then receive(s, function(b) function_command(ctx, b) end) end
    elseif s.mode == "rx" then
      s.rx = s.rx | (bit << s.nbits)
      s.nbits = s.nbits + 1
      if s.nbits == 8 then
        local b, f = s.rx, s.after
        s.wn = s.wn + 1
        s.rx, s.nbits = 0, 0
        f(b, s.wn)
      end
    end
  elseif token == TOK_CONVERT then
    if s.converting and ctx.now() >= s.converting then convert_done(ctx) end
  end
end

function M.observe(ctx)
  local s = ctx.state
  return { temperature = s.last, conversions = s.conversions, resolution = resolution(s), alarm = s.alarm }
end

return M
