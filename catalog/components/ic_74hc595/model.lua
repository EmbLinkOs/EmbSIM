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

local function show(ctx)
  local s = ctx.state
  local en = not high(ctx, "OE_N")
  for i = 0, 7 do
    local p = "Q" .. i
    if en then out(ctx, p, (s.latch >> i) & 1 == 1) else ctx.release(p) end
  end
  out(ctx, "Q7S", (s.sr >> 7) & 1 == 1)
end
function M.reset(ctx)
  local s = ctx.state
  s.sr, s.latch, s.sh, s.st = 0, 0, v(ctx, "SHCP"), v(ctx, "STCP")
  show(ctx)
end
function M.on_input(ctx, pin)
  local s = ctx.state
  if low(ctx, "MR_N") then s.sr = 0 end
  if pin == "SHCP" then
    local c = v(ctx, "SHCP")
    if s.sh == 0 and c == 1 and not low(ctx, "MR_N") then
      s.sr = ((s.sr << 1) | (high(ctx, "DS") and 1 or 0)) & 0xFF
    end
    s.sh = c
  elseif pin == "STCP" then
    local c = v(ctx, "STCP")
    if s.st == 0 and c == 1 then s.latch = s.sr end
    s.st = c
  end
  show(ctx)
end
function M.observe(ctx) return { shift = ctx.state.sr, latch = ctx.state.latch } end
return M
