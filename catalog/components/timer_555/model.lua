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

local LN3, LN2 = math.log(3), math.log(2)
local function ps(t) return math.max(1, math.floor(t * 1e12 + 0.5)) end
local function set(ctx, hi)
  ctx.state.out = hi
  ctx.drive("OUT", hi and 1 or 0)
  if hi then ctx.release("DIS") else ctx.drive("DIS", 0) end
end
local function running(ctx) return not low(ctx, "RESET") end
function M.reset(ctx)
  local p = ctx.params
  ctx.state.t_high = ps(LN2 * (p.r1 + p.r2) * p.c)
  ctx.state.t_low = ps(LN2 * p.r2 * p.c)
  ctx.state.t_first = ps(LN3 * (p.r1 + p.r2) * p.c)
  ctx.state.t_mono = ps(1.1 * p.r1 * p.c)
  ctx.state.trig = v(ctx, "TRIG")
  set(ctx, false)
  if p.mode == "astable" and running(ctx) then
    set(ctx, true)                       -- the capacitor charges from 0 V: a longer first high
    ctx.wake_at(ctx.state.t_first, 0)
  end
end
function M.on_time(ctx, token)
  local s = ctx.state
  if not running(ctx) then return end
  if ctx.params.mode == "astable" then
    set(ctx, not s.out)
    ctx.wake_at(ctx.now() + (s.out and s.t_high or s.t_low), 0)
  else
    set(ctx, false)
  end
end
function M.on_input(ctx, pin)
  local s = ctx.state
  if pin == "RESET" then
    if not running(ctx) then ctx.cancel(0); set(ctx, false)
    elseif ctx.params.mode == "astable" and not s.out then set(ctx, true); ctx.wake_at(ctx.now() + s.t_high, 0) end
  elseif pin == "TRIG" then
    local t = v(ctx, "TRIG")
    if ctx.params.mode == "monostable" and s.trig ~= 0 and t == 0 and running(ctx) and not s.out then
      set(ctx, true)
      ctx.wake_at(ctx.now() + s.t_mono, 0)
    end
    s.trig = t
  end
end
function M.observe(ctx)
  local s = ctx.state
  return { output = s.out == true, frequency_hz = 1 / ((s.t_high + s.t_low) * 1e-12) }
end
return M
