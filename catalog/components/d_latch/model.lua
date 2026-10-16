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
  if high(ctx, "LE") then
    local d = v(ctx, "D")
    if d == 0 or d == 1 then ctx.state.q = d end
  end
  out(ctx, "Q", ctx.state.q == 1); out(ctx, "QN", ctx.state.q == 0)
end
function M.reset(ctx) ctx.state.q = 0; eval(ctx) end
function M.on_input(ctx) eval(ctx) end
return M
