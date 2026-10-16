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

function M.reset(ctx) ctx.state.trig, ctx.state.rise = v(ctx, "TRIG"), nil; ctx.drive("ECHO", 0); ctx.state.echo_us = 0 end
function M.on_input(ctx, pin)
  local s = ctx.state
  if pin ~= "TRIG" then return end
  local t = v(ctx, "TRIG")
  if s.trig ~= 1 and t == 1 then s.rise = ctx.now()
  elseif s.trig == 1 and t ~= 1 and s.rise and ctx.now() - s.rise >= 10000000 then
    ctx.wake_at(ctx.now() + 250000000, 0)          -- the 40 kHz burst: about 250 us
  end
  s.trig = t
end
function M.on_time(ctx, token)
  local s = ctx.state
  if token == 0 then
    local d = ctx.input("distance_cm") or 100
    local us = (d >= 2 and d <= 400) and math.floor(d * 58.3 + 0.5) or 38000   -- out of range: the 38 ms timeout
    s.echo_us = us
    ctx.drive("ECHO", 1)
    ctx.wake_at(ctx.now() + us * 1000000, 1)
  else ctx.drive("ECHO", 0) end
end
function M.observe(ctx) return { echo_us = ctx.state.echo_us } end
return M
