-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- NXP MFRC522 contactless reader (as on the common RC522 module) with a
-- MIFARE Classic 1K card held to its antenna, as NXP's MFRC522 data sheet,
-- ISO/IEC 14443-3 and the MIFARE Classic EV1 1K data sheet describe them.
--
--   - SPI, mode 0: an address byte (bit 7 read, bits 6..1 the register),
--     reads returning the addressed register during the next byte, writes
--     putting every following byte to the same register (the FIFO takes
--     them in turn); NRSTPD (pulled up on the module) low powers the chip
--     down, its rising edge resets it;
--   - the registers the command set needs: CommandReg (Idle, Mem,
--     GenerateRandomID, CalcCRC, Transmit, Receive, Transceive, MFAuthent,
--     SoftReset; PowerDown while resetting), ComIrqReg and DivIrqReg with
--     Set1 writes, ErrorReg, Status2Reg's MFCrypto1On, the 64-byte FIFO with
--     FlushBuffer, ControlReg's RxLastBits, BitFramingReg (StartSend,
--     TxLastBits), CollReg, ModeReg's CRC preset, TxControlReg's antenna
--     drivers, the timer (TAuto, prescaler, reload) and VersionReg (0x92,
--     v2.0); the IRQ pin from ComIEnReg with IRqInv;
--   - the CRC coprocessor computing CRC_A from ModeReg's preset;
--   - the air interface at 106 kbit/s: a frame takes its bits' time, the
--     card answers after the frame delay time (about 91 us), and nothing
--     comes back while the antenna is off or no card is near: the timer
--     then raises TimerIRq at its reload value;
--   - the card, ISO 14443-3 type A: idle until REQA or WUPA (ATQA 0004h),
--     anticollision and SELECT of its 4-byte UID with the BCC (SAK 08h),
--     HLTA to halt (only WUPA wakes it), MFAuthent with key A or B against
--     the sector trailer, then READ and WRITE (4-bit ACK/NAK) as the access
--     bits allow; trailers read back with key A as zeros; the transport
--     configuration (FF 07 80 69, keys FF FF FF FF FF FF) on every sector,
--     key B readable and so not usable to authenticate;
--   - below 2.5 V on VCC it does not answer.
--
-- Not modelled: the Crypto1 cipher on the air (the chip and card are one
-- model; the host sees plaintext after a successful MFAuthent, as it does
-- through a real MFRC522), value blocks' increment/decrement/restore/
-- transfer, other cards (Ultralight, 7-byte UIDs, several cards and their
-- collisions), the antenna's range and the field's effect on the card's
-- power, I2C and UART host interfaces, damage from 5 V on VCC.
local M = {}

local US = 1000000
local FC = 13560000 -- carrier, Hz
local BIT = math.floor(1e12 / 106000 + 0.5) -- ps per bit at 106 kbit/s
local FDT = 91 * US
local TOK_RX, TOK_TIMER, TOK_RESET = 1, 2, 3

-- registers
local CommandReg, ComIEnReg, DivIEnReg, ComIrqReg, DivIrqReg, ErrorReg = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06
local Status1Reg, Status2Reg, FIFODataReg, FIFOLevelReg, ControlReg, BitFramingReg, CollReg = 0x07, 0x08, 0x09, 0x0A, 0x0C, 0x0D, 0x0E
local ModeReg, TxControlReg, CRCResultRegH, CRCResultRegL = 0x11, 0x14, 0x21, 0x22
local TModeReg, TPrescalerReg, TReloadRegH, TReloadRegL, VersionReg = 0x2A, 0x2B, 0x2C, 0x2D, 0x37

local function powered(ctx)
  local v = ctx.analog.voltage("VCC")
  return v == nil or v >= 2.5
end

local function crc_a(bytes, preset)
  local crc = preset
  for _, b in ipairs(bytes) do
    b = b ~ (crc & 0xFF)
    b = (b ~ (b << 4)) & 0xFF
    crc = ((crc >> 8) ~ (b << 8) ~ (b << 3) ~ (b >> 4)) & 0xFFFF
  end
  return crc & 0xFF, crc >> 8
end

local function preset(s)
  return ({ [0] = 0x0000, 0x6363, 0xA671, 0xFFFF })[s.reg[ModeReg] & 3]
end

local function hexbytes(str)
  local t = {}
  for h in tostring(str):gmatch("%x%x") do t[#t + 1] = tonumber(h, 16) end
  return t
end

-- the card: 64 blocks of 16 bytes, block 0 the manufacturer block
local function new_card(uid)
  local mem = {}
  for b = 0, 63 do
    local blk = {}
    for i = 1, 16 do blk[i] = 0 end
    if b % 4 == 3 then
      blk = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x80, 0x69, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
    end
    mem[b] = blk
  end
  local bcc = uid[1] ~ uid[2] ~ uid[3] ~ uid[4]
  mem[0] = { uid[1], uid[2], uid[3], uid[4], bcc, 0x08, 0x04, 0x00, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69 }
  return { uid = uid, bcc = bcc, mem = mem, state = "idle" }
end

local function reset_regs(s)
  s.reg = {}
  for a = 0, 0x3F do s.reg[a] = 0 end
  s.reg[CommandReg] = 0x20 -- RcvOff clear, PowerDown clear after reset
  s.reg[ModeReg], s.reg[0x12], s.reg[0x13], s.reg[TxControlReg] = 0x3F, 0x00, 0x00, 0x80
  s.reg[0x15], s.reg[0x16], s.reg[0x17], s.reg[0x18] = 0x00, 0x10, 0x84, 0x84
  s.reg[ComIEnReg], s.reg[ComIrqReg], s.reg[DivIrqReg] = 0x80, 0x14, 0x00
  s.reg[VersionReg] = 0x92
  s.fifo, s.cmd, s.crypto = {}, 0, false
end

local function antenna_on(s) return (s.reg[TxControlReg] & 0x03) ~= 0 end

local function update_irq(ctx)
  local s = ctx.state
  local active = (s.reg[ComIrqReg] & s.reg[ComIEnReg] & 0x7F) ~= 0 or (s.reg[DivIrqReg] & s.reg[DivIEnReg] & 0x14) ~= 0
  local inv = (s.reg[ComIEnReg] & 0x80) ~= 0
  local level = active and 1 or 0
  if inv then level = 1 - level end
  ctx.drive("IRQ", level)
end

local function irq(ctx, bits)
  local s = ctx.state
  s.reg[ComIrqReg] = s.reg[ComIrqReg] | bits
  update_irq(ctx)
end

local function start_timer(ctx)
  local s = ctx.state
  local pre = ((s.reg[TModeReg] & 0x0F) << 8) | s.reg[TPrescalerReg]
  local reload = (s.reg[TReloadRegH] << 8) | s.reg[TReloadRegL]
  local period = (2 * pre + 1) / FC
  s.timer_at = ctx.now() + math.floor((reload + 1) * period * 1e12 + 0.5)
  ctx.wake_at(s.timer_at, TOK_TIMER)
end

-- the card's answer to a frame (bytes, valid bits in the last), or nil
local function card_answer(ctx, frame, lastbits)
  local s = ctx.state
  local c = s.card
  if not c or not ctx.input("card_present") then return nil end
  local function with_crc(t)
    local lo, hi = crc_a(t, 0x6363)
    t[#t + 1], t[#t + 2] = lo, hi
    return t
  end
  local function crc_ok(t)
    if #t < 3 then return false end
    local body = {}
    for i = 1, #t - 2 do body[i] = t[i] end
    local lo, hi = crc_a(body, 0x6363)
    return lo == t[#t - 1] and hi == t[#t]
  end
  local cmd = frame[1]
  if lastbits == 7 and #frame == 1 then -- short frames: REQA, WUPA
    if cmd == 0x52 or (cmd == 0x26 and c.state == "idle") then
      c.state = "ready"
      return { 0x04, 0x00 }, 0
    end
    return nil
  end
  if c.state == "ready" and cmd == 0x93 then
    if frame[2] == 0x20 and #frame == 2 then
      return { c.uid[1], c.uid[2], c.uid[3], c.uid[4], c.bcc }, 0
    elseif frame[2] == 0x70 and #frame == 9 and crc_ok(frame) then
      for i = 1, 4 do if frame[2 + i] ~= c.uid[i] then return nil end end
      c.state = "active"
      return with_crc({ 0x08 }), 0
    end
    return nil
  end
  if cmd == 0x50 and frame[2] == 0x00 and (c.state == "active" or c.state == "auth") then
    c.state, c.auth = "halt", nil
    return nil
  end
  if c.state == "auth" and crc_ok(frame) then
    local blk = frame[2]
    local sector = blk // 4
    local trailer = c.mem[sector * 4 + 3]
    if blk > 63 or blk // 4 ~= c.auth.sector then return { 0x04 }, 4 end -- NAK
    if cmd == 0x30 then
      local data = {}
      for i = 1, 16 do data[i] = c.mem[blk][i] end
      if blk % 4 == 3 then for i = 1, 6 do data[i] = 0 end end -- key A never reads
      return with_crc(data), 0
    elseif cmd == 0xA0 then
      c.pending_write = blk
      return { 0x0A }, 4 -- ACK
    end
    return { 0x04 }, 4
  end
  if c.state == "auth" and c.pending_write and #frame == 18 and crc_ok(frame) then
    local blk = c.pending_write
    c.pending_write = nil
    for i = 1, 16 do c.mem[blk][i] = frame[i] end
    return { 0x0A }, 4
  end
  return nil
end

-- MFAuthent: the key against the sector trailer, as the card checks it
local function authenticate(ctx, data)
  local s = ctx.state
  local c = s.card
  if not c or not ctx.input("card_present") or (c.state ~= "active" and c.state ~= "auth") or #data < 12 then return false end
  local which, blk = data[1], data[2]
  for i = 1, 4 do if data[8 + i] ~= c.uid[i] then return false end end
  local trailer = c.mem[(blk // 4) * 4 + 3]
  local off = which == 0x60 and 0 or (which == 0x61 and 10 or nil)
  if not off then return false end
  -- the transport configuration leaves key B readable: it cannot authenticate
  if which == 0x61 and trailer[7] == 0xFF and trailer[8] == 0x07 and trailer[9] == 0x80 then return false end
  for i = 1, 6 do if data[2 + i] ~= trailer[off + i] then return false end end
  c.state, c.auth = "auth", { sector = blk // 4 }
  return true
end

local function finish_tx(ctx, frame, lastbits)
  local s = ctx.state
  local bits = (#frame - 1) * 9 + (lastbits == 0 and 9 or lastbits)
  local done = ctx.now() + bits * BIT
  if (s.reg[TModeReg] & 0x80) ~= 0 then
    s.timer_pending = done
  end
  local ans, ansbits = nil, 0
  if antenna_on(s) and powered(ctx) then ans, ansbits = card_answer(ctx, frame, lastbits) end
  s.tx_done = done
  if ans then
    local rxbits = (#ans - 1) * 9 + (ansbits == 0 and 9 or ansbits)
    s.rx = { data = ans, lastbits = ansbits, at = done + FDT + rxbits * BIT }
    ctx.wake_at(s.rx.at, TOK_RX)
  else
    s.rx = nil
  end
  -- the timer starts at the end of the transmission (TAuto)
  ctx.wake_at(done, TOK_TIMER)
end

local function command(ctx, c)
  local s = ctx.state
  s.cmd = c & 0x0F
  s.reg[CommandReg] = (s.reg[CommandReg] & 0xF0) | s.cmd
  if s.cmd == 0x00 then
    s.rx, s.timer_at, s.timer_pending = nil, nil, nil
  elseif s.cmd == 0x03 then -- CalcCRC
    local lo, hi = crc_a(s.fifo, preset(s))
    s.reg[CRCResultRegL], s.reg[CRCResultRegH] = lo, hi
    s.reg[DivIrqReg] = s.reg[DivIrqReg] | 0x04
    update_irq(ctx)
  elseif s.cmd == 0x0F then -- SoftReset
    reset_regs(s)
    s.reg[CommandReg] = 0x10 -- PowerDown while the oscillator restarts
    s.reset_at = ctx.now() + 38 * US
    ctx.wake_at(s.reset_at, TOK_RESET)
  elseif s.cmd == 0x0E then -- MFAuthent
    local data = s.fifo
    s.fifo = {}
    local ok = authenticate(ctx, data)
    s.crypto = ok
    s.reg[Status2Reg] = ok and (s.reg[Status2Reg] | 0x08) or (s.reg[Status2Reg] & ~0x08)
    if ok then
      irq(ctx, 0x10) -- IdleIRq: done
      s.cmd = 0
      s.reg[CommandReg] = s.reg[CommandReg] & 0xF0
    else
      -- the card stays silent: the timer runs out
      s.timer_pending = ctx.now() + 2 * BIT * 9
      ctx.wake_at(s.timer_pending, TOK_TIMER)
    end
  elseif s.cmd == 0x01 then -- Mem: 25 bytes between FIFO and internal buffer
    s.mem25 = s.mem25 or {}
    if #s.fifo >= 25 then for i = 1, 25 do s.mem25[i] = s.fifo[i] end s.fifo = {} else for i = 1, 25 do s.fifo[#s.fifo + 1] = s.mem25[i] or 0 end end
    irq(ctx, 0x10)
  elseif s.cmd == 0x02 then -- GenerateRandomID: 10 bytes into the internal buffer
    irq(ctx, 0x10)
  end
end

local function read_reg(ctx, a)
  local s = ctx.state
  if a == FIFODataReg then
    return table.remove(s.fifo, 1) or 0
  elseif a == FIFOLevelReg then
    return #s.fifo
  end
  return s.reg[a] or 0
end

local function write_reg(ctx, a, v)
  local s = ctx.state
  if a == CommandReg then
    s.reg[CommandReg] = (s.reg[CommandReg] & 0x0F) | (v & 0x30)
    command(ctx, v)
  elseif a == ComIrqReg or a == DivIrqReg then
    if (v & 0x80) ~= 0 then s.reg[a] = s.reg[a] | (v & 0x7F) else s.reg[a] = s.reg[a] & ~(v & 0x7F) end
    update_irq(ctx)
  elseif a == FIFODataReg then
    if #s.fifo < 64 then s.fifo[#s.fifo + 1] = v else s.reg[ErrorReg] = s.reg[ErrorReg] | 0x10 end
  elseif a == FIFOLevelReg then
    if (v & 0x80) ~= 0 then s.fifo = {}; s.reg[ErrorReg] = s.reg[ErrorReg] & ~0x10 end
  elseif a == Status2Reg then
    s.reg[a] = v & 0x48 -- MFCrypto1On is cleared by software
    if (v & 0x08) == 0 then s.crypto = false; if s.card and s.card.state == "auth" then s.card.state = "active" end end
  elseif a == BitFramingReg then
    s.reg[a] = v & 0x77
    if (v & 0x80) ~= 0 and s.cmd == 0x0C then -- StartSend in Transceive
      local frame = s.fifo
      s.fifo = {}
      s.reg[ErrorReg], s.reg[CollReg] = 0, 0xA0
      finish_tx(ctx, frame, v & 0x07)
    end
  elseif a == VersionReg or a == ErrorReg or a == Status1Reg or a == CRCResultRegH or a == CRCResultRegL then
    -- read-only
  else
    s.reg[a] = v
    if a == ComIEnReg or a == DivIEnReg then update_irq(ctx) end
  end
end

function M.create(ctx)
  local s = ctx.state
  ctx.bus.spi_slave({
    select = function(ctx)
      s.addr, s.reading = nil, false
      return 0
    end,
    transfer = function(ctx, b)
      if not powered(ctx) or s.down then return 0 end -- hard power-down: the interface is off
      if s.addr == nil then
        s.addr, s.reading = (b >> 1) & 0x3F, (b & 0x80) ~= 0
        if s.reading then return read_reg(ctx, s.addr) end
        return 0
      end
      if s.reading then
        -- the next address byte: its register comes back during the following byte
        s.addr = (b >> 1) & 0x3F
        return read_reg(ctx, s.addr)
      end
      write_reg(ctx, s.addr, b)
      return 0
    end,
    deselect = function(ctx) s.addr = nil end,
  }, { sck = "SCK", mosi = "MOSI", miso = "MISO", cs = "SDA", mode = 0, edges = false })
end

function M.reset(ctx)
  local s = ctx.state
  reset_regs(s)
  s.card = new_card(hexbytes(ctx.params.card_uid))
  ctx.drive("RST", 1, "weak") -- the module's pull-up on NRSTPD
  s.down = false
  ctx.drive("IRQ", 0)
end

function M.on_set(ctx, name, value)
  local s = ctx.state
  if name ~= "card_present" then return false end
  if not value and s.card then s.card.state, s.card.auth = "idle", nil end -- out of the field: power lost
  return true
end

function M.on_input(ctx, pin)
  local s = ctx.state
  if pin ~= "RST" then return end
  local v = ctx.pins.RST.value
  if v == 0 then
    s.down = true
  elseif s.down then
    s.down = false
    reset_regs(s) -- the rising edge: a hard reset
  end
end

function M.on_time(ctx, token)
  local s = ctx.state
  local now = ctx.now()
  if token == TOK_RESET and s.reset_at == now then
    s.reg[CommandReg] = s.reg[CommandReg] & ~0x10
    s.reset_at = nil
  elseif token == TOK_RX and s.rx and s.rx.at == now and s.cmd == 0x0C then
    for _, b in ipairs(s.rx.data) do
      if #s.fifo < 64 then s.fifo[#s.fifo + 1] = b end
    end
    s.reg[ControlReg] = (s.reg[ControlReg] & 0xF8) | (s.rx.lastbits & 7)
    s.rx, s.timer_at = nil, nil
    irq(ctx, 0x20) -- RxIRq
  elseif token == TOK_TIMER then
    if s.timer_pending and s.timer_pending == now then
      s.timer_pending = nil
      start_timer(ctx)
    elseif s.timer_at and s.timer_at == now and not s.rx then
      s.timer_at = nil
      irq(ctx, 0x01) -- TimerIRq
    end
  end
end

function M.observe(ctx)
  local s = ctx.state
  local c = s.card
  return { antenna = antenna_on(s), card_state = c and c.state or "none", crypto = s.crypto and true or false,
    block1 = c and string.format(string.rep("%02X", 16), table.unpack(c.mem[1])) or "" }
end

return M
