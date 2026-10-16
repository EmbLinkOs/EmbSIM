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
local SEGCHAR = {[126] = "0", [48] = "1", [109] = "2", [121] = "3", [51] = "4", [91] = "5", [95] = "6", [112] = "7", [127] = "8", [123] = "9", [119] = "A", [31] = "b", [78] = "C", [61] = "d", [79] = "E", [71] = "F", [1] = "-", [55] = "H", [14] = "L", [103] = "P", [62] = "U"}
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
      s.sr = ((s.sr << 1) | (high(ctx, "DIN") and 1 or 0)) & 0xFFFF
      out(ctx, "DOUT", (s.sr >> 15) & 1 == 1)
    end
    s.clk = c
  elseif pin == "CS" then
    local l = v(ctx, "CS")
    if s.load == 0 and l == 1 then latch(ctx) end
    s.load = l
  end
end
local function text(s)
  if s.test then return "88888888" end
  if not s.on then return "        " end
  local t = ""
  for d = 8, 1, -1 do
    local ch = " "
    if d - 1 <= s.scan then
      local x = s.digits[d]
      if (s.decode >> (d - 1)) & 1 == 1 then
        local k = (x & 0x0F) + 1
        ch = CODEB:sub(k, k)
      else
        ch = SEGCHAR[x & 0x7F] or ((x & 0x7F) == 0 and " " or "?")
      end
    end
    t = t .. ch
  end
  return t
end
function M.observe(ctx)
  local s = ctx.state
  return { text = text(s), on = s.on, intensity = s.intensity }
end
return M
