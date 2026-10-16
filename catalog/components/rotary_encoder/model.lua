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

local CW = { {0, 1}, {0, 0}, {1, 0}, {1, 1} }      -- A leads B
local CCW = { {1, 0}, {0, 0}, {0, 1}, {1, 1} }
local function drive_ab(ctx, a, b) ctx.drive("CLK", a); ctx.drive("DT", b) end
function M.reset(ctx)
  local s = ctx.state
  s.pos, s.target, s.phase = 0, 0, 0
  drive_ab(ctx, 1, 1)
  ctx.drive("SW", 1, "weak")
end
local function step(ctx)
  local s = ctx.state
  if s.pos == s.target then return end
  local seq = s.target > s.pos and CW or CCW
  s.phase = s.phase + 1
  local ab = seq[s.phase]
  drive_ab(ctx, ab[1], ab[2])
  if s.phase == 4 then s.phase = 0; s.pos = s.pos + (s.target > s.pos and 1 or -1) end
  if s.pos ~= s.target or s.phase ~= 0 then ctx.wake_at(ctx.now() + math.floor(ctx.params.step_time * 1e12 / 4 + 0.5), 0) end
end
function M.on_set(ctx, name, value)
  local s = ctx.state
  if name == "position" then
    if math.type(value) ~= "integer" and not (type(value) == "number" and value == math.floor(value)) then return false end
    local was_idle = s.pos == s.target and s.phase == 0
    s.target = math.floor(value)
    if was_idle then step(ctx) end
  elseif name == "pressed" then
    if value then ctx.drive("SW", 0) else ctx.drive("SW", 1, "weak") end
  else return false end
end
function M.on_time(ctx) step(ctx) end
function M.observe(ctx) return { count = ctx.state.pos } end
return M
