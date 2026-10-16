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

local function eval(ctx)
  local en = low(ctx, "E1_N") and low(ctx, "E2_N") and high(ctx, "E3")
  local a = (high(ctx, "A0") and 1 or 0) | (high(ctx, "A1") and 2 or 0) | (high(ctx, "A2") and 4 or 0)
  for i = 0, 7 do out(ctx, "Y" .. i .. "_N", not (en and a == i)) end
end
function M.reset(ctx) eval(ctx) end
function M.on_input(ctx) eval(ctx) end
return M
