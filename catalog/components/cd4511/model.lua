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

local SEG = {[0] = "abcdef", [1] = "bc", [2] = "abdeg", [3] = "abcdg", [4] = "bcfg", [5] = "acdfg", [6] = "acdefg", [7] = "abc", [8] = "abcdefg", [9] = "abcdfg"}
local function show(ctx)
  local lit
  if low(ctx, "LT_N") then lit = "abcdefg"
  elseif low(ctx, "BL_N") then lit = ""
  else lit = SEG[ctx.state.bcd] or "" end
  for c in ("abcdefg"):gmatch(".") do out(ctx, "S" .. c:upper(), lit:find(c, 1, true) ~= nil) end
  ctx.state.lit = lit
end
local function sample(ctx)
  if not high(ctx, "LE") then
    ctx.state.bcd = (high(ctx, "A") and 1 or 0) | (high(ctx, "B") and 2 or 0) | (high(ctx, "C") and 4 or 0) | (high(ctx, "D") and 8 or 0)
  end
end
function M.reset(ctx) ctx.state.bcd = 0; sample(ctx); show(ctx) end
function M.on_input(ctx) sample(ctx); show(ctx) end
function M.observe(ctx) return { digit = ctx.state.bcd, segments = ctx.state.lit } end
return M
