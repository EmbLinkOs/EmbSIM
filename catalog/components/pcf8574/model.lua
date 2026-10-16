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

local function apply(ctx)
  local s = ctx.state
  for i = 0, 7 do
    local p = "P" .. i
    if (s.latch >> i) & 1 == 1 then ctx.drive(p, 1, "weak") else ctx.drive(p, 0) end
  end
end
local function inputs(ctx)
  local b = 0
  for i = 0, 7 do if ctx.pins["P" .. i].level == 1 then b = b | (1 << i) end end
  return b
end
function M.create(ctx)
  local s = ctx.state
  s.latch, s.seen = 0xFF, nil
  ctx.bus.i2c_slave(ctx.params.address, {
    write = function(ctx, b) s.latch = b; apply(ctx); s.seen = nil; ctx.release("INT_N") end,
    read = function(ctx) local b = inputs(ctx); s.seen = b; ctx.release("INT_N"); return b end })
end
function M.reset(ctx) apply(ctx); ctx.release("INT_N") end
function M.on_input(ctx, pin)
  local s = ctx.state
  if not pin:match("^P%d$") then return end
  local now = inputs(ctx)
  if s.seen and now ~= s.seen then ctx.drive("INT_N", 0) end
end
function M.observe(ctx) return { latch = ctx.state.latch, port = inputs(ctx) } end
return M
