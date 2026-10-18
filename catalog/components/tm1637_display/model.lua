-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- TM1637 4-digit 7-segment LED display module, as Titan Micro's TM1637
-- data sheet (V2.4) and the common module's schematic describe it.
--
--   - the two-wire interface: START is DIO falling while CLK is high, STOP
--     DIO rising while CLK is high; bits are taken LSB first on CLK's rising
--     edges; the chip pulls DIO low from the falling edge of the 8th clock
--     (within the data sheet's 100 ns) to the end of the 9th (released
--     within 300 ns); there is no address, so any byte stream addresses it;
--   - the first byte after a START is a command (B7..B6): 01 the data
--     command (write, or read the keys with B1; auto-increment or a fixed
--     address with B2), 11 the address command (C0H to C5H; C6H and up
--     drop the data until a valid address comes), 10 display control (on
--     with B3, the pulse width 1/16, 2/16, 4/16, 10/16 to 14/16 with
--     B2..B0); the bytes after an address command are display data;
--   - a STOP ends the transfer: bytes already acknowledged stay;
--   - reading the keys: after the command's ACK the chip shifts out the key
--     byte on DIO, LSB first, changing on CLK's falling edges; the module
--     wires no keys, so it reads 0xFF (the data sheet's "no key");
--   - the module: digit 1 to 4 left to right on GRID1 to GRID4, segments a
--     to g on SEG1 to SEG7 and SEG8 to the colon between digits 2 and 3
--     (the clock variant, `dots` "colon") or to each digit's decimal point
--     (`dots` "points"); 10 kOhm pull-ups on CLK and DIO to VCC;
--   - after power-on the display is off and the address C0H (the data
--     sheet); a clock pulse shorter than the data sheet's 400 ns is
--     reported (timed and electrical policies).
--
-- Not modelled: the grid multiplexing on the module's own LEDs (each digit
-- shows at the set pulse width, not as scanned grids), the 100 pF
-- capacitors on CLK and DIO, the display RAM's content at power-up (taken
-- as zero), test mode (B3 of the data command), key scanning (no keys on
-- the module), the LEDs' current and brightness spread.
local M = {}

local NS = 1000
local TOK_DIO = 0
local WIDTH = { 1, 2, 4, 10, 11, 12, 13, 14 } -- sixteenths
local GLYPH = {
  [0x3F] = "0", [0x06] = "1", [0x5B] = "2", [0x4F] = "3", [0x66] = "4", [0x6D] = "5", [0x7D] = "6",
  [0x07] = "7", [0x7F] = "8", [0x6F] = "9", [0x77] = "A", [0x7C] = "b", [0x39] = "C", [0x5E] = "d",
  [0x79] = "E", [0x71] = "F", [0x40] = "-", [0x00] = " ", [0x54] = "n", [0x5C] = "o", [0x50] = "r",
  [0x74] = "h", [0x76] = "H", [0x38] = "L", [0x73] = "P", [0x3E] = "U", [0x1C] = "u", [0x6E] = "y",
  [0x78] = "t", [0x08] = "_", [0x1E] = "J", [0x58] = "c", [0x3D] = "G",
}

local function delay(ctx, ns)
  if ctx.policy == "functional" then return 0 end
  return ns * NS
end

-- DIO pulled low or released after the data sheet's delay (tPZL 100 ns,
-- tPLZ 300 ns); a newer change replaces a pending one
local function set_dio(ctx, low)
  local s = ctx.state
  s.dio_low = low
  local d = delay(ctx, low and 100 or 300)
  if d == 0 then
    if low then ctx.drive("DIO", 0) else ctx.release("DIO") end
  else
    ctx.wake_at(ctx.now() + d, TOK_DIO)
  end
end

local function level(ctx, pin)
  local v = ctx.pins[pin].level
  return v == 1 and 1 or 0
end

function M.reset(ctx)
  local s = ctx.state
  s.ram = { 0, 0, 0, 0, 0, 0 }
  s.addr, s.auto, s.read = 0, true, false
  s.on, s.width = false, 1
  s.active, s.first, s.bits, s.byte, s.clocks = false, true, 0, 0, 0
  s.reading, s.key = false, 0xFF
  s.clk, s.dio = level(ctx, "CLK"), level(ctx, "DIO")
  s.edge, s.warned, s.ver, s.dio_low = ctx.now(), false, 0, false
  ctx.release("DIO")
end

-- a whole byte, acknowledged at the 9th clock's falling edge
local function byte(ctx, b)
  local s = ctx.state
  if s.first then
    s.first = false
    local kind = b >> 6
    if kind == 1 then
      s.read = (b & 0x03) == 0x02
      s.auto = (b & 0x04) == 0
      if s.read then s.reading, s.key = true, 0xFF end
    elseif kind == 3 then
      s.addr = b & 0x0F
    elseif kind == 2 then
      s.on = (b & 0x08) ~= 0
      s.width = WIDTH[(b & 0x07) + 1]
    end
    s.ver = s.ver + 1
    return
  end
  if s.reading then return end
  -- display data: C6H and up is ignored until a valid address
  if s.addr <= 5 then s.ram[s.addr + 1] = b end
  if s.auto and s.addr <= 5 then s.addr = s.addr + 1 end
  s.ver = s.ver + 1
end

local function check_width(ctx)
  local s = ctx.state
  local now = ctx.now()
  if ctx.policy ~= "functional" and not s.warned and now - s.edge < 400 * NS then
    s.warned = true
    ctx.diag("warning", "tm1637.timing", "CLK pulse shorter than the data sheet's 400 ns")
  end
  s.edge = now
end

function M.on_input(ctx, pin)
  local s = ctx.state
  local clk, dio = level(ctx, "CLK"), level(ctx, "DIO")
  if pin == "CLK" and clk ~= s.clk then
    s.clk = clk
    check_width(ctx)
    if not s.active then return end
    if clk == 1 then
      if s.clocks < 8 and not s.reading_byte then
        s.byte = s.byte | (dio << s.clocks)
      end
      s.clocks = s.clocks + 1
    else
      if s.clocks == 8 then
        -- the 8th clock's falling edge: ACK
        set_dio(ctx, true)
      elseif s.clocks == 9 then
        -- the end of the 9th clock: release (or the key byte's first bit)
        if not s.reading_byte then byte(ctx, s.byte) end
        s.clocks, s.byte, s.reading_byte = 0, 0, false
        if s.reading then
          s.reading_byte, s.reading = true, false
          set_dio(ctx, (s.key & 1) == 0)
        else
          set_dio(ctx, false)
        end
      elseif s.reading_byte and s.clocks < 8 then
        -- the next key bit
        set_dio(ctx, ((s.key >> s.clocks) & 1) == 0)
      end
    end
  elseif pin == "DIO" and dio ~= s.dio then
    s.dio = dio
    if clk == 1 then
      if dio == 0 then
        -- START
        s.active, s.first, s.clocks, s.byte, s.reading_byte, s.reading = true, true, 0, 0, false, false
      else
        -- STOP: what was acknowledged stays
        s.active, s.reading_byte, s.reading = false, false, false
        if s.dio_low then set_dio(ctx, false) end
      end
    end
  end
end

function M.on_time(ctx, token)
  if token == TOK_DIO then
    if ctx.state.dio_low then ctx.drive("DIO", 0) else ctx.release("DIO") end
  end
end

function M.on_set() return false end

function M.observe(ctx)
  local s = ctx.state
  if s.cache and s.cache_ver == s.ver then return s.cache end
  local o = { on = s.on, brightness = s.on and s.width / 16 or 0 }
  local points = ctx.params.dots == "points"
  local text, hex = {}, {}
  for d = 1, 4 do
    local b = s.ram[d]
    hex[d] = string.format("%02X", b)
    for k, seg in ipairs({ "a", "b", "c", "d", "e", "f", "g" }) do
      o["d" .. d .. "_" .. seg] = s.on and (b >> (k - 1)) & 1 == 1
    end
    local dp = (b & 0x80) ~= 0
    o["d" .. d .. "_dp"] = s.on and points and dp
    local ch = s.on and (GLYPH[b & 0x7F] or "?") or " "
    text[#text + 1] = ch
    if points and dp and s.on then text[#text + 1] = "." end
    if d == 2 and not points then
      local colon = s.on and dp
      o.colon = colon
      if colon then text[#text + 1] = ":" end
    end
  end
  if points then o.colon = false end
  o.text = table.concat(text)
  o.segments = table.concat(hex, " ")
  s.cache, s.cache_ver = o, s.ver
  return o
end

return M
