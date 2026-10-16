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

local function dump(mem, n)
  local t = {}
  for i = 0, n - 1 do t[#t + 1] = string.format("%02X", mem[i]) end
  return table.concat(t)
end

function M.create(ctx)
  local s = ctx.state
  s.mem = {} for i = 0, 255 do s.mem[i] = 0xFF end
  s.ptr, s.busy_until, s.writes, s.buf = 0, -1, 0, nil
  ctx.bus.i2c_slave(ctx.params.address, {
    start = function(ctx, dir)
      if ctx.now() < s.busy_until then return false end   -- write cycle in progress: no ACK
      s.first = dir == "write"
    end,
    write = function(ctx, b)
      if s.first then s.ptr, s.first, s.buf = b, false, {} return end
      s.buf[#s.buf + 1] = { s.ptr, b }
      s.ptr = (s.ptr & 0xF8) | ((s.ptr + 1) & 0x07)      -- 8-byte pages wrap
    end,
    read = function(ctx) local b = s.mem[s.ptr]; s.ptr = (s.ptr + 1) & 0xFF; return b end,
    stop = function(ctx)
      if s.buf and #s.buf > 0 then
        if not high(ctx, "WP") then
          for _, w in ipairs(s.buf) do s.mem[w[1]] = w[2]; s.writes = s.writes + 1 end
        end
        s.busy_until = ctx.now() + math.floor(ctx.params.twr * 1e12 + 0.5)
      end
      s.buf = nil
    end })
end
function M.observe(ctx)
  local s = ctx.state
  return { dump = dump(s.mem, 256), writes = s.writes, busy = ctx.now() < s.busy_until }
end
return M
