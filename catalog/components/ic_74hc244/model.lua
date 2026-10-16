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
  for g = 1, 2 do
    local en = low(ctx, g .. "OE_N")
    for i = 0, 3 do
      local y = g .. "Y" .. i
      if en then
        local a = v(ctx, g .. "A" .. i)
        if a == 0 or a == 1 then out(ctx, y, a == 1) else ctx.drive(y, "x") end
      else ctx.release(y) end
    end
  end
end
function M.reset(ctx) eval(ctx) end
function M.on_input(ctx) eval(ctx) end
return M
