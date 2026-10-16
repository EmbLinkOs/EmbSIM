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
  local a, b = 0, 0
  for i = 0, 3 do
    if high(ctx, "A" .. i) then a = a | (1 << i) end
    if high(ctx, "B" .. i) then b = b | (1 << i) end
  end
  local s = a + b + (high(ctx, "CIN") and 1 or 0)
  for i = 0, 3 do out(ctx, "S" .. i, (s >> i) & 1 == 1) end
  out(ctx, "COUT", s > 15)
  ctx.state.sum = s
end
function M.reset(ctx) eval(ctx) end
function M.on_input(ctx) eval(ctx) end
function M.observe(ctx) return { sum = ctx.state.sum or 0 } end
return M
