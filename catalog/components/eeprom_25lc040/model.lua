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
  s.mem = {} for i = 0, 511 do s.mem[i] = 0xFF end
  s.wel, s.busy_until, s.writes = false, -1, 0
  local function busy(ctx) return ctx.now() < s.busy_until end
  ctx.bus.spi_slave({
    select = function(ctx) s.phase, s.buf, s.after = "cmd", {}, nil; return 0xFF end,
    transfer = function(ctx, b)
      if s.phase == "cmd" then
        s.cmd = b & 0xF7
        s.a8 = (b >> 3) & 1
        if busy(ctx) and s.cmd ~= 0x05 then s.phase = "ignore" return 0xFF end
        if s.cmd == 0x06 then s.after = "wren" elseif s.cmd == 0x04 then s.after = "wrdi"
        elseif s.cmd == 0x05 then s.phase = "status"; return (busy(ctx) and 1 or 0) | (s.wel and 2 or 0)
        elseif s.cmd == 0x03 or s.cmd == 0x02 then s.phase = "addr" end
        return 0xFF
      elseif s.phase == "addr" then
        s.addr = (s.a8 << 8) | b
        s.phase = s.cmd == 0x03 and "read" or "write"
        if s.phase == "read" then local x = s.mem[s.addr]; s.addr = (s.addr + 1) & 511; return x end
        return 0xFF
      elseif s.phase == "read" then
        local x = s.mem[s.addr]; s.addr = (s.addr + 1) & 511; return x
      elseif s.phase == "write" then
        s.buf[#s.buf + 1] = { s.addr, b }
        s.addr = (s.addr & 0x1F0) | ((s.addr + 1) & 0x0F)    -- 16-byte pages wrap
        return 0xFF
      elseif s.phase == "status" then
        return (busy(ctx) and 1 or 0) | (s.wel and 2 or 0)
      end
      return 0xFF
    end,
    deselect = function(ctx)
      if s.after == "wren" then s.wel = true elseif s.after == "wrdi" then s.wel = false end
      if s.phase == "write" and #s.buf > 0 and s.wel and not low(ctx, "WP_N") then
        for _, w in ipairs(s.buf) do s.mem[w[1]] = w[2]; s.writes = s.writes + 1 end
        s.busy_until = ctx.now() + math.floor(ctx.params.twc * 1e12 + 0.5)
        s.wel = false
      end
    end }, { sck = "SCK", mosi = "SI", miso = "SO", cs = "CS_N" })
end
function M.observe(ctx)
  local s = ctx.state
  return { dump = dump(s.mem, 512), writes = s.writes, write_enabled = s.wel }
end
return M
