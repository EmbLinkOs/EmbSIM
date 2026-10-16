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

function M.reset(ctx) ctx.state.q = 0; ctx.state.clk = v(ctx, "CLK"); out(ctx, "Q", false); out(ctx, "QN", true) end
local function show(ctx) out(ctx, "Q", ctx.state.q == 1); out(ctx, "QN", ctx.state.q == 0) end
function M.on_input(ctx, pin)
  local s = ctx.state
  if low(ctx, "CLR_N") then s.q = 0; show(ctx) return end
  if low(ctx, "PRE_N") then s.q = 1; show(ctx) return end
  if pin == "CLK" then
    local c = v(ctx, "CLK")
    if s.clk == 0 and c == 1 then
      local j, k = high(ctx, "J"), high(ctx, "K")
      if j and k then s.q = 1 - s.q elseif j then s.q = 1 elseif k then s.q = 0 end
      show(ctx)
    end
    s.clk = c
  end
end
return M
