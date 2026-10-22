-- Microchip 24LC256: 32768 x 8 serial EEPROM on I2C, two address bytes,
-- 64-byte pages, self-timed write cycle (DS20001203).
local M = {}
local SIZE, PAGE = 32768, 64
local function high(ctx, p) return ctx.pins[p].value == 1 end

local function view(s, at, n)
  local t = {}
  for i = 0, n - 1 do t[#t + 1] = string.format("%02X", s.mem[(at + i) % SIZE]) end
  return table.concat(t)
end

function M.create(ctx)
  local s = ctx.state
  s.mem = {} for i = 0, SIZE - 1 do s.mem[i] = 0xFF end
  s.ptr, s.busy_until, s.writes, s.cycles = 0, -1, 0, 0
  s.phase, s.buf = nil, nil
  ctx.bus.i2c_slave(0x50, {
    start = function(ctx, dir)
      -- data not ended by a STOP (a repeated START) starts no write cycle
      s.buf = nil
      if ctx.now() < s.busy_until then return false end   -- write cycle: no ACK (ACK polling)
      s.phase = dir == "write" and 0 or nil
    end,
    write = function(ctx, b)
      if s.phase == 0 then s.ptr, s.phase = ((b & 0x7F) << 8) | (s.ptr & 0xFF), 1 return end  -- A15 is don't care
      if s.phase == 1 then s.ptr, s.phase, s.buf = (s.ptr & 0x7F00) | b, 2, {} return end
      s.buf[#s.buf + 1] = { s.ptr, b }
      -- past the page's end the counter wraps to its start and overwrites
      s.ptr = (s.ptr & ~(PAGE - 1)) | ((s.ptr + 1) & (PAGE - 1))
    end,
    read = function(ctx)
      local b = s.mem[s.ptr]
      s.ptr = (s.ptr + 1) % SIZE                           -- sequential reads roll over 7FFF -> 0000
      return b
    end,
    stop = function(ctx)
      if s.buf and #s.buf > 0 then
        if not high(ctx, "WP") then
          for _, w in ipairs(s.buf) do s.mem[w[1]] = w[2] end
          s.writes = s.writes + #s.buf
        end
        -- the cycle runs (and the part does not answer) whether or not WP blocked it
        s.cycles = s.cycles + 1
        s.busy_until = ctx.now() + math.floor(ctx.params.twr * 1e12 + 0.5)
      end
      s.buf, s.phase = nil, nil
    end }, { address_pins = { "A0", "A1", "A2" } })
end

function M.observe(ctx)
  local s = ctx.state
  local at = math.floor(ctx.params.view_at) % SIZE
  return { view = view(s, at, 64), writes = s.writes, cycles = s.cycles, busy = ctx.now() < s.busy_until }
end
return M
