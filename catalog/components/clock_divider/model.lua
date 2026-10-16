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

function M.reset(ctx) ctx.state.n, ctx.state.clk = 0, v(ctx, "CLK"); out(ctx, "OUT", false) end
function M.on_input(ctx, pin)
  local s = ctx.state
  if pin ~= "CLK" then return end
  local c = v(ctx, "CLK")
  if s.clk == 0 and c == 1 then
    local n = math.max(2, ctx.params.n)
    s.n = (s.n + 1) % n
    out(ctx, "OUT", s.n >= n - n // 2)
  end
  s.clk = c
end
return M
