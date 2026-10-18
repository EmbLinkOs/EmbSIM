-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- SSD1306 OLED controller on a 128x64 (or 128x32) module, as Solomon
-- Systech's datasheet describes the controller and the common I2C modules
-- wire it.
--
--   - I2C at the module's address (0x3C, or 0x3D with the SA0 jumper),
--     write-only: each transfer a control byte (Co, D/C#) then commands or
--     display data; a read is not acknowledged;
--   - every command with its arguments: contrast, entire display on,
--     inverse, display on/off, the three memory addressing modes with the
--     column and page windows, segment remap, COM scan direction,
--     multiplex ratio, display offset, start line, COM pins configuration,
--     clock divide, pre-charge, VCOMH, charge pump, and the scrolling
--     commands;
--   - what the panel shows: RAM through the segment remap (A0/A1), the scan
--     direction (C0/C8), the multiplex ratio, offset and start line, and
--     the COM pins configuration against how the panel is wired (128x64
--     panels need the alternative configuration 0x12, 128x32 panels the
--     sequential 0x02; the wrong one interleaves the rows, as on the real
--     module); the module's panel is mounted so A1 and C8 show the RAM
--     upright, and the reset state (A0, C0) shows it turned 180 degrees;
--   - the panel lights only with its high voltage: the internal charge pump
--     enabled (8Dh 14h) on a module without external VCC, and 100 ms after
--     display on (AFh), as the datasheet's power-on sequence says;
--   - horizontal and vertical scrolling run at the panel's frame rate,
--     370 kHz / (divide ratio x (phase 1 + phase 2 + 50) x multiplex
--     ratio), one column every 2 to 256 frames; deactivating (2Eh) leaves
--     the RAM as scrolled, so it must be rewritten, as the datasheet warns;
--   - below 1.65 V on VCC it does not acknowledge.
--
-- Not modelled: the oscillator frequency setting's effect on the frame
-- rate (370 kHz whatever D5h's upper nibble), contrast as brightness (an
-- output only), the SPI and parallel interfaces, the RES# pin, fade and
-- zoom (SSD1306B commands), RAM written during an active scroll (it is
-- written as when idle; the datasheet calls it undefined).
local M = {}

local MS = 1000000000
local FOSC = 370000 -- Hz, the reset setting's typical oscillator
local ARGS = { [0x81] = 1, [0x20] = 1, [0x21] = 2, [0x22] = 2, [0xA8] = 1, [0xD3] = 1, [0xD5] = 1, [0xD9] = 1, [0xDA] = 1,
  [0xDB] = 1, [0x8D] = 1, [0x26] = 6, [0x27] = 6, [0x29] = 5, [0x2A] = 5, [0xA3] = 2 }
local INTERVAL = { [0] = 5, 64, 128, 256, 3, 4, 25, 2 } -- frames per scroll step

local function rows(ctx) return math.tointeger(ctx.params.rows) or 64 end

local function powered(ctx)
  local v = ctx.analog.voltage("VCC")
  return v == nil or v >= 1.65
end

local function reset_state(s)
  s.ram = {}
  for i = 0, 1023 do s.ram[i] = 0 end
  s.on, s.on_at, s.invert, s.allon, s.contrast = false, nil, false, false, 0x7F
  s.mode, s.page, s.col, s.c0, s.c1, s.p0, s.p1 = 2, 0, 0, 0, 127, 0, 7
  s.segremap, s.comdec, s.mux, s.offset, s.startline = false, false, 64, 0, 0
  s.compins, s.clockdiv, s.precharge, s.pump = 0x12, 0x80, 0x22, false
  s.scroll, s.vfixed, s.vrows = nil, 0, 64
end

-- frames per second the panel scans at
local function frame_rate(s)
  local d = (s.clockdiv & 0x0F) + 1
  local k = (s.precharge & 0x0F) + (s.precharge >> 4) + 50
  return FOSC / (d * k * s.mux)
end

-- the scroll as it stands now: columns shifted and rows moved
local function scroll_now(ctx, s)
  local sc = s.scroll
  if not sc or not sc.active then return 0, 0 end
  local frames = (ctx.now() - sc.since) * frame_rate(s) / 1e12
  local steps = math.floor(frames / INTERVAL[sc.interval])
  return steps * sc.dir, steps * sc.voff
end

-- RAM bit at (column, row) as the scroll (hs columns, vs rows) shows it
local function ram_bit(s, c, r, hs, vs)
  local sc = s.scroll
  if sc and sc.active then
    local page = r // 8
    if sc.vertical and r >= s.vfixed and r < s.vfixed + s.vrows then
      r = s.vfixed + ((r - s.vfixed + vs) % s.vrows)
      page = r // 8
    end
    if page >= sc.p0 and page <= sc.p1 then c = (c - hs) % 128 end
  end
  return (s.ram[(r // 8) * 128 + c] >> (r % 8)) & 1
end

-- bake an active scroll into RAM (2Eh leaves the RAM scrolled)
local function bake(ctx, s)
  local ram = {}
  local hs, vs = scroll_now(ctx, s)
  for r = 0, 63 do
    for c = 0, 127 do
      local i = (r // 8) * 128 + c
      ram[i] = (ram[i] or 0) | (ram_bit(s, c, r, hs, vs) << (r % 8))
    end
  end
  s.ram = ram
end

-- the panel position COM n drives, given the COM pins configuration and
-- how the panel is wired (alternative for 64 rows, sequential for 32)
local function panel_pos(ctx, s, n)
  if (s.compins & 0x20) ~= 0 then n = (n + 32) % 64 end -- left/right remap
  local alt = (s.compins & 0x10) ~= 0
  local wired_alt = rows(ctx) == 64
  if alt == wired_alt then return n end
  if wired_alt then return (n % 32) * 2 + n // 32 end -- sequential on an alternative panel
  return (n % 2) * 32 + n // 2 -- alternative on a sequential panel
end

local function lit(ctx, s)
  return s.on and (s.pump or ctx.params.external_vcc) and s.on_at and ctx.now() >= s.on_at
end

-- the visible image: img[y][x] = 0 or 1
local function image(ctx, s)
  local R = rows(ctx)
  local img = {}
  for y = 0, R - 1 do img[y] = {} for x = 0, 127 do img[y][x] = 0 end end
  if not lit(ctx, s) then return img end
  local hs, vs = scroll_now(ctx, s)
  for k = 0, s.mux - 1 do
    local r = (s.startline + k) % 64
    local n = ((s.comdec and (s.mux - 1 - k) or k) + s.offset) % 64
    local p = panel_pos(ctx, s, n)
    if p < R then
      local y = R - 1 - p
      for c = 0, 127 do
        local seg = s.segremap and (127 - c) or c
        local x = 127 - seg
        local b = s.allon and 1 or ram_bit(s, c, r, hs, vs)
        if s.invert then b = 1 - b end
        img[y][x] = b
      end
    end
  end
  return img
end

local function cmd(ctx, s, c)
  s.ver = (s.ver or 0) + 1
  if s.pending then
    s.args[#s.args + 1] = c
    if #s.args < s.need then return end
    local op, a = s.pending, s.args
    s.pending = nil
    if op == 0x81 then s.contrast = a[1]
    elseif op == 0x20 then s.mode = a[1] & 3
    elseif op == 0x21 then s.c0, s.c1, s.col = a[1] & 127, a[2] & 127, a[1] & 127
    elseif op == 0x22 then s.p0, s.p1, s.page = a[1] & 7, a[2] & 7, a[1] & 7
    elseif op == 0xA8 then if (a[1] & 63) >= 15 then s.mux = (a[1] & 63) + 1 end
    elseif op == 0xD3 then s.offset = a[1] & 63
    elseif op == 0xD5 then s.clockdiv = a[1]
    elseif op == 0xD9 then s.precharge = a[1]
    elseif op == 0xDA then s.compins = a[1] & 0x32
    elseif op == 0x8D then s.pump = (a[1] & 0x04) ~= 0
    elseif op == 0x26 or op == 0x27 then
      s.scroll = { dir = op == 0x26 and 1 or -1, p0 = a[2] & 7, interval = a[3] & 7, p1 = a[4] & 7, voff = 0, vertical = false }
    elseif op == 0x29 or op == 0x2A then
      s.scroll = { dir = op == 0x29 and 1 or -1, p0 = a[2] & 7, interval = a[3] & 7, p1 = a[4] & 7, voff = a[5] & 63, vertical = true }
    elseif op == 0xA3 then s.vfixed, s.vrows = a[1] & 63, a[2] & 127
    end
    return
  end
  if ARGS[c] then s.pending, s.need, s.args = c, ARGS[c], {} return end
  if c == 0xAE then s.on, s.on_at = false, nil
  elseif c == 0xAF then
    if not s.on then s.on, s.on_at = true, ctx.now() + 100 * MS end
  elseif c == 0xA6 then s.invert = false elseif c == 0xA7 then s.invert = true
  elseif c == 0xA4 then s.allon = false elseif c == 0xA5 then s.allon = true
  elseif c == 0xA0 then s.segremap = false elseif c == 0xA1 then s.segremap = true
  elseif c == 0xC0 then s.comdec = false elseif c == 0xC8 then s.comdec = true
  elseif c >= 0x40 and c <= 0x7F then s.startline = c & 63
  elseif c >= 0xB0 and c <= 0xB7 then s.page = c & 7
  elseif c <= 0x0F then s.col = (s.col & 0xF0) | c
  elseif c >= 0x10 and c <= 0x1F then s.col = (s.col & 0x0F) | ((c & 0x0F) << 4)
  elseif c == 0x2F then if s.scroll then s.scroll.active, s.scroll.since = true, ctx.now() end
  elseif c == 0x2E then
    if s.scroll and s.scroll.active then bake(ctx, s) end
    if s.scroll then s.scroll.active = false end
  end
end

local function data(s, b)
  s.ver = (s.ver or 0) + 1
  s.ram[s.page * 128 + s.col] = b
  if s.mode == 2 then
    s.col = (s.col + 1) & 127
  elseif s.mode == 0 then
    s.col = s.col + 1
    if s.col > s.c1 then s.col = s.c0; s.page = s.page + 1; if s.page > s.p1 then s.page = s.p0 end end
  else
    s.page = s.page + 1
    if s.page > s.p1 then s.page = s.p0; s.col = s.col + 1; if s.col > s.c1 then s.col = s.c0 end end
  end
end

function M.create(ctx)
  local s = ctx.state
  reset_state(s)
  ctx.bus.i2c_slave(ctx.params.address, {
    start = function(ctx, dir)
      if dir == "read" or not powered(ctx) then return false end -- write-only over I2C
      s.ctl, s.cont = nil, true
      return true
    end,
    write = function(ctx, b)
      if s.ctl == nil or not s.cont then s.ctl, s.cont = b, (b & 0x80) == 0 return end
      if (s.ctl & 0x40) ~= 0 then data(s, b) else cmd(ctx, s, b) end
      if (s.ctl & 0x80) ~= 0 then s.ctl = nil end
    end,
  })
end

function M.reset(ctx)
  reset_state(ctx.state)
  ctx.state.ver, ctx.state.cache_key = (ctx.state.ver or 0) + 1, nil
end

function M.observe(ctx)
  local s = ctx.state
  -- the image changes only with the RAM and settings, the panel lighting
  -- and the scroll's steps: rebuild it only then
  local hs, vs = scroll_now(ctx, s)
  local key = string.format("%d/%s/%d/%d", s.ver or 0, tostring(lit(ctx, s)), hs, vs)
  if s.cache_key == key then return s.cache end
  local img = image(ctx, s)
  local R = rows(ctx)
  local parts, count, h = {}, 0, 0x811C9DC5
  for y = 0, R - 1 do
    local row = img[y]
    for x0 = 0, 127, 8 do
      local byte = 0
      for i = 0, 7 do byte = (byte << 1) | row[x0 + i] end
      h = ((h ~ byte) * 0x01000193) & 0xFFFFFFFF
    end
    local x = 0
    while x < 128 do
      if row[x] == 1 then
        local x0 = x
        while x < 128 and row[x] == 1 do x = x + 1 end
        count = count + (x - x0)
        parts[#parts + 1] = string.format("M%d %dh%dv1h-%dz", x0, y, x - x0, x - x0)
      else
        x = x + 1
      end
    end
  end
  s.cache_key = key
  s.cache = { on = lit(ctx, s) and true or false, lit_pixels = count, pixels = table.concat(parts), contrast = s.contrast,
    inverted = s.invert, checksum = string.format("%08x", h), scrolling = (s.scroll and s.scroll.active) and true or false }
  return s.cache
end

return M
