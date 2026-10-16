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

local WINDOW = 50000000000      -- 50 ms without an edge: silent (passive buzzer)
local function active_on(ctx) return v(ctx, "+") == 1 and v(ctx, "-") ~= 1 end
function M.reset(ctx) ctx.state.last, ctx.state.period, ctx.state.on = nil, nil, false; ctx.state.lvl = v(ctx, "+") end
function M.on_input(ctx, pin)
  local s = ctx.state
  if ctx.params.kind == "active" then s.on = active_on(ctx); return end
  if pin ~= "+" then return end
  local l = v(ctx, "+")
  if s.lvl == 0 and l == 1 then
    local now = ctx.now()
    if s.last then s.period = now - s.last end
    s.last, s.on = now, s.period ~= nil
    ctx.wake_at(now + WINDOW, 0)
  end
  s.lvl = l
end
function M.on_time(ctx) ctx.state.on, ctx.state.period, ctx.state.last = false, nil, nil end
function M.observe(ctx)
  local s = ctx.state
  local hz = 0
  if ctx.params.kind == "active" then hz = s.on and ctx.params.tone or 0
  elseif s.on and s.period and s.period > 0 then hz = 1e12 / s.period end
  return { sounding = s.on, tone_hz = hz }
end
return M
