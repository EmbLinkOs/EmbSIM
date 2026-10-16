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

local MICRO = { [0] = 1, [1] = 2, [2] = 4, [3] = 8, [7] = 16 }
function M.reset(ctx) ctx.state.pos, ctx.state.step = 0, v(ctx, "STEP") end
local function enabled(ctx) return not high(ctx, "ENABLE_N") and not low(ctx, "RESET_N") and not low(ctx, "SLEEP_N") end
local function micro(ctx)
  local m = (high(ctx, "MS1") and 1 or 0) | (high(ctx, "MS2") and 2 or 0) | (high(ctx, "MS3") and 4 or 0)
  return MICRO[m] or 16
end
function M.on_input(ctx, pin)
  local s = ctx.state
  if low(ctx, "RESET_N") then s.pos = 0 end
  if pin ~= "STEP" then return end
  local c = v(ctx, "STEP")
  if s.step == 0 and c == 1 and enabled(ctx) then
    local n = 16 // micro(ctx)                       -- position counts sixteenth steps
    s.pos = s.pos + (high(ctx, "DIR") and n or -n)
  end
  s.step = c
end
function M.observe(ctx)
  local s = ctx.state
  return { position = s.pos / 16, angle_deg = s.pos / 16 * 360 / ctx.params.steps_per_rev, microsteps = micro(ctx) }
end
return M
