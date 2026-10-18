-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- HD44780-compatible character LCD module, as Hitachi's datasheet
-- describes the controller; the glass is `columns` by `rows` (16x2, 20x4).
-- Shared by: lcd1602, lcd2004
--
--   - power-on reset: 8-bit interface, one line, display, cursor and blink
--     off, increment, no shift, the display cleared; busy for 10 ms;
--   - instructions and data are latched on E's falling edge, eight bits at
--     once or, after a function set with DL = 0, a high then a low nibble
--     on DB7..DB4; each takes its execution time (37 us; clear and return
--     home 1.52 ms) and the controller ignores writes while busy, as the
--     part does, so a program that does not wait loses them;
--   - reads (R/W high) give the busy flag and the address counter (RS low)
--     or the DDRAM or CGRAM byte at the address counter, which then moves
--     (RS high);
--   - RS, R/W and DB0..DB7 have the part's pull-ups: an unconnected R/W
--     reads high, so the controller reads instead of being written, and
--     the unconnected DB3..DB0 of a 4-bit wiring read as ones;
--   - DDRAM: one line of 80 or two of 40 (0x00 and 0x40), the display and
--     cursor shift, the address counter wrapping as the part's does; CGRAM:
--     eight characters stored and read back;
--   - the glass: row 1 shows line 1 from its start and row 2 line 2; on a
--     four-row module rows 3 and 4 continue lines 1 and 2 (a 20x4's rows
--     start at 0x00, 0x40, 0x14 and 0x54), so text running off row 1 goes
--     on in row 3, as on the real module; in one-line mode rows 2 and 4 stay
--     blank;
--   - the character ROM A00: ASCII with the yen sign at 0x5C and arrows at
--     0x7E and 0x7F, katakana at 0xA1..0xDF, Greek and symbols above;
--   - the characters show only while the contrast voltage VDD - V0 is at
--     least 3.0 V (V0 near VDD shows nothing, as a contrast pot turned the
--     wrong way does); the backlight is on while A is 2.5 V above K.
--
-- Not modelled: the 5x10 font, execution times' change with the supply
-- and temperature (the 270 kHz oscillator's), the CGRAM characters' pixels
-- in the lines output (shown as #), the cursor and blink drawing.
local M = {}

local US = 1000000
local EXEC, EXEC_LONG, POWER_UP = 37 * US, 1520 * US, 10000 * US
local DATA = { "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7" }
local PULLED = { "RS", "RW", "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7" }

local IS_PULLED = { RS = true, RW = true, D0 = true, D1 = true, D2 = true, D3 = true,
  D4 = true, D5 = true, D6 = true, D7 = true }

-- a pin's level; the pulled-up ones read high unconnected or undriven
local function high(ctx, p)
  local pin = ctx.pins[p]
  if IS_PULLED[p] and (not pin.connected or pin.level == "z") then return true end
  return pin.value == 1
end

local function blank(n)
  local t = {}
  for i = 0, n - 1 do t[i] = 32 end
  return t
end

local function pull_ups(ctx, list)
  for _, p in ipairs(list) do ctx.drive(p, 1, "weak") end
end

function M.reset(ctx)
  local s = ctx.state
  s.dd, s.cgram, s.ac, s.cg = blank(128), blank(64), 0, false
  for i = 0, 63 do s.cgram[i] = 0 end
  s.inc, s.autoshift, s.shift = true, false, 0
  s.on, s.cursor, s.blink = false, false, false
  s.eight, s.lines2 = true, false
  s.half, s.hi, s.reading = 0, 0, false
  s.e = ctx.pins.E.value
  s.busy_until = ctx.now() + POWER_UP
  pull_ups(ctx, PULLED)
end

local function busy(ctx) return ctx.now() < ctx.state.busy_until end

local function step(s)
  if s.cg then
    s.ac = (s.ac + (s.inc and 1 or -1)) % 64
    return
  end
  if s.inc then s.ac = s.ac + 1 else s.ac = s.ac - 1 end
  if s.lines2 then
    if s.ac == 0x28 then s.ac = 0x40 elseif s.ac == 0x68 then s.ac = 0x00
    elseif s.ac == -1 then s.ac = 0x67 elseif s.ac == 0x3F then s.ac = 0x27 end
  else
    s.ac = s.ac % 80
  end
end

local function command(ctx, c)
  local s = ctx.state
  local t = EXEC
  if c == 0x01 then
    s.dd, s.ac, s.inc, s.shift, s.cg = blank(128), 0, true, 0, false
    t = EXEC_LONG
  elseif c & 0xFE == 0x02 then
    s.ac, s.shift, s.cg = 0, 0, false
    t = EXEC_LONG
  elseif c & 0xFC == 0x04 then
    s.inc, s.autoshift = (c & 2) ~= 0, (c & 1) ~= 0
  elseif c & 0xF8 == 0x08 then
    s.on, s.cursor, s.blink = (c & 4) ~= 0, (c & 2) ~= 0, (c & 1) ~= 0
  elseif c & 0xF0 == 0x10 then
    if (c & 8) ~= 0 then s.shift = s.shift + (((c & 4) ~= 0) and -1 or 1) else step(s) end
  elseif c & 0xE0 == 0x20 then
    s.eight, s.lines2 = (c & 0x10) ~= 0, (c & 0x08) ~= 0
    s.half = 0
  elseif c & 0xC0 == 0x40 then
    s.ac, s.cg = c & 0x3F, true
  elseif c & 0x80 == 0x80 then
    s.ac, s.cg = c & 0x7F, false
  end
  s.busy_until = ctx.now() + t
end

local function write_data(ctx, d)
  local s = ctx.state
  if s.cg then
    s.cgram[s.ac] = d & 0x1F
  else
    s.dd[s.ac] = d
    if s.autoshift then s.shift = s.shift + (s.inc and 1 or -1) end
  end
  step(s)
  s.busy_until = ctx.now() + EXEC
end

local function bus(ctx, from, to)
  local b = 0
  for i = from, to do
    if high(ctx, DATA[i + 1]) then b = b | (1 << (i - from)) end
  end
  return b
end

local function put(ctx, val, from, to)
  for i = from, to do ctx.drive(DATA[i + 1], (val >> (i - from)) & 1) end
end

function M.on_input(ctx, pin)
  local s = ctx.state
  if pin ~= "E" then return end
  local e = ctx.pins.E.value
  local rw = high(ctx, "RW")
  if s.e ~= 1 and e == 1 and rw then
    -- a read: the busy flag and address counter, or the RAM byte
    local val
    if high(ctx, "RS") then
      val = s.cg and s.cgram[s.ac] or (s.dd[s.ac] or 32)
      s.read_data = true
    else
      val = (busy(ctx) and 0x80 or 0) | (s.ac & 0x7F)
      s.read_data = false
    end
    if s.eight then
      put(ctx, val, 0, 7)
    else
      put(ctx, s.half == 0 and (val >> 4) or (val & 15), 4, 7)
    end
    s.reading = true
  elseif s.e == 1 and e ~= 1 then
    if s.reading then
      pull_ups(ctx, DATA)
      s.reading = false
      local done = s.eight
      if not s.eight then
        s.half = 1 - s.half
        done = s.half == 0
      end
      if done and s.read_data then
        step(s)
        s.busy_until = ctx.now() + EXEC
      end
    elseif not rw and not busy(ctx) then
      local rs = high(ctx, "RS")
      local b
      if s.eight then
        b = bus(ctx, 0, 7)
      elseif s.half == 0 then
        s.hi, s.half = bus(ctx, 4, 7), 1
      else
        s.half = 0
        b = (s.hi << 4) | bus(ctx, 4, 7)
      end
      if b then
        if rs then write_data(ctx, b) else command(ctx, b) end
      end
    end
  end
  s.e = e
end

-- the character ROM A00, as UTF-8
local HIGH = { [0xE0] = "α", "ä", "β", "ε", "μ", "σ", "ρ", "g", "√", "⁻", "j", "ˣ", "¢", "£", "ñ", "ö",
  "p", "q", "θ", "∞", "Ω", "ü", "Σ", "π", "x̄", "y", "千", "万", "円", "÷", " ", "█" }

local function glyph(c)
  if c < 0x10 then return "#" end -- a CGRAM character
  if c == 0x5C then return "¥" end
  if c == 0x7E then return "→" end
  if c == 0x7F then return "←" end
  if c >= 0x20 and c < 0x7E then return string.char(c) end
  if c >= 0xA1 and c <= 0xDF then return utf8.char(0xFF61 + c - 0xA1) end
  if c >= 0xE0 then return HIGH[c] end
  return " "
end

local function visible(ctx)
  if not ctx.pins.V0.connected then return true end
  local v0, vdd = ctx.analog.voltage("V0"), ctx.analog.voltage("VDD")
  return v0 == nil or vdd == nil or vdd - v0 >= 3.0
end

-- row r (0 first) of the glass
local function row(ctx, r)
  local s = ctx.state
  local cols = ctx.params.columns
  if not s.on or not visible(ctx) or (not s.lines2 and r % 2 == 1) then return string.rep(" ", cols) end
  local off = (r // 2) * cols -- rows 3 and 4 continue lines 1 and 2
  local t = {}
  for i = 0, cols - 1 do
    local a
    if s.lines2 then
      a = (i + off + s.shift) % 40 + (r % 2 == 1 and 0x40 or 0)
    else
      a = (i + off + s.shift) % 80
    end
    t[#t + 1] = glyph(s.dd[a] or 32)
  end
  return table.concat(t)
end

function M.observe(ctx)
  local s = ctx.state
  local a, k = ctx.analog.voltage("A"), ctx.analog.voltage("K")
  local o = {
    on = s.on,
    cursor = s.ac,
    four_bit = not s.eight,
    contrast_ok = visible(ctx),
    backlight = a ~= nil and k ~= nil and a - k >= 2.5,
  }
  for r = 0, ctx.params.rows - 1 do o["line" .. (r + 1)] = row(ctx, r) end
  return o
end

return M
