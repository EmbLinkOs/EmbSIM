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
  local cathode = ctx.params.common ~= "anode"
  local com = v(ctx, "COM")
  local function on(p)
    local x = v(ctx, p)
    if cathode then return x == 1 and com ~= 1 end
    return x == 0 and com ~= 0
  end
  local r, g, b = on("R"), on("G"), on("B")
  ctx.state.r, ctx.state.g, ctx.state.b = r, g, b
  ctx.state.color = string.format("#%02x%02x%02x", r and 255 or 0, g and 255 or 0, b and 255 or 0)
end
function M.reset(ctx) eval(ctx) end
function M.on_input(ctx) eval(ctx) end
function M.observe(ctx)
  local s = ctx.state
  return { red = s.r, green = s.g, blue = s.b, color = s.color, lit = s.r or s.g or s.b }
end
return M
