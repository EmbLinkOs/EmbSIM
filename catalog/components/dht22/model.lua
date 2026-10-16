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

local US = 1000000
function M.reset(ctx) ctx.state.busy, ctx.state.low_at, ctx.state.count = false, nil, 0; ctx.release("DATA") end
local function frame(ctx)
  local h = math.floor((ctx.input("humidity") or 50) * 10 + 0.5)
  local t = math.floor((ctx.input("temperature") or 25) * 10 + 0.5)
  if t < 0 then t = 0x8000 | (-t) end
  local bytes = { h >> 8, h & 0xFF, t >> 8, t & 0xFF }
  bytes[5] = (bytes[1] + bytes[2] + bytes[3] + bytes[4]) & 0xFF
  local seq = { { 0, 80 }, { 1, 80 } }
  for _, b in ipairs(bytes) do
    for i = 7, 0, -1 do
      seq[#seq + 1] = { 0, 50 }
      seq[#seq + 1] = { 1, ((b >> i) & 1) == 1 and 70 or 26 }
    end
  end
  seq[#seq + 1] = { 0, 50 }
  return seq
end
function M.on_input(ctx, pin)
  local s = ctx.state
  if pin ~= "DATA" or s.busy then return end
  local l = ctx.pins.DATA.level
  if l == 0 then s.low_at = ctx.now()
  elseif s.low_at and ctx.now() - s.low_at >= 800 * US then        -- the host's start signal (1 ms or more)
    s.low_at, s.busy, s.seq, s.i = nil, true, frame(ctx), 0
    ctx.wake_at(ctx.now() + 30 * US, 0)
  else s.low_at = nil end
end
function M.on_time(ctx)
  local s = ctx.state
  s.i = s.i + 1
  local seg = s.seq[s.i]
  if not seg then
    ctx.release("DATA"); s.busy = false; s.count = s.count + 1
    return
  end
  if seg[1] == 0 then ctx.drive("DATA", 0) else ctx.release("DATA") end
  ctx.wake_at(ctx.now() + seg[2] * US, 0)
end
function M.observe(ctx) return { readings = ctx.state.count } end
return M
