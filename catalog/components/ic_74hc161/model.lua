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
  local q = ctx.state.q
  for i = 0, 3 do out(ctx, "Q" .. i, (q >> i) & 1 == 1) end
  out(ctx, "TC", q == 15 and high(ctx, "CET"))
end
function M.reset(ctx) ctx.state.q = 0; ctx.state.cp = v(ctx, "CP"); show(ctx) end
function M.on_input(ctx, pin)
  local s = ctx.state
  if low(ctx, "MR_N") then s.q = 0; show(ctx) end
  if pin == "CP" then
    local c = v(ctx, "CP")
    if s.cp == 0 and c == 1 and not low(ctx, "MR_N") then
      if low(ctx, "PE_N") then
        local d = 0
        for i = 0, 3 do if high(ctx, "D" .. i) then d = d | (1 << i) end end
        s.q = d
      elseif high(ctx, "CEP") and high(ctx, "CET") then
        s.q = (s.q + 1) & 15
      end
      show(ctx)
    end
    s.cp = c
  elseif pin == "CET" then show(ctx) end
end
function M.observe(ctx) return { count = ctx.state.q } end
return M
