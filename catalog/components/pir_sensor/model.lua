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

function M.reset(ctx) ctx.drive("OUT", 0); ctx.state.on = false end
function M.on_set(ctx, name, value)
  if name ~= "motion" or type(value) ~= "boolean" then return false end
  local hold = math.floor(ctx.params.hold * 1e12 + 0.5)
  if value then
    ctx.cancel(0)
    ctx.drive("OUT", 1)
    ctx.state.on = true
  else
    ctx.wake_at(ctx.now() + hold, 0)
  end
end
function M.on_time(ctx) ctx.drive("OUT", 0); ctx.state.on = false end
function M.observe(ctx) return { active = ctx.state.on } end
return M
