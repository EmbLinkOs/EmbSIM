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

local CODEB = "0123456789-EHLP "
-- segment patterns (no-decode mode: bit 6 = A ... bit 0 = G, bit 7 = DP) read as
-- the characters a person sees on the digit; any other lit pattern is shown as
-- "?" in the text, and exactly in `segments`
local SEGCHAR = {[126] = "0", [48] = "1", [109] = "2", [121] = "3", [51] = "4", [91] = "5", [95] = "6", [112] = "7", [127] = "8", [123] = "9", [119] = "A", [31] = "b", [78] = "C", [61] = "d", [79] = "E", [71] = "F", [1] = "-", [55] = "H", [14] = "L", [103] = "P", [62] = "U",
  [13] = "c", [5] = "r", [28] = "u", [16] = "i", [21] = "n", [29] = "o", [23] = "h", [8] = "_", [94] = "G", [60] = "J", [59] = "y", [15] = "t"}
-- code-B font (datasheet Table 5): the segment patterns of 0-9, -, E, H, L, P and blank
local CODEB_SEG = {126, 48, 109, 121, 51, 91, 95, 112, 127, 123, 1, 79, 55, 14, 103, 0}
function M.reset(ctx)
  local s = ctx.state
  s.sr, s.clk, s.load = 0, v(ctx, "CLK"), v(ctx, "CS")
  s.digits = {0, 0, 0, 0, 0, 0, 0, 0}
  s.decode, s.intensity, s.scan, s.on, s.test = 0, 0, 0, false, false   -- power-up: shutdown
  out(ctx, "DOUT", false)
end
local function latch(ctx)
  local s = ctx.state
  local addr, data = (s.sr >> 8) & 0x0F, s.sr & 0xFF
  if addr >= 1 and addr <= 8 then s.digits[addr] = data
  elseif addr == 9 then s.decode = data
  elseif addr == 10 then s.intensity = data & 0x0F
  elseif addr == 11 then s.scan = data & 0x07
  elseif addr == 12 then s.on = (data & 1) == 1
  elseif addr == 15 then s.test = (data & 1) == 1 end
end
function M.on_input(ctx, pin)
  local s = ctx.state
  if pin == "CLK" then
    local c = v(ctx, "CLK")
    if s.clk == 0 and c == 1 then
      -- DIN is shifted in on CLK's rising edge (datasheet Figure 1)
      s.sr = ((s.sr << 1) | (high(ctx, "DIN") and 1 or 0)) & 0xFFFF
    elseif s.clk == 1 and c == 0 then
      -- DOUT presents bit 15 on the falling edge, for the next device's rising edge
      out(ctx, "DOUT", (s.sr >> 15) & 1 == 1)
    end
    s.clk = c
  elseif pin == "CS" then
    local l = v(ctx, "CS")
    if s.load == 0 and l == 1 then latch(ctx) end
    s.load = l
  end
end
-- the eight digits' segment patterns, DIG7 (left) to DIG0 (right), as lit
local function patterns(s)
  local t = {}
  for d = 8, 1, -1 do
    local x = 0
    if s.test then x = 0xFF
    elseif s.on and d - 1 <= s.scan then
      x = s.digits[d]
      if (s.decode >> (d - 1)) & 1 == 1 then x = CODEB_SEG[(x & 0x0F) + 1] | (x & 0x80) end
    end
    t[#t + 1] = x
  end
  return t
end
local function text(s)
  local t = ""
  for _, x in ipairs(patterns(s)) do
    local seg = x & 0x7F
    t = t .. (seg == 0 and " " or (SEGCHAR[seg] or "?")) .. ((x & 0x80) ~= 0 and "." or "")
  end
  return t
end
local function hexes(s)
  local t = {}
  for _, x in ipairs(patterns(s)) do t[#t + 1] = string.format("%02x", x) end
  return table.concat(t, " ")
end
function M.observe(ctx)
  local s = ctx.state
  return { text = text(s), segments = hexes(s), on = s.on, intensity = s.intensity, scan_limit = s.scan }
end
return M
