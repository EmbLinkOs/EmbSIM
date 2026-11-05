# Original ESP32 UART0 normal asynchronous profile

**Implemented: opt-in UART0 normal asynchronous transmit/receive FIFOs, explicit
source-clock advancement, framing, CTS gating, interrupt latches and native
APB/AHB firmware access. This is not a complete UART, pin binding, USB bridge,
physical clock calibration or successful SDK boot. UART1/2 remain unsupported.**

## Hardware and implementation boundary

ESP32 TRM v5.9 chapter 19 describes three UARTs sharing 1024 bytes of RAM, with
128-byte TX and RX allocations by default. A transmitter removes a byte from its
FIFO into a separate serializer, then emits a low start, 5..8 data bits LSB first,
optional odd/even parity and a high stop. Its baud divider has a 20-bit integer
and four fractional bits, with APB or REF_TICK selected by CONF0. RX, error,
threshold, modem-change and completion interrupts have independent raw latches,
software enables and write-one clear controls.

This original C99 implementation has no copied vendor runtime. It supports the
fixed UART0 allocation at APB 0x3ff40000 and FIFO alias 0x60000000. UART1/2,
allocation changes/shared-RAM interference, DPORT peripheral reset/clock gates,
DMA/UHCI, IRDA, RS485, autobaud, inversion, loopback, RX flow/software flow,
break detection/transmission, idle insertion, AT commands, wakeup and electrical
pad/USB behavior remain absent. Unsupported register/features/accesses return an
error unchanged; they do not acquire fabricated zero/default behavior.

The pinned SDK documents hardware issues that constrain this profile:

- `uart_ll_rxfifo_rst` drains through APB because the hardware RX reset bit is
  unreliable. Setting that bit is unsupported here. The SDK helper itself also
  inspects RX memory pointers; its complete reset procedure is not yet qualified.
- `uart_ll_set_stop_bits` uses RS485 `dl1_en` for the two-stop-bit workaround.
  STOP encoding 3 and that workaround remain unsupported. One and 1.5 stop bits
  are implemented; invalid encoding zero fails.
- UART1 TX reset also affects UART2. Neither port is inferred from UART0 or
  exposed through an invented independent reset.

## Explicit fixture and externally visible behavior

Call `esk_esp32_machine_uart_configure` with on=1 and explicit binary RXD, CTS
and DSR levels. These are host fixture inputs, not inferred GPIO pinmux, board
wiring, pull state or physical captures. The standalone UART object must first
be initialized with `esk_esp32_uart_init`; the machine does this on creation.
Attach initializes empty FIFOs, CLKDIV=0x2b6, CONF0=0x0800001c, CONF1=0x6060
and enables zero. Threshold conditions are immediately evaluated for the active
fixture; its raw value is not a claim about the instantaneous silicon reset edge.
Detach/re-attach is a debug fixture operation, not a SoC reset producer.

- TX has 128 queued bytes plus a separate active frame. Moving the first queued
  byte into an idle, unblocked serializer is immediate in this functional profile;
  APB-to-UART synchronization latency is unqualified. Bytes are masked to the
  configured character length when a frame starts. Completion emits the framed
  character through an optional callback; a NULL callback explicitly discards it.
- `STATUS` reports FIFO counts, projected TX state/bit, supplied RXD/CTS/DSR and
  software RTS/DTR controls. RX inputs represent completed frames, so this
  profile has no in-progress receive FSM. TX FSM half-bit projections and FIFO
  transitions follow explicit host clock turns, not instruction retirements.
- Hardware TX flow control stops the next frame while CTS is high. A frame
  already in flight completes. CTS/DSR edges latch their change interrupts.
  External input updates never rewrite the active frame.
- RX receives a decoded character plus explicit parity/frame-error flags. Enabled
  parity checking is required for a parity-error event. ERR_WR_MASK discards
  errored bytes while preserving error interrupts. Overflow discards the incoming
  byte and latches RXFIFO_OVF; queued bytes retain order. Empty FIFO reads and full
  TX writes fail unchanged rather than inventing undefined bytes or drop rules.
- FIFO word reads/writes use serialized, coherent APB/AHB access. RX memory
  status exposes the fixed FIFO's relative modulo-128 read/write pointers for
  the selected SDK length helper. Absolute shared-RAM layout, bus pipeline and
  silicon FIFO-count errata are not qualified by that helper test.
- RX-full is strictly above its threshold; TX-empty is strictly below its
  threshold. Raw events latch independently of enable. Clear pulses remove
  selected events, and a threshold still true immediately reasserts. TX_DONE
  occurs after the last queued frame completes, not merely when its FIFO drains.
  Unsupported event producers cannot be injected through a blanket raw-bit API.
- RX timeout is an explicit external qualified timeout event. It requires enabled
  timeout and unread RX data; the host is responsible for its timing. No guessed
  timeout duration is derived from the differing SDK/TRM clock-unit descriptions.
- UART0 owns interrupt-matrix source 34 while attached. Both core views share
  state. Firmware changes route after instruction commit; host changes route
  immediately between turns. External injection into that owned slot fails.
  The bounded CPU-only reset preserves this peripheral; global/reset-domain
  behavior remains unsupported.

MMIO is aligned 32-bit data access only. FIFO fetch/CAS, write-only clear reads,
unknown registers and feature-enabling writes remain unsupported. TX FIFO reset
is supported only while the serializer is idle; writes while reset is held fail.
Framing/divider changes during an active frame fail unchanged rather than
asserting an unverified mid-frame reconfiguration policy. Enable/clear/threshold
changes and RX access remain available during transmission.

## Source-clock contract, separate from CPU timing

`esk_esp32_machine_uart_advance` supplies ticks of the currently selected UART
clock. There is no automatic conversion from retired instructions, wall time,
CPU CCOUNT or an assumed APB frequency. A caller must supply the selected clock's
events and respect upstream clock gating; full SoC clock-tree integration is absent.

Let `D = 16 * CLKDIV + CLKDIV_FRAG`. Internally, one half bit consumes `D` units,
where one unit is 1/32 of a source-clock cycle. Advancing one source tick supplies
32 units. This integer rational accumulator preserves fractional carry across
frames and chunked calls without rounded per-byte deadlines. Each completion
callback supplies its offset within the current advance call in 1/32 source-cycle
units, preserving distinct character times even when one call drains a queue. TX captures the
selected divider/framing at frame start. Zero integer dividers are unsupported.
The idle transmitter consumes no backlog of future time.

Callbacks and all APIs are serialized and must not re-enter. Mutating standalone
UART APIs reject calls during clock advancement; machine APIs reject re-entry
while executing or advancing. Direct machine bus use is permitted only between
turns, as defined by the machine contract. No host callback may modify that bus.

This is a functional serial-clock model. It does not qualify oscillator error,
PLL changes, jitter, synchronizers, metastability, CPU instruction timing, cache
stalls, bus contention, analog edges, real baud tolerance or silicon captures.

## Evidence and reproduction

Native `test_esp32_uart` passes **915,199 assertions**, including 8,192 frame
configurations, bit/state projections, CTS gating, FIFO ordering/wrap/overflow,
errors, W1C/reassertion, fractional deadline/chunking, unchanged failures,
reset hold, source ownership and fetched enable-store/next-instruction interrupt
entry. The sanitizer build runs the same tests.

`tests/vendor/xtensa_integer/run_uart.py` independently constructs bitstreams from
pinned register fields: **316,077 half-bit/frame comparisons** pass. It also
compiles **eleven unchanged ESP-IDF v4.4.7 helper bodies** with the actual ESP32
GCC 8.4 toolchain: **128 interactive C calls / 44,032 retired instructions** pass
on both native core buses. Firmware uses the real AHB TX/APB RX addresses, FIFO
length and interrupt helpers. The host advances the UART clock and supplies
received frames at a RAM handshake while firmware runs; no PC shortcut or
Python instruction-memory callback implements the UART operations.

```sh
python3 tests/vendor/xtensa_integer/run_uart.py \
  --uart-reg "$IDF_PATH/components/soc/esp32/include/soc/uart_reg.h" \
  --uart-ll "$IDF_PATH/components/hal/esp32/include/hal/uart_ll.h" \
  --uart-struct "$IDF_PATH/components/soc/esp32/include/soc/uart_struct.h" \
  --misc "$IDF_PATH/components/hal/platform_port/include/hal/misc.h" \
  --compiler xtensa-esp32-elf-gcc --objcopy xtensa-esp32-elf-objcopy \
  --linker xtensa-esp32-elf-ld --nm xtensa-esp32-elf-nm
```

External SDK source is pinned to
[ESP-IDF v4.4.7 / 38eeba213aa695aabfd6d89aa9f5078dbe5a94c3](https://github.com/espressif/esp-idf/tree/38eeba213aa695aabfd6d89aa9f5078dbe5a94c3).
Selected helper bodies and headers retain their Apache-2.0 notices in temporary
build files. They are external test inputs, not bundled simulator runtime code.

| External source | SHA-256 |
| --- | --- |
| ESP32 TRM v5.9 PDF, chapter 19 | `1007b90e6fafc16541d693d7444b63e1c5b01e46e60cce9b6810e02340a64b82` |
| `soc/esp32/include/soc/uart_reg.h` | `29d5e65d1dd560a339eb6057a93a1c8e2bcc90d730c82a8506fd894bd4869922` |
| `hal/esp32/include/hal/uart_ll.h` | `b98b4846e92480a24b6710b4dcd025a9c6951e4be37a95ce1fcd0c89df20dda5` |
| `soc/esp32/include/soc/uart_struct.h` | `4438b0d2289fa7acc417a6df9a5e4a024bcfe7932396955ad8594a907988be91` |
| `hal/platform_port/include/hal/misc.h` | `552e2d4ce5be7d93d228eb3b48bd959d0485290a7476ce5a7de438fe7ab9e152` |

The [native ROM probe](esp32-boot-verification.md) adds
`--uart0-active-fixture` to the existing explicit eFuse/RTC/GPIO fixtures. It
supplies empty FIFOs and RXD=1/CTS=0/DSR=0, with no serial ticks. The ROM now retires
**6,737 instructions**, stopping at **UART1 INT_CLR write 0x3ff50010**,
PC **0x40009013**, opcode **0x68a2**. The
[hash-recorded evidence](../../tests/vendor/xtensa_integer/evidence/boot-with-uart0-2026-11-05.json)
retains `boot_verified: false`. Without this UART0 fixture, the previous UART0
blocker remains reproducible. Neither this native failure nor reference QEMU
success qualifies physical-board or complete ROM/SDK boot.
