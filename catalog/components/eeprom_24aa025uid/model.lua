-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- Microchip 24AA025UID: a 2-Kbit (256 x 8) I2C serial EEPROM whose upper
-- half is permanently write-protected and holds a 32-bit serial number
-- (DS20005202), as a real part shows it on its bus (sigrok's captures of
-- one, tests/captures/24aa025uid):
--
--   - address 0x50 + A2..A0 as wired; one word-address byte;
--   - byte and page writes committed at STOP, in 16-byte pages: past a
--     page's end the address wraps to its start and later bytes overwrite
--     earlier ones; a repeated START instead of a STOP writes nothing; an
--     address with no data only moves the pointer (a random read);
--   - the self-timed write cycle, during which the part does not
--     acknowledge its address (ACK polling); the real part was busy for
--     between 3.10 and 4.03 ms (the datasheet's maximum is 5 ms);
--   - the upper half (80h-FFh) acknowledges writes and keeps its contents:
--     FFh up to F9h, then the manufacturer code 29h, the device code 41h
--     and the serial number, most significant byte first;
--   - sequential reads roll over from FFh to 00h; the lower half ships
--     erased (FFh), or holds `contents` as a programmer left it.
--
-- Not modelled: endurance and retention, the bus speed limits (400 kHz;
-- faster clocks are decoded too), the contents kept between sessions.
local M = {}
local PAGE = 16

local function view(s)
  local t = {}
  for i = 0, 255 do t[#t + 1] = string.format("%02X", s.mem[i]) end
  return table.concat(t)
end

local function power_on(ctx)
  local s = ctx.state
  s.mem = {}
  for i = 0, 255 do s.mem[i] = 0xFF end
  local hex = (ctx.params.contents or ""):gsub("[%s,]", "")
  if #hex % 2 ~= 0 or #hex > 256 or hex:find("[^0-9a-fA-F]") then
    error("contents: up to 128 bytes in hexadecimal (the lower half; the upper half is the part's own)")
  end
  for i = 0, #hex // 2 - 1 do s.mem[i] = tonumber(hex:sub(2 * i + 1, 2 * i + 2), 16) end
  local sn = math.floor(ctx.params.serial) & 0xFFFFFFFF
  s.mem[0xFA], s.mem[0xFB] = 0x29, 0x41
  for i = 0, 3 do s.mem[0xFC + i] = (sn >> (8 * (3 - i))) & 0xFF end
  s.ptr, s.busy_until, s.writes, s.cycles = 0, -1, 0, 0
  s.phase, s.buf = nil, nil
end

function M.create(ctx)
  local s = ctx.state
  power_on(ctx)
  ctx.bus.i2c_slave(0x50, {
    start = function(ctx, dir)
      s.buf = nil -- data not ended by a STOP writes nothing
      if ctx.now() < s.busy_until then return false end -- write cycle: no ACK
      s.phase = dir == "write" and 0 or nil
    end,
    write = function(ctx, b)
      if s.phase == 0 then s.ptr, s.phase, s.buf = b, 1, {} return end
      s.buf[#s.buf + 1] = { s.ptr, b }
      s.ptr = (s.ptr & ~(PAGE - 1)) | ((s.ptr + 1) & (PAGE - 1))
    end,
    read = function(ctx)
      local b = s.mem[s.ptr]
      s.ptr = (s.ptr + 1) & 0xFF
      return b
    end,
    stop = function(ctx)
      if s.buf and #s.buf > 0 then
        for _, w in ipairs(s.buf) do
          if w[1] < 0x80 then s.mem[w[1]] = w[2]; s.writes = s.writes + 1 end
        end
        s.cycles = s.cycles + 1
        s.busy_until = ctx.now() + math.floor(ctx.params.twc * 1e12 + 0.5)
      end
      s.buf, s.phase = nil, nil
    end }, { address_pins = { "A0", "A1", "A2" } })
end

function M.reset(ctx)
  power_on(ctx)
end

function M.observe(ctx)
  local s = ctx.state
  return { dump = view(s), writes = s.writes, cycles = s.cycles, busy = ctx.now() < s.busy_until }
end
return M
