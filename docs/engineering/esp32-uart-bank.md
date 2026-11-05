# Original ESP32 UART bank and shared FIFO RAM

**Implemented: normal asynchronous UART0/1/2, shared FIFO storage, bounded
allocation changes, extended counts/thresholds and idle-only UART1→UART2 TX
reset coupling. This does not complete every UART mode, clock/pin/USB binding,
reset domain, physical timing or ROM/SDK boot.**

## Hardware facts and observable contract

The [UART0 contract](esp32-uart0.md) documents framing, CTS gating, decoded RX,
error handling, externally advanced fractional source clocks and interrupt
latches. This follow-up reuses those qualified operations for all three ports;
it adds a coordinator for shared memory/reset behavior rather than independent
FIFO copies. Its primary references are ESP32 TRM v5.9 chapter 19, especially
Figure 19.3-2, and the pinned ESP-IDF v4.4.7 UART register/LL headers.

| Port | APB registers | AHB FIFO | Matrix source | Default TX RAM offset | Default RX RAM offset |
| --- | --- | --- | --- | --- | --- |
| UART0 | 0x3ff40000 | 0x60000000 | 34 | 0 | 384 |
| UART1 | 0x3ff50000 | 0x60010000 | 35 | 128 | 512 |
| UART2 | 0x3ff6e000 | 0x6002e000 | 36 | 256 | 640 |

Offsets are bytes in the shared 1024-byte RAM. Default capacities are 128 bytes.
The bank owns one byte array. Each FIFO has independent cursors/counts; extending
one into a peer's allocation really allows writes to overwrite bytes still queued
by that peer. A serializer captures a byte when removing it from its FIFO, so
later RAM writes do not rewrite a frame already in flight. RAM is not zeroed by
FIFO reset or host attach. Initial undefined RAM bytes are not readable through
an empty FIFO and no guessed raw-RAM MMIO interface is exposed.

### Allocation and counters

`MEM_CONF` exposes TX_SIZE/RX_SIZE, high TX-empty/RX-full thresholds and high RX
timeout configuration. Sizes count 128-byte blocks at the documented default
bases. Supported allocations stay within the end of physical RAM: TX sizes
1..(8−port), RX sizes 1..(5−port). Overlap is supported; end-of-RAM wrapping,
zero-size behavior and 9..15-block aliasing are unqualified and fail unchanged.
RAM power-down, flow-control extensions and unsupported features also fail.

Changing a TX allocation requires its queue empty and serializer idle; changing
an RX allocation requires its queue empty. Changed allocation cursors restart at
zero as an explicit empty-queue fixture policy; live migration, cursor behavior
across nonempty resize and arbitration latency are not guessed. Threshold-only
changes remain available while queues contain data. Reserved bits do not enable
features or allocate memory.

The low eight count bits appear in STATUS; the upper three bits appear in
MEM_CNT_STATUS. MEM_RX_STATUS projects relative read/write cursors for the
selected allocation. Absolute bus pipeline behavior and silicon FIFO-count
errata are not qualified. RX-full and TX-empty thresholds concatenate their
three MEM_CONF high bits with the CONF1 low seven bits; raw threshold events
remain latched until W1C and reassert while their condition remains true.

The SDK default FIFO-length/drain helpers are qualified with one-block
allocations. Its RX length helper contains a hardcoded modulo-128 calculation;
these tests do not claim it handles expanded FIFOs or replace its code with a
host approximation. Expanded counts, wrap and bytes are checked separately by
native tests and an independent shared-RAM referee.

### Reset coupling and failure boundaries

The SDK states that UART1 TX FIFO reset also resets UART2's TX FIFO, and avoids
that helper on both ports. The bank supports the corresponding explicit CONF0
TX-reset control when **both serializers are idle**: UART1 and UART2 TX cursors
and counts clear, UART0 remains intact, and stored RAM bytes remain unchanged.
While held, the shared reset blocks UART2 queue writes/start; releasing UART1's
reset releases that input. UART2's own CONF0 reset bit is unsupported because the
TRM states that UART2 has no FIFO-reset register.

A reset request while either affected serializer is active fails without partial
clearing, frame abort, fabricated TX_DONE or peer changes. Invalid configuration
combined with reset also fails without touching peer/RAM state. UART0 TX reset
remains local and idle-only. RX reset is unsupported on every port: the SDK
warns that the reset bit is unreliable, so the unchanged RX drain helper is
exercised instead. Cross-port active-reset glitches, exact reset synchronizers
and the RX-reset erratum's undocumented effects are not modeled.

## Host and machine integration

`esk_esp32_uart_bank_configure` and `esk_esp32_machine_uart_port_configure`
select an explicit port and require RXD/CTS/DSR levels. They are fixture attach
operations, not inferred GPIO, board netlists or SoC reset producers. Each port
starts with empty default allocations; attaching one does not erase common RAM
or another port's queue. The standalone configure API rejects bank-owned objects
so it cannot accidentally discard their coordinator binding.

The machine provides port-selected input, decoded receive, qualified timeout and
source-clock advance APIs. Existing UART0 APIs remain compatibility wrappers.
Both CPU bus views share the bank. Sources 34/35/36 are exclusively owned while
their respective ports are attached; external injection into an owned source
fails, and detach clears/releases only that source. Sources mapped to the same
CPU input are OR-combined, and clearing one UART does not dismiss another's IRQ.
Firmware changes route after instruction commit. CPU-only reset retains bank
state; full peripheral/global/power-reset domains remain unqualified.

Source-clock ticks are explicit per-port inputs. There is no assumed APB/ref
frequency, instruction-to-time conversion or implicit advancement of other
ports. The caller supplies each selected clock's events; physical clock-tree and
pin/electrical integration remain absent. Completion offsets remain in 1/32
source-cycle units within the current advance call. All operations are serialized;
callbacks must not re-enter. Bank MMIO/configuration and machine APIs reject
calls during advancement. Direct per-port writes cannot bypass shared reset
coordination; use bank MMIO for UART1 reset changes.

## Full implementation and remaining omissions

Normal-mode framing, CTS, received decoded frames/errors, FIFO state, thresholds,
interrupts and explicit time inputs are observable. Internal independent cursors
plus one RAM array implement those observations without pretending to model RAM
arbitration cycles. The implementation remains original C99.

IRDA, RS485, the two-stop-bit workaround, inversion, loopback, autobaud, break,
idle insertion, AT commands, RX/software flow control, DMA/UHCI, wakeup, RAM power
state, end-wrapping allocation, live resize, active resets, waveform-sampled RX,
physical pin loading/USB bridge and calibrated CPU/SoC timing remain unsupported.
Enabling an omitted feature fails; no blanket MMIO success/zero fallback is added.

## Proof and reproduction

- **271,883 native bank assertions** cover all supported nonwrapping size pairs
  on all ports, full/empty/wrap/overflow, extended counts, relative cursors, real
  cross-port TX/RX RAM overwrites, preserved RAM, reset hold/coupling/rejection,
  invalid operation rollback, source ownership and OR-combined CPU inputs.
- The previous **915,199 UART0 assertions** still pass. The default UART0 vendor
  command still produces its prior 128 C calls / 44,032 instructions.
- The all-port runner checks **316,077 independent bit/frame/completion-offset
  observations**, plus **4,096 independent shared-RAM turns**. The latter uses
  a Python byte array, independently tracked cursors/counts/IRQ latches and the
  TRM's allocation bases; it compares counts, data, pointers and raw/masked IRQs
  after writes, reads, resizes, clears, resets and frame completion. It observes
  overwritten queued bytes rather than caching idealized FIFO payloads.
- **384 actual ESP32 C interactive calls / 160,128 retired instructions** execute
  **twelve unchanged SDK helper bodies** across every port on both native CPUs.
  Firmware writes through real AHB addresses, reads through APB, checks FIFO and
  interrupt helpers, then drains three additional externally received bytes
  using `uart_ll_rxfifo_rst`. A queued UART2 byte survives the SDK's intentional
  UART1 TX-reset-helper no-op. These are native simulator tests, not hardware
  captures, physical UART timing proof or successful SDK boot.

Use the [UART0 reproduction command](esp32-uart0.md#evidence-and-reproduction)
with **`--all-ports`** to select the bank tests. External references retain the
same SDK/TRM hashes and licences. No vendor runtime or ROM bytes are bundled.

## Native boot boundary

Add **`--uart-bank-active-fixture`** to the ROM diagnostic with the existing
explicit eFuse/RTC/GPIO fixtures, instead of `--uart0-active-fixture`. Each port
has empty default allocations and explicit RXD=1/CTS=0/DSR=0. No serial ticks or
physical board pinmux are inferred.

Native ROM now retires **6,790 instructions / 6,791 attempts**, passing the
previous UART1 interrupt-clear blocker. It then fails instruction fetch at
**0x40065c18**, entered by the real CALL8 at 0x40007729. The fabric currently
exposes the first 384 KiB instruction-ROM range, while the SDK declares a mask-ROM
range through 0x40070000 and provides some symbols above 0x40060000. Qualifying
that extended mapping and the external ROM image layout is a separate boot task;
this milestone does not substitute RAM, zero fill or a host PC shortcut there.
[Hash-recorded evidence](../../tests/vendor/xtensa_integer/evidence/boot-with-uart-bank-2026-11-05.json)
still reports `boot_verified: false`. The previous UART0-only 6,737-instruction
boundary remains reproducible by its original fixture selection.
