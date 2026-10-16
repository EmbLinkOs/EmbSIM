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

local CHARS = {["abcdef"] = "0", ["bc"] = "1", ["abdeg"] = "2", ["abcdg"] = "3", ["bcfg"] = "4", ["acdfg"] = "5", ["acdefg"] = "6", ["abc"] = "7", ["abcdefg"] = "8", ["abcdfg"] = "9", ["abcefg"] = "A", ["cdefg"] = "b", ["adef"] = "C", ["bcdeg"] = "d", ["adefg"] = "E", ["aefg"] = "F", ["g"] = "-", ["bcefg"] = "H", ["def"] = "L", ["abefg"] = "P", ["bcdef"] = "U"}
local SEGS = {"A", "B", "C", "D", "E", "F", "G", "DP"}
local function eval(ctx)
  local cathode = ctx.params.common ~= "anode"
  local com = v(ctx, "COM")
  local lit, key = {}, ""
  for _, s in ipairs(SEGS) do
    local x = v(ctx, s)
    local on
    if cathode then on = x == 1 and com ~= 1 else on = x == 0 and com ~= 0 end
    lit[s] = on
    if on and s ~= "DP" then key = key .. s:lower() end
  end
  ctx.state.lit = lit
  ctx.state.char = key == "" and " " or (CHARS[key] or "?")
end
function M.reset(ctx) eval(ctx) end
function M.on_input(ctx) eval(ctx) end
function M.observe(ctx)
  local o = { char = ctx.state.char }
  for s, on in pairs(ctx.state.lit) do o["seg_" .. s:lower()] = on end
  return o
end
return M
