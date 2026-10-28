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
  local n, lit = 0, {}
  local k = v(ctx, "K")
  for i = 1, 8 do
    lit[i] = v(ctx, "A" .. i) == 1 and k == 0
    if lit[i] then n = n + 1 end
  end
  ctx.state.lit, ctx.state.n = lit, n
end
function M.reset(ctx) eval(ctx) end
function M.on_input(ctx) eval(ctx) end
function M.observe(ctx)
  local o = { level = ctx.state.n }
  for i = 1, 8 do o["lit" .. i] = ctx.state.lit[i] end
  return o
end
return M
