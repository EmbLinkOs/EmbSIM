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

local function vin(ctx, ch)
  local p = "CH" .. ch
  if ctx.pins[p].connected then return ctx.pins[p].voltage end
  return ctx.input("ch" .. ch) or 0
end
local function vref(ctx)
  if ctx.pins.VREF.connected then return ctx.pins.VREF.voltage end
  return ctx.params.vref
end
function M.reset(ctx)
  local s = ctx.state
  s.cs, s.clk, s.phase = v(ctx, "CS_N"), v(ctx, "CLK"), "idle"
  s.code, s.channel = 0, 0
  ctx.release("DOUT")
end
local function convert(ctx)
  local s = ctx.state
  local sgl, ch = (s.cfg >> 3) & 1, s.cfg & 7
  local v_in
  if sgl == 1 then v_in = vin(ctx, ch)
  else local a = ch & 6; v_in = (ch & 1 == 0) and (vin(ctx, a) - vin(ctx, a + 1)) or (vin(ctx, a + 1) - vin(ctx, a)) end
  local r = vref(ctx)
  local code = r > 0 and math.floor(1024 * v_in / r) or 0
  s.code, s.channel = math.max(0, math.min(1023, code)), ch
end
function M.on_input(ctx, pin)
  local s = ctx.state
  if pin == "CS_N" then
    local c = v(ctx, "CS_N")
    if c == 0 and s.cs ~= 0 then s.phase, s.n, s.cfg = "start", 0, 0 end
    if c ~= 0 then s.phase = "idle"; ctx.release("DOUT") end
    s.cs = c
  elseif pin == "CLK" and s.phase ~= "idle" then
    local c = v(ctx, "CLK")
    if s.clk == 0 and c == 1 then                       -- rising: sample DIN
      local bit = high(ctx, "DIN") and 1 or 0
      if s.phase == "start" then if bit == 1 then s.phase, s.n = "config", 0 end
      elseif s.phase == "config" then
        s.cfg, s.n = (s.cfg << 1) | bit, s.n + 1
        if s.n == 4 then convert(ctx); s.phase, s.n = "sample", 0 end
      end
    elseif s.clk == 1 and c == 0 then                   -- falling: shift DOUT
      if s.phase == "sample" then ctx.drive("DOUT", 0); s.phase, s.n = "data", 0     -- the null bit
      elseif s.phase == "data" then
        if s.n < 10 then ctx.drive("DOUT", (s.code >> (9 - s.n)) & 1); s.n = s.n + 1 else ctx.drive("DOUT", 0) end
      end
    end
    s.clk = c
  end
end
function M.observe(ctx) return { last_code = ctx.state.code, last_channel = ctx.state.channel } end
return M
