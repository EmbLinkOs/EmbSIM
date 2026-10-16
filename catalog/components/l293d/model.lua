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
  local st = {}
  for _, g in ipairs({ { "EN12", "1", "2" }, { "EN34", "3", "4" } }) do
    local en = high(ctx, g[1])
    for _, k in ipairs({ g[2], g[3] }) do
      if en then ctx.drive(k .. "Y", v(ctx, k .. "A") == 1 and 1 or 0) else ctx.release(k .. "Y") end
    end
    local a, b = v(ctx, g[2] .. "A"), v(ctx, g[3] .. "A")
    st[#st + 1] = not en and "coast" or (a == b and "brake" or (a == 1 and "forward" or "reverse"))
  end
  ctx.state.m1, ctx.state.m2 = st[1], st[2]
end
function M.reset(ctx) eval(ctx) end
function M.on_input(ctx) eval(ctx) end
function M.observe(ctx) return { motor1 = ctx.state.m1, motor2 = ctx.state.m2 } end
return M
