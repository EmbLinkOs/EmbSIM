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

local function show(ctx)
  local n = ctx.state.n
  for i = 0, 9 do out(ctx, "Q" .. i, n == i) end
  out(ctx, "CO", n < 5)
end
function M.reset(ctx) ctx.state.n = 0; ctx.state.cp = v(ctx, "CP0"); show(ctx) end
function M.on_input(ctx, pin)
  local s = ctx.state
  if high(ctx, "MR") then s.n = 0; show(ctx) end
  if pin == "CP0" then
    local c = v(ctx, "CP0")
    if s.cp == 0 and c == 1 and not high(ctx, "MR") and not high(ctx, "CP1_N") then
      s.n = (s.n + 1) % 10
      show(ctx)
    end
    s.cp = c
  end
end
function M.observe(ctx) return { count = ctx.state.n } end
return M
