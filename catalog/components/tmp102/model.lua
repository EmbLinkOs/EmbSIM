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

local function reg_temp(t) return (math.floor(t / 0.0625 + 0.5) & 0xFFF) << 4 end
local function temp_of(r) local x = r >> 4; if x >= 0x800 then x = x - 0x1000 end return x * 0.0625 end
local function update_alert(ctx)
  local s = ctx.state
  local t = ctx.state.t or ctx.input("temperature") or 25
  if t >= temp_of(s.regs[3]) then s.alert = true elseif t < temp_of(s.regs[2]) then s.alert = false end
  if s.alert then ctx.drive("ALERT", 0) else ctx.release("ALERT") end
end
function M.create(ctx)
  local s = ctx.state
  s.regs = { [0] = 0, [1] = 0x60A0, [2] = reg_temp(75), [3] = reg_temp(80) }
  s.ptr, s.alert = 0, false
  ctx.bus.i2c_slave(ctx.params.address, {
    start = function(ctx, dir) s.n = 0 end,
    write = function(ctx, b)
      s.n = s.n + 1
      if s.n == 1 then s.ptr = b & 3; s.hi = true
      elseif s.ptr ~= 0 then
        if s.n == 2 then s.msb = b else s.regs[s.ptr] = (s.msb << 8) | b; update_alert(ctx) end
      end
    end,
    read = function(ctx)
      local r = s.ptr == 0 and reg_temp(s.t or ctx.input("temperature") or 25) or s.regs[s.ptr]
      local b = s.hi and (r >> 8) or (r & 0xFF)
      s.hi = not s.hi
      return b
    end })
end
function M.reset(ctx) ctx.state.hi = true; update_alert(ctx) end
function M.on_set(ctx, name, value)
  if name ~= "temperature" or type(value) ~= "number" or value < -55 or value > 150 then return false end
  ctx.state.t = value
  update_alert(ctx)
end
function M.observe(ctx) return { temperature = ctx.state.t or ctx.input("temperature") or 25, alert = ctx.state.alert } end
return M
