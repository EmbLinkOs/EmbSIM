# Original ESP32 dual-core target: identity and implementation gates

**Status: firmware inspection implemented; no executable target is added.** Requested by
 the owner on 2026-11-04. This is the original ESP32 family, not ESP32-C3,
ESP32-S2 or ESP32-S3. The generic marketplace title “ESP-32 / ESP-32S
WiFi+Bluetooth Dual Cores” does not identify a reproducible board revision.
The owner supplied [Amazon ASIN B07N97DD1B](https://www.amazon.fr/dp/B07N97DD1B).
The listing brand is TECNOIOT. Its main image shows an ESP-WROOM-32 shield,
30 header positions, Micro-USB, EN and BOOT buttons. This identifies the
advertised form factor, not a guaranteed PCB revision or bill of materials.
See the [listing evidence and header transcription](esp32-tecnoiot-b07n97dd1b.md).

## Reference material

Primary references inspected for this investigation:

- [ESP32 Series Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf):
  chip selection, Xtensa LX6 CPUs, memory, peripheral inventory, pin restrictions,
  electrical characteristics and power modes.
- [ESP32 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf):
  memory and peripheral register behavior, reset, interrupts and ULP execution.
- [ESP32-DevKitC V4 user guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html):
  a reference board family, not an identification of the owner's board.
- [Espressif QEMU guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/tools/qemu.html):
  possible independent execution referee. Under the current reuse assessment,
  QEMU is a separate referee, not code to copy into the native runtime.

Record the exact document versions, hashes, schematic revision, silicon revision
and vendor SDK/toolchain versions in the eventual target fixtures. The URLs above
can change. Vendor maximum ratings are not normal operating conditions.

## Implemented: standalone firmware integrity inspection

`embsim image-info esp32 app.bin` inspects a plaintext original-ESP32 image
without loading or executing it. It reports the entry point, SPI flash header
encodings, raw chip-revision bounds and every segment's load address, payload
file offset and length. It rejects wrong chip IDs, invalid format fields,
truncation, 32-bit address wrap, bad padding/checksum/digest and trailing data.
The output descriptor is cleared on failure; the source file is never modified.

The checksum covers segment data with Espressif's initial XOR byte `0xEF`;
optional SHA-256 covers the header through checksum. An absent digest is reported,
not manufactured. An intact digest establishes integrity, not authenticity.
Segment addresses and revision fields are reported without claiming that the
requested PCB, silicon or memory map accepts them. Zero-length/dummy segments
are retained. Unsupported SPI encodings fail explicitly.

Scope is one standalone plaintext image, at most 64 MiB and 16 segments. Merged
flash dumps/partition tables, ELF conversion, encrypted or signed images, secure
boot verification and flash placement are not implemented in this inspector.
The existing C3 raw-flash loader is unchanged. This is not an ESP32 boot demo.

Format reference: [Espressif's firmware image specification](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/firmware-image-format.html).
The C parser is original code with no vendor ROM or loader copied. Acceptance
uses independent Python `struct`/`hashlib` fixtures, truncation at every byte,
16 segments, checksum-preserving data corruption, wrong-chip IDs, field failures
and untouched-input checks. A separate esptool 5.6.0 comparison accepted our
fixture's checksum/hash and generated ten files accepted by the native inspector
(with/without digest and zero to 1024-byte payloads). These synthetic files prove
format compatibility, not firmware execution or physical-board equivalence.

## Establish identity before catalog publication

The implementation needs a tuple, not a marketing name:

| Layer | Required evidence |
|---|---|
| Board | Manufacturer, product ID, PCB revision, header count/order, front/back labels and schematic |
| Module | Exact ESP-WROOM/ESP32-WROOM/ESP32-WROVER/ESP-32S marking, module pin map and module datasheet |
| Silicon | Exact part and revision, CPU count, applicable errata and eFuse defaults |
| Storage | Actual flash part/capacity/mode, and PSRAM presence/part/capacity |
| Power | Regulator, input routing/protection, capacitors, enable/reset network and current limits |
| USB | Actual USB-to-UART bridge and auto-reset transistor network |
| Controls | BOOT/EN contacts, pull resistors, LED connections/polarity and any extra devices |

Do not assume a 30-pin clone and a 38-pin DevKitC share a connector map, regulator,
USB bridge or LED pin. Do not expose module flash/PSRAM pins as free GPIOs.
If evidence is unavailable, label that property unresolved rather than choosing
a plausible component. WROOM and WROVER variants need separate descriptions.

## Existing runtime and reusable boundaries

The current ESP32-C3 has a RISC-V backend and C3-specific register models in
`core/mcu/esp32c3/`. The original ESP32 requires Xtensa LX6 instruction and
exception semantics. There is no Xtensa constructor in `core/mcu/build.c`.
Renaming the C3 target cannot execute ESP32 firmware correctly.

Reuse the scheduler, buses, board composition, electrical solver, instruments,
record/replay, transport and debugger infrastructure. Review individual C3
algorithms for shared concepts, but implement ESP32 register addresses, reset
values, interrupts and pin matrix against its own TRM. Existing multi-core
machinery is a starting point, not evidence of correct ESP32 SMP behavior.

## Implementation sequence and acceptance

Each gate is a coherent implementation PR with declared limitations and tests.
No unimplemented target appears as runnable in the part picker.

| Gate | Implementation | Acceptance evidence |
|---|---|---|
| 1. CPU | Xtensa LX6 configured instruction set, density instructions, register windows, ABI, special registers, exceptions, interrupts, atomics, memory ordering and debug registers | Instruction fixtures and unmodified toolchain-generated call/window/exception/atomic programs; unknown instructions stop with PC/opcode diagnostics |
| 2. Dual-core SoC | PRO_CPU/APP_CPU reset/release/stall, separate architectural state, shared/aliased memory, arbitration, interrupt matrix, inter-core interrupts and synchronization | Deterministic two-core shared-memory and spinlock tests; core-local reset and exception tests; record/replay equality |
| 3. Boot/storage | ROM provenance/licensing, strapping/eFuse behavior, SPI flash commands, image loader, partition/bootloader flow, cache/MMU, optional PSRAM | Unmodified pinned ESP-IDF bootloader and app, invalid image failures, flash read/write/erase and cache invalidation tests; no host execution of firmware |
| 4. Clocks/reset | Oscillators, PLL/APB/RTC clocks, reset causes/domains, watchdogs, brownout and clock switching | Register/timing fixtures; reset-retention matrix; watchdog and supply-failure recovery without erasing retained state accidentally |
| 5. Pins | GPIO/IO MUX/matrix, pads, drive strength, pulls, input-only pads, strapping latch and analog routing | Physical-header continuity, input thresholds/output loading, floating/conflicting driver warnings, boot with external strap loads |
| 6. Peripherals | UARTs, SPI, I2C, I2S, timers, LEDC, RMT, MCPWM, pulse counter, ADCs, DACs, touch, SD/MMC and documented DMA paths | Every declared mode tied to register fixtures and unmodified vendor examples; external electrical peers on actual pins |
| 7. Security/system | Interrupts, RNG, crypto engines, eFuses, flash encryption/secure boot as applicable to the identified revision | Known-answer vectors, reset/access protection and negative tests; deterministic test seeds explicitly distinguished from hardware entropy |
| 8. Low power | Modem/light/deep sleep, wake sources, RTC memory/peripherals, ULP, clock/domain retention | Wake timing, retained/lost state, ULP program and pin wake tests; current-consumption models qualified separately from functional sleep |
| 9. Radio | Wi-Fi station/AP and supported modes, Bluetooth Classic BR/EDR and BLE, coexistence and controller interfaces | Unmodified SDK examples and peer traffic; connection loss/retry, concurrent traffic and timing fixtures; document packet-level versus RF/analog fidelity |
| 10. Board | Exact module, power path, bridge/auto-reset, capacitors/resistors, buttons/LEDs and header connections | Schematic-derived netlist, loaded supply and reset traces, serial programming/console workflow, button interactions while firmware runs |
| 11. Product | ELF/flash loading, toolchain profile, GDB per-core state/breakpoints, traces, watches, schematic artwork and controls | Native CLI and GUI run the same image; cross-platform integration, undo/control interaction and saved-project compatibility |
| 12. Qualification | Arduino-ESP32 and ESP-IDF applications, SMP FreeRTOS, peripheral combinations and hardware comparisons | Pinned reproducible builds on Linux/macOS/Windows; full coverage inventory, raw traces and tolerances; no unexplained unsupported-register accesses |

The peripheral inventory must be expanded into every TRM block and revision-specific
mode before declaring the SoC complete. The table is a work breakdown, not a
claim that all edge cases have already been enumerated. Nominal CPU execution
and peripheral timing accuracy must be reported separately; do not claim
cycle accuracy from successful boot alone.

## Electrical and visual contract

- GPIOs participate in circuit loading, including pad restrictions, configured
  drive strength and pulls. GPIOs are not ideal rail-to-rail sources.
- ADC attenuation/calibration/nonlinearity, source impedance, sampling behavior
  and ADC2/radio arbitration require explicit qualification. DAC and touch
  readings must come from connected circuit behavior, not fabricated values.
- BOOT and EN operate during execution. Their electrical networks determine
  reset/strapping effects. Serial DTR/RTS follows the actual board circuit.
- Supply sag, brownout and reset must have consistent effects on firmware,
  peripherals and schematic indicators. A power LED is driven by its circuit.
- Readable pin names, boot/power warnings and live states belong in the schematic.
  A generic illustration must not imply a verified header layout.
- “Ultra-low power” requires identified measurement conditions and validated
  consumption models; it is not inferred from the product title.

## ADR-030 answers and release rule

Real operation is defined by the identified board schematic, silicon TRM,
module datasheet, errata and pinned vendor firmware. Observable behavior includes
both cores' software state, physical pin voltages/currents/timing, boot/reset,
host serial traffic, radio peers and sleep/wake behavior. Full implementation
means the gate inventory and documented mode/edge-case coverage, not a boot demo.
Internal abstractions are permitted only with explicit externally tested bounds;
packet radio models do not establish RF propagation fidelity. Proof combines
instruction fixtures, vendor firmware, independent referees and hardware traces.
No hardware trace has been collected for this new target yet.

Until those gates pass, report the target's exact supported subset and missing
features. This implementation establishes neither board support nor real-hardware
equivalence. The exact board identity blocks the final board netlist; it does not
block research on the shared Xtensa/SoC foundation.
