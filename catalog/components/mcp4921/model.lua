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

function M.reset(ctx)
  local s = ctx.state
  s.cs, s.clk, s.sr, s.n, s.code, s.gain, s.on, s.pending = v(ctx, "CS_N"), v(ctx, "SCK"), 0, 0, 0, 1, false, nil
end
local function vref(ctx)
  if ctx.pins.VREF.connected then return ctx.pins.VREF.voltage end
  return ctx.params.vref
end
local function apply(ctx)
  local s = ctx.state
  if s.pending then s.code, s.gain, s.on = s.pending[1], s.pending[2], s.pending[3]; s.pending = nil end
end
function M.on_input(ctx, pin)
  local s = ctx.state
  if pin == "CS_N" then
    local c = v(ctx, "CS_N")
    if c == 0 and s.cs ~= 0 then s.sr, s.n = 0, 0 end
    if c ~= 0 and s.cs == 0 and s.n >= 16 then
      local w = s.sr & 0xFFFF
      if (w >> 15) == 0 then
        s.pending = { w & 0xFFF, ((w >> 13) & 1) == 1 and 1 or 2, ((w >> 12) & 1) == 1 }
        if not high(ctx, "LDAC_N") then apply(ctx) end
      end
    end
    s.cs = c
  elseif pin == "SCK" then
    local c = v(ctx, "SCK")
    if s.clk == 0 and c == 1 and s.cs == 0 then s.sr, s.n = (s.sr << 1) | (high(ctx, "SDI") and 1 or 0), s.n + 1 end
    s.clk = c
  elseif pin == "LDAC_N" and low(ctx, "LDAC_N") then apply(ctx) end
end
function M.observe(ctx)
  local s = ctx.state
  local vout = s.on and math.min(vref(ctx) * s.gain * s.code / 4096, ctx.params.vdd) or 0
  return { code = s.code, vout = vout, active = s.on }
end
return M
