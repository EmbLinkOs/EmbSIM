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

-- parse the pattern once per reset: hexadecimal words separated by commas or spaces
local function parse(s)
  local steps = {}
  for tok in string.gmatch(s or "", "[^,%s]+") do
    local n = tonumber(tok:gsub("^0[xX]", ""), 16)
    if not n then error("pattern: " .. tok .. " is not a hexadecimal word") end
    steps[#steps + 1] = n & 0xFFFF
  end
  if #steps == 0 then steps[1] = 0 end
  return steps
end

local function apply(ctx, word)
  local od = ctx.params.drive == "open_drain"
  local last = ctx.state.word
  for i = 0, 15 do
    local bit = (word >> i) & 1
    if last == nil or ((last >> i) & 1) ~= bit then
      local pin = "OUT" .. i
      if od and bit == 1 then ctx.release(pin) else ctx.drive(pin, bit) end
    end
  end
  ctx.state.word = word
end

function M.reset(ctx)
  ctx.state.steps = parse(ctx.params.pattern)
  ctx.state.i = 1
  ctx.state.word = nil
  ctx.state.period = math.max(1, math.floor(ctx.params.period * 1e12 + 0.5))
  local start = math.floor(ctx.params.start * 1e12 + 0.5)
  apply(ctx, ctx.params.idle)
  ctx.wake_at(start)
end

function M.on_time(ctx)
  local s = ctx.state
  if s.i > #s.steps then
    if not ctx.params["repeat"] then return end
    s.i = 1
  end
  apply(ctx, s.steps[s.i])
  s.i = s.i + 1
  ctx.wake_at(ctx.now() + s.period)
end

function M.observe(ctx) return { step = (ctx.state.i or 1) - 1 } end
return M
