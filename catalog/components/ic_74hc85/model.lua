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

local function word(ctx, p)
  local w = 0
  for i = 0, 3 do if high(ctx, p .. i) then w = w | (1 << i) end end
  return w
end
local function eval(ctx)
  local a, b = word(ctx, "A"), word(ctx, "B")
  local gt, eq, lt
  if a > b then gt, eq, lt = true, false, false
  elseif a < b then gt, eq, lt = false, false, true
  else
    eq = high(ctx, "IEQ")
    gt = not eq and high(ctx, "IGT")
    lt = not eq and high(ctx, "ILT")
  end
  out(ctx, "AGTB", gt); out(ctx, "AEQB", eq); out(ctx, "ALTB", lt)
end
function M.reset(ctx) eval(ctx) end
function M.on_input(ctx) eval(ctx) end
return M
