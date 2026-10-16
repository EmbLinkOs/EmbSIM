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

local function blank() local t = {} for i = 0, 127 do t[i] = 32 end return t end
function M.reset(ctx)
  local s = ctx.state
  s.dd, s.ac, s.inc, s.shift, s.on, s.cursor, s.blink = blank(), 0, true, 0, false, false, false
  s.eight, s.lines2, s.nib, s.half, s.e, s.cg, s.reading = true, true, false, 0, v(ctx, "E"), false, false
end
local function bus8(ctx)
  local b = 0
  for i = 0, 7 do if high(ctx, "D" .. i) then b = b | (1 << i) end end
  return b
end
local function bus4(ctx)
  local b = 0
  for i = 4, 7 do if high(ctx, "D" .. i) then b = b | (1 << (i - 4)) end end
  return b
end
local function step(s)
  if s.inc then s.ac = s.ac + 1 else s.ac = s.ac - 1 end
  if s.lines2 then
    if s.ac == 0x28 then s.ac = 0x40 elseif s.ac == 0x68 then s.ac = 0x00
    elseif s.ac == -1 then s.ac = 0x67 elseif s.ac == 0x3F then s.ac = 0x27 end
  else s.ac = s.ac % 80 end
end
local function command(ctx, c)
  local s = ctx.state
  if c == 0x01 then s.dd, s.ac, s.inc, s.shift, s.cg = blank(), 0, true, 0, false
  elseif c & 0xFE == 0x02 then s.ac, s.shift, s.cg = 0, 0, false
  elseif c & 0xFC == 0x04 then s.inc, s.autoshift = (c & 2) ~= 0, (c & 1) ~= 0
  elseif c & 0xF8 == 0x08 then s.on, s.cursor, s.blink = (c & 4) ~= 0, (c & 2) ~= 0, (c & 1) ~= 0
  elseif c & 0xF0 == 0x10 then
    if (c & 8) ~= 0 then s.shift = s.shift + (((c & 4) ~= 0) and -1 or 1) else step(s) end
  elseif c & 0xE0 == 0x20 then
    s.eight, s.lines2 = (c & 0x10) ~= 0, (c & 0x08) ~= 0
    s.half = 0
  elseif c & 0xC0 == 0x40 then s.cg = true
  elseif c & 0x80 == 0x80 then s.ac, s.cg = c & 0x7F, false end
end
local function data(ctx, d)
  local s = ctx.state
  if s.cg then return end          -- CGRAM contents are kept by the real part; not drawn here
  s.dd[s.ac] = d
  step(s)
  if s.autoshift then s.shift = s.shift + (s.inc and 1 or -1) end
end
local function transfer(ctx, rs, b)
  if rs then data(ctx, b) else command(ctx, b) end
end
function M.on_input(ctx, pin)
  local s = ctx.state
  if pin ~= "E" then return end
  local e = v(ctx, "E")
  local rw = high(ctx, "RW")
  if s.e ~= 1 and e == 1 and rw and not high(ctx, "RS") then
    -- read the busy flag (never busy) and the address counter
    local val = s.ac & 0x7F
    if s.eight then for i = 0, 7 do ctx.drive("D" .. i, (val >> i) & 1) end
    else
      local nib = s.half == 0 and (val >> 4) or (val & 15)
      for i = 4, 7 do ctx.drive("D" .. i, (nib >> (i - 4)) & 1) end
    end
    s.reading = true
  elseif s.e == 1 and e == 0 then
    if s.reading then
      for i = 0, 7 do ctx.release("D" .. i) end
      s.reading = false
      if not s.eight then s.half = 1 - s.half end
    elseif not rw then
      local rs = high(ctx, "RS")
      if s.eight then transfer(ctx, rs, bus8(ctx))
      elseif s.half == 0 then s.hi, s.half = bus4(ctx), 1
      else s.half = 0; transfer(ctx, rs, (s.hi << 4) | bus4(ctx)) end
    end
  end
  s.e = e
end
local function line(s, base)
  if not s.on then return string.rep(" ", 16) end
  local t = {}
  for i = 0, 15 do
    local a = (i + s.shift) % 40 + base
    local c = s.dd[a] or 32
    if c < 8 then c = 35 elseif c < 32 or c > 126 then c = 63 end
    t[#t + 1] = string.char(c)
  end
  return table.concat(t)
end
function M.observe(ctx)
  local s = ctx.state
  return { line1 = line(s, 0x00), line2 = s.lines2 and line(s, 0x40) or string.rep(" ", 16), on = s.on,
           cursor = s.ac, four_bit = not s.eight }
end
return M
