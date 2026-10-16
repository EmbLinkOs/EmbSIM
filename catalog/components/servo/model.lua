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

function M.reset(ctx) ctx.state.angle, ctx.state.width, ctx.state.lvl, ctx.state.rise = ctx.params.initial, 0, v(ctx, "SIG"), nil end
function M.on_input(ctx, pin)
  local s = ctx.state
  if pin ~= "SIG" then return end
  local l = v(ctx, "SIG")
  if s.lvl ~= 1 and l == 1 then s.rise = ctx.now()
  elseif s.lvl == 1 and l ~= 1 and s.rise then
    local w = (ctx.now() - s.rise) / 1e6                 -- microseconds
    if w >= 400 and w <= 3000 then
      local p = ctx.params
      local a = (w - p.min_pulse * 1e6) / ((p.max_pulse - p.min_pulse) * 1e6) * p.range
      s.angle, s.width = math.max(0, math.min(p.range, a)), w
    end
  end
  s.lvl = l
end
function M.observe(ctx) return { angle = ctx.state.angle, pulse_us = ctx.state.width } end
return M
