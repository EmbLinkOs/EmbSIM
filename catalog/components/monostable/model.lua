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

local function ps(t) return math.max(1, math.floor(t * 1e12 + 0.5)) end
local function show(ctx) out(ctx, "Q", ctx.state.q); out(ctx, "QN", not ctx.state.q) end
function M.reset(ctx)
  ctx.state.q, ctx.state.a, ctx.state.b = false, v(ctx, "A_N"), v(ctx, "B")
  ctx.state.tw = ps(ctx.params.k * ctx.params.rext * ctx.params.cext)
  show(ctx)
end
function M.on_time(ctx) ctx.state.q = false; show(ctx) end
function M.on_input(ctx, pin)
  local s = ctx.state
  local a, b = v(ctx, "A_N"), v(ctx, "B")
  if low(ctx, "CLR_N") then ctx.cancel(0); s.q = false; show(ctx)
  else
    local fire = (pin == "A_N" and s.a == 1 and a == 0 and b == 1) or (pin == "B" and s.b == 0 and b == 1 and a == 0)
    if fire then s.q = true; show(ctx); ctx.wake_at(ctx.now() + s.tw, 0) end   -- retriggerable
  end
  s.a, s.b = a, b
end
function M.observe(ctx) return { pulse = ctx.state.q } end
return M
