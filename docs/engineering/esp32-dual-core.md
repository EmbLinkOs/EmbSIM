# Original ESP32 dual-core target: identity and implementation gates

**Status: firmware inspection and experimental integer/window/loop execution implemented; no executable target is added.** Requested by
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

## Implemented: experimental Xtensa integer execution kernel

`core/mcu/xtensa/integer.c` executes a bounded subset of little-endian, wide
(24-bit) instructions against a caller-provided A0–A15 register window:
ADD, SUB, ADDX2/4/8, SUBX2/4/8, AND, OR, XOR, MOVI, ADDI, ADDMI, NEG,
ABS, MULL, SLLI/SRLI/SRAI and EXTUI. MOV's OR encoding is included. Arithmetic wraps modulo 2^32,
including ABS/NEG of the most negative integer; sign extension uses unsigned
arithmetic. Destinations may alias either source, and A0 is writable.

Unsupported instructions and truncated/invalid inputs fail without changing any
register; successful execution reports three consumed bytes. The integer helper also implements ADD.N, ADDI.N, MOV.N and MOVI.N; the
last uses Xtensa's asymmetric -32..95 immediate range. This helper has no
register-window rotation, interrupts, architectural exception dispatch, special
registers, FPU, SMP or timing model.
It is not wired into the CPU constructor or catalog and cannot run firmware.
The caller-provided window is not a claim that register-window behavior exists.

`test_xtensa_integer` checks an assembled sequence, arithmetic extremes, aliases,
input errors and unchanged state on unsupported execution (111 assertions).
`tests/vendor/xtensa_integer/run.py` independently assembles 5,376 instruction
cases with GNU binutils and compares every output register against Python
integer arithmetic. It varies register aliases, immediates and 32-bit extremes
with a fixed seed. GNU xtensa-lx106 binutils 2.40 was used solely as an external
encoding referee for the shared subset; it is not an LX6 system emulator or an
ESP32 qualification suite. No assembler code or decoder tables are copied.

Reproduce on Linux with a host C compiler and separate Xtensa binutils:

```sh
python tests/vendor/xtensa_integer/run.py \
  --assembler /path/to/xtensa-lx106-elf-as \
  --objcopy /path/to/xtensa-lx106-elf-objcopy
```

The deterministic native unit test runs in CTest without those external tools.
Hardware execution comparison and a complete LX6 ISA referee remain outstanding.

## Implemented: mixed-width fetch, flow and memory execution

`core/mcu/xtensa/execute.c` adds a PC and a visible register window, plus a
byte/halfword/word callback bus. It executes:

- The integer helper's wide and compact operations, NOP and NOP.N.
- J, JX, CALL0, CALLX0, RET and RET.N. CALLX0 reads the target before
  writing A0, including when the target register is A0. Windowed calls and
  returns remain explicitly unsupported.
- BEQZ/BNEZ/BLTZ/BGEZ, BEQ/BNE/BLT/BGE/BLTU/BGEU, BEQZ.N/BNEZ.N
  and BEQI/BNEI/BLTI/BGEI/BLTUI/BGEUI with encoded constant selection.
  Signed comparisons differ from unsigned; narrow branches have unsigned
  forward displacement, while wide branches sign-extend their offsets.
- L8UI, L16UI, L16SI, L32I, S8I, S16I, S32I, L32I.N and S32I.N.
  Narrow loads mask callback results, and signed halfword loads sign-extend.
- L32R in the default PC-relative mode; extended LITBASE is not implemented.

Instruction fetch reads exactly two or three bytes, even at memory boundaries.
Data access checks natural alignment before calling the bus. On unsupported
instructions, truncation, failed fetch or failed data access, registers and PC
remain unchanged, and diagnostics retain the failing PC/address/opcode. These
are caller-visible fault reports, not architectural exception-vector dispatch.
The bus callback must not complete a write when reporting failure. Side effects
of MMIO reads cannot be rolled back. There is no MMU, protection, cache or
multicore ordering/timing claim. Execution is sequential and has no inferred
cycle count.

An assembled mixed-width program fetches through memory, sums four words in a
loop, calls a doubling subroutine and returns to the expected PC (27 executed
instructions). The native test has 618 assertions covering branches, compact
immediate limits, byte/halfword/word behavior, literal loads, link-register alias,
32-bit PC wrap, memory-edge fetches, alignment and failed-access state retention.
An optional external-assembler/linker referee checks 3,840 execution cases and
960 rejected accesses against independent Python PC/register/bus expectations.
Linker relaxation is disabled to preserve the fixture's explicit instruction
addresses. The earlier 5,376 integer cases continue to pass.

```sh
python tests/vendor/xtensa_integer/run_execute.py \
  --assembler /path/to/xtensa-lx106-elf-as \
  --objcopy /path/to/xtensa-lx106-elf-objcopy \
  --linker /path/to/xtensa-lx106-elf-ld
```

A further compiler acceptance runner compiles unchanged freestanding C for
summing words, insertion-sorting unsigned words and copying bytes. With GCC
xtensa-lx106 10.2.1 at `-O1`, 96 executions fetch 33,472 instructions and verify
results, complete output buffers/guards, source preservation, CALL0 callee-saved
registers and return addresses. This shared-subset compiler evidence is not LX6
windowed-ABI, hardware or vendor SDK qualification.

```sh
python tests/vendor/xtensa_integer/run_c.py \
  --compiler /path/to/xtensa-lx106-elf-gcc \
  --objcopy /path/to/xtensa-lx106-elf-objcopy \
  --linker /path/to/xtensa-lx106-elf-ld \
  --nm /path/to/xtensa-lx106-elf-nm
```

No CPU constructor, GDB register mapping or board catalog support is registered.
Full instruction coverage, general exception/interrupt handling, complete system
registers, synchronization, dual-core execution, SoC boot and peripherals still
need implementation. Selected windowed-ABI support is described below. Single-step synthetic programs do not qualify ESP-IDF boot.

## Implemented: register windows, spill/fill transitions and hardware loops

`core/mcu/xtensa/windows.c` wraps execution with a 64-physical-AR register file,
16 four-register window positions and exact read/write operand references for
the implemented instruction set. The visible register cache can be edited by
the caller; it synchronizes with the physical file on successful transitions.
Initialization configures an ABI test state, not ESP32 power-on reset defaults.

Implemented window behavior:

- CALL4/8/12 and CALLX4/8/12 encode the return link and CALLINC without rotating.
  ENTRY allocates its frame from the old stack pointer and then rotates, with
  physical register overlap, wraparound and WindowStart tracking.
- RETW/RETW.N reconstruct the return PC's high bits, validate the return window
  distance and clear the departing active-window bit on successful return.
- Lazy overflow checks consider actual operands, not all 16 visible registers.
  They select the overflow4/8/12 event from the occupied window layout.
- Underflow4/8/12 returns move to the caller window and record exception state.
  Both overflow and underflow set EXCM, OWB and EPC1 and transfer to caller-
  supplied vector addresses. They consume zero instruction bytes: the faulting
  instruction is retried after RFWO/RFWU restores the original window/PC.
- RFWO clears the spilled window bit; RFWU sets the filled bit. Physical register
  state is retained across the handler and retry. L32E/S32E support exception-mode
  spill/fill memory accesses, with natural alignment and failed-access retention.
- ROTW and RSR/WSR for PS, WINDOWBASE, WINDOWSTART and EPC1 are supported.
  Privileged operations reject unsupported user-mode execution. General privilege
  exception dispatch is not implemented. PS fields unrelated to these mechanisms
  are retained but do not implement interrupts, security or other system behavior.

The module preserves CALLINC on RETW, one of the ISA-permitted implementation
choices; exact silicon-specific behavior still needs independent qualification.
L32E/S32E outside exception mode is intentionally unsupported. RSYNC has no
asynchronous pipeline to drain in this sequential functional engine. No cache,
MMU, memory-protection, pipeline timing or SoC vector map is inferred.

LOOP, LOOPNEZ and LOOPGTZ update LBEG/LEND/LCOUNT; their RSR/WSR access is
supported. Sequential loop-end execution loops back and decrements LCOUNT,
while taken branches, jumps, calls and returns do not. This distinction is
explicit even when a branch target equals its fallthrough address. EXCM disables
loop-back during handlers. Unconditional LOOP with count zero retains the
architectural 2^32 iteration count; conditional zero/negative skips are distinct.
Pipeline synchronization, illegal/restricted endpoint sequences and processor-
specific loop alignment constraints are not qualified by this functional model.

### Windowed-ABI evidence

The native window test has 1,337 assertions: CALL4/8/12 across every physical
window position, stack/link/argument/result overlap, lazy overflow and retries,
underflow restore/retry, physical wraparound, special registers, invalid modes,
loop limits/exception behavior, taken-versus-sequential end handling and failed
exception-memory accesses. The existing integer/execution tests still pass.

The actual ESP32 toolchain is Espressif's GCC 8.4.0 esp-2021r2-patch5:

- [Archive](https://dl.espressif.com/dl/xtensa-esp32-elf-gcc8_4_0-esp-2021r2-patch5-linux-amd64.tar.gz)
- SHA-256 `8ef14e0409c2011b41e504a30f70d3e35287313a795d1f2462ad2cd0e2052d37`,
  verified against [ESP-IDF v4.4.7 tools.json](https://github.com/espressif/esp-idf/blob/v4.4.7/tools/tools.json).

The optional `run_windows_c.py` first passes 128 real ESP32 windowed-ABI C runs:
sum, sort, copy and nested calls, including compiler-emitted hardware loops
(35,165 retired instructions). With the vendor vector source below it passes
224 runs: 160 compiled C runs and 64 project assembly recursion runs, retiring
63,337 instructions. Deep recursion exercises every overflow/underflow type:
204/204 events for 4-register frames, 300/300 for 8-register frames and 351/351
for 12-register frames. Results, memory guards, caller windows and returns pass.

The six spill/fill handler instruction bodies are extracted without instruction
changes from [ESP-IDF v4.4.7 xtensa_vectors.S](https://github.com/espressif/esp-idf/blob/v4.4.7/components/freertos/port/xtensa/xtensa_vectors.S).
Expected source SHA-256:
`29b544a01b708ca59af85ec6caa80101fba33345d8ec765e6a8fd832b1728366`.
The runner rejects source drift, retains the source's Cadence copyright and
permissive permission notice in the generated assembly, and compiles only the
six handlers at their original relative offsets. Toolchain and vendor source
are external test inputs, not linked into EmbSIM or bundled in the catalog.
This is the source's permissive software grant, not permission to redistribute
unrelated vendor documents or ROMs.

```sh
python tests/vendor/xtensa_integer/run_windows_c.py \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld \
  --nm /path/to/xtensa-esp32-elf-nm \
  --vectors-source /path/to/xtensa_vectors.S
```

Test vectors and memory addresses are configured by the runner; they are not
an ESP32 memory map. The harness initializes a valid caller window/stack. The
vendor handlers perform actual bus loads/stores and RFWO/RFWU retries; no host
shadow-register restore is used. General RTOS exception/interrupt code, MOVSP,
full context switching, ESP-IDF boot and SoC devices remain outside this evidence.

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
exception semantics. There is no Xtensa CPU constructor in `core/mcu/build.c`; the experimental
integer kernel below is not a runtime backend.
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
