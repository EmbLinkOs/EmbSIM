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

local function load(ctx)
  local d = 0
  for i = 0, 7 do if high(ctx, "D" .. i) then d = d | (1 << i) end end
  ctx.state.sr = d
end
local function show(ctx)
  local b = (ctx.state.sr >> 7) & 1
  out(ctx, "Q7", b == 1); out(ctx, "Q7_N", b == 0)
end
function M.reset(ctx) ctx.state.sr = 0; ctx.state.cp = v(ctx, "CP"); if low(ctx, "PL_N") then load(ctx) end; show(ctx) end
function M.on_input(ctx, pin)
  local s = ctx.state
  if low(ctx, "PL_N") then load(ctx) end
  if pin == "CP" then
    local c = v(ctx, "CP")
    if s.cp == 0 and c == 1 and high(ctx, "PL_N") and not high(ctx, "CE_N") then
      s.sr = ((s.sr << 1) | (high(ctx, "DS") and 1 or 0)) & 0xFF
    end
    s.cp = c
  end
  show(ctx)
end
function M.observe(ctx) return { shift = ctx.state.sr } end
return M
