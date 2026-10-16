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
  local s = (high(ctx, "S0") and 1 or 0) | (high(ctx, "S1") and 2 or 0) | (high(ctx, "S2") and 4 or 0)
  local y = not high(ctx, "E_N") and high(ctx, "I" .. s)
  out(ctx, "Y", y); out(ctx, "Y_N", not y)
end
function M.reset(ctx) eval(ctx) end
function M.on_input(ctx) eval(ctx) end
return M
