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

local ARGS = { [0x81] = 1, [0x20] = 1, [0x21] = 2, [0x22] = 2, [0xA8] = 1, [0xD3] = 1, [0xD5] = 1, [0xD9] = 1, [0xDA] = 1,
               [0xDB] = 1, [0x8D] = 1, [0x26] = 6, [0x27] = 6, [0x29] = 5, [0x2A] = 5, [0xA3] = 2 }
local function clear(s) s.ram = {} for i = 0, 1023 do s.ram[i] = 0 end s.dirty = true end
local function cmd(s, c)
  if s.pending then
    s.args[#s.args + 1] = c
    if #s.args < s.need then return end
    local op, a = s.pending, s.args
    s.pending = nil
    if op == 0x81 then s.contrast = a[1]
    elseif op == 0x20 then s.mode = a[1] & 3
    elseif op == 0x21 then s.c0, s.c1, s.col = a[1] & 127, a[2] & 127, a[1] & 127
    elseif op == 0x22 then s.p0, s.p1, s.page = a[1] & 7, a[2] & 7, a[1] & 7 end
    return
  end
  if ARGS[c] then s.pending, s.need, s.args = c, ARGS[c], {} return end
  if c == 0xAE then s.on = false elseif c == 0xAF then s.on = true
  elseif c == 0xA6 then s.invert = false elseif c == 0xA7 then s.invert = true
  elseif c == 0xA4 then s.allon = false elseif c == 0xA5 then s.allon = true
  elseif c >= 0xB0 and c <= 0xB7 then s.page = c & 7
  elseif c <= 0x0F then s.col = (s.col & 0xF0) | c
  elseif c >= 0x10 and c <= 0x1F then s.col = (s.col & 0x0F) | ((c & 0x0F) << 4) end
  s.dirty = true
end
local function data(s, b)
  s.ram[s.page * 128 + s.col] = b
  s.dirty = true
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
  clear(s)
  s.on, s.invert, s.allon, s.contrast, s.mode = false, false, false, 0x7F, 2
  s.page, s.col, s.c0, s.c1, s.p0, s.p1 = 0, 0, 0, 127, 0, 7
  ctx.bus.i2c_slave(ctx.params.address, {
    start = function(ctx, dir) s.ctl, s.cont = nil, true end,
    write = function(ctx, b)
      if s.ctl == nil or not s.cont then s.ctl, s.cont = b, (b & 0x80) == 0 return end
      if (s.ctl & 0x40) ~= 0 then data(s, b) else cmd(s, b) end
      if (s.ctl & 0x80) ~= 0 then s.ctl = nil end
    end,
    read = function(ctx) return s.on and 0x03 or 0x43 end })
end
local function render(s)
  local parts, lit = {}, 0
  local show = s.on
  for y = 0, 63 do
    local page, bit = y // 8, 1 << (y % 8)
    local x = 0
    while x < 128 do
      local function on(xx)
        local p = s.allon or (s.ram[page * 128 + xx] & bit) ~= 0
        if s.invert then p = not p end
        return p
      end
      if on(x) then
        local x0 = x
        while x < 128 and on(x) do x = x + 1 end
        lit = lit + (x - x0)
        if show then parts[#parts + 1] = string.format("M%d %dh%dv1h-%dz", x0, y, x - x0, x - x0) end
      else x = x + 1 end
    end
  end
  s.path, s.lit, s.dirty = table.concat(parts), lit, false
end
function M.observe(ctx)
  local s = ctx.state
  if s.dirty or s.path == nil then render(s) end
  return { on = s.on, lit_pixels = s.lit, pixels = s.path, contrast = s.contrast, inverted = s.invert }
end
return M
