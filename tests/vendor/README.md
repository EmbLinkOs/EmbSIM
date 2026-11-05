# Vendor frameworks

Firmware written for a vendor's own framework, built with that framework
unchanged and run on EmbSIM's boards under scenarios (01 §8.2). The
frameworks are fetched, not vendored; CI's linux-gcc job pins them, and
its esp-idf and arduino-esp32 jobs ESP-IDF and Arduino-ESP32 with their
toolchains.

| Directory | Framework | Board |
| --- | --- | --- |
| `arduino/` | ArduinoCore-avr (Blink, a tour of the core) | `emblink/arduino_uno` |
| `stm32cube/` | ST's STM32F1 HAL and CMSIS device files | `emblink/bluepill` |
| `stm32cubef4/` | STM32CubeF4's own examples for the NUCLEO-F446RE (GPIO, HAL time bases, RCC, RTC, timers, UART, PWR's low-power modes, I2C between two boards, EEPROM emulation in flash) and, with `--board nucleo_f446ze`, for the NUCLEO-F446ZE (ADC, CRC, Cortex, DMA, flash erase and program, GPIO, the watchdogs, RTC, seven timer examples, UART, the USB device HID mouse on a computer's port, the USB host HID and MSC demos with Adafruit's 1.8" TFT shield, a mouse and a flash drive; their middleware and components, stm32-mw-usb-device v2.11.4, stm32-mw-usb-host, stm32-mw-fatfs, stm32-st7735 and stm32-bsp-common at the commits CubeF4 pins, fetched like the rest and copied into CubeF4's tree, with its Adafruit_Shield BSP and Utilities' LCD log and fonts) | `emblink/nucleo_f446re`, `emblink/nucleo_f446ze` |
| `pico-sdk/` | The Raspberry Pi Pico SDK | `emblink/pico` |
| `micropython/` | MicroPython v1.24.1's rp2 port (RPI_PICO), built with its own Makefile and copied as firmware.uf2 onto the Pico's BOOTSEL drive; its prompt over its USB serial port: arithmetic, `machine.Pin` lighting the LED, a file in its flash filesystem | `emblink/pico` |
| `freedom-metal/` | SiFive's freedom-metal and freedom-e-sdk's examples | `emblink/hifive1_revb` |
| `nrfx/` | Nordic's nrfx drivers and MDK | `emblink/nrf52840dk` |
| `freertos/` | The FreeRTOS kernel (V11.1.0), its tasks read back through the image's symbols and DWARF (`run --rtos`, scenario `rtos` assertions) | `emblink/nucleo_f446re` |
| `esp-idf/` | ESP-IDF v5.4's own examples (hello_world, blink, LEDC, the temperature sensor, the ADC, an I2C EEPROM, the general-purpose timer, a UART echo, FreeRTOS's run-time statistics; Wi-Fi's scan, station, softAP with a station, ESP-NOW and http_request (hello_world and http_request through a USB power meter); BLE's NimBLE bleprph and blecent and Bluedroid's ble_ibeacon, some on two boards), built by `idf.py` into a flash image | `emblink/esp32c3_devkitm1` |
| `arduino-esp32/` | Arduino-ESP32 3.2.0 through arduino-cli, as the IDE builds for "ESP32C3 Dev Module": the IDE's Blink, DigitalReadSerial and Fade, the core's GetChipID, RepeatTimer, WiFiScan and WiFiClientConnect, the BLE library's Server and iBeacon, HTTPClient's BasicHttpClient | `emblink/esp32c3_devkitm1` |

`vendor_run.py` is what their runners share.

## Experimental Xtensa integer kernel

`xtensa_integer/run.py` uses a separate GNU Xtensa assembler and objcopy, plus
Python arithmetic, to check 9,728 wide integer instructions against the original
C kernel. Run with `--assembler PATH --objcopy PATH` on Linux with a host C
compiler. The expanded runner requires an actual ESP32-configured assembler
for its ISA options. No toolchain is vendored or linked, and these checks do
not qualify the complete ESP32 LX6 configuration.
See [scope and reproduction](../../docs/engineering/esp32-dual-core.md).

`xtensa_integer/run_execute.py` additionally uses a separate linker, with
relaxation disabled, for 4,800 flow/memory/density cases and 1,152 rejected bus
accesses. Supply `--assembler`, `--objcopy` and `--linker` paths. It compares all
registers, PC and exact bus transactions, including failed-access retention.
It does not establish architectural exception delivery or complete ISA fidelity.

`xtensa_integer/run_c.py` compiles unchanged sum/sort/copy C routines using
external CALL0 GCC and runs 96 cases through instruction fetch and the callback
bus. Supply `--compiler`, `--objcopy`, `--linker` and `--nm`. Checks include whole
buffers and guards, source retention, return PC and callee-saved registers. The
reference result used GCC xtensa-lx106 10.2.1 at `-O1`; this tests shared
instructions, not the full LX6 configuration, ESP-IDF boot or a real board.

`xtensa_integer/run_windows_c.py` uses the actual Espressif ESP32 windowed-ABI
compiler. Without vector input, 128 sum/sort/copy/nested-call executions pass.
With `--vectors-source` pointing to the hash-pinned ESP-IDF v4.4.7
`components/freertos/port/xtensa/xtensa_vectors.S`, 224 runs (160 C, 64 assembly)
execute all six vendor window spill/fill bodies via actual bus accesses and
RFWO/RFWU retries. The generated temporary assembly retains the source's
permissive copyright/permission notice; no vendor handler is linked into the
app. The runner validates the source SHA-256 before extraction. See the target
contract for exact toolchain/source hashes and commands. This qualifies selected
window/loop behavior, not general RTOS context switching, ESP-IDF boot or SoC
peripherals. Compiler/linker/objcopy/nm are external tools, not dependencies of
the runnable app.

`xtensa_integer/run_system_c.py` uses the actual ESP32 compiler for 7,040 shift,
rotation and funnel runs (37,818 fetched instructions), checking functional bit
results and caller stack/window/return state. Supply `--compiler`, `--objcopy`,
`--linker` and `--nm` as for the windowed runner. This is not interrupt, cache,
pipeline or hardware timing qualification.

`xtensa_integer/run_interrupts.py` validates the hash-pinned actual ESP32
`core-isa.h` and checks 2,048 input type/level/mask combinations.
`run_timer_clock.py` uses its timer-input mapping for 8,192 counter/deadline
comparisons against 263,719 individual oracle ticks. Both take `--core-isa PATH`
and compile the original C modules with the host compiler; the vendor header
is external reference data, not linked into the app. These checks do not qualify
SoC routing, instruction timing, NMI sampling or interrupt-service latency.

`xtensa_integer/run_atomic_c.py` compiles real ESP32 exchange/CAS/increment and
division functions: 832 executions, including 64 pairs of instruction-interleaved
execution states forcing a CAS retry. Supply `--compiler`, `--objcopy`, `--linker`
and `--nm`. Its callback is a serialized fixture, not a host-thread-safe shared
SoC RAM model or proof of full SMP/coherence.

`xtensa_integer/run_machine_c.py` uses the same actual ESP32 tools to execute two
compiled atomic counters and spinlocks protecting ordinary writes through native
internal RAM, acquire/release operations and CAS, without
Python memory callbacks: 512 task executions, 128 deterministic schedule replays,
229,322 instructions. It checks counts, sums, wrap, return/window/stack state and
retirement. Supply `--compiler`, `--objcopy`, `--linker`, `--nm`. This is a
serialized functional harness, not DPORT/ROM boot, cache/coherence, cycle timing
or full SMP qualification.

`xtensa_integer/run_matrix.py` takes `--dport-reg`, `--soc-header` and `--core-isa`
external hash-pinned ESP-IDF v4.4.7/toolchain headers. Parsed vendor register
addresses/defaults, source numbers and CPU input types drive 4,288 routing and
512 combined-source/mask comparisons against the native controller. No vendor
implementation is linked. Reproduction and header hashes are in the target
contract linked above.

`xtensa_integer/run_ipi_c.py` takes the actual ESP32 `--compiler`, `--objcopy`,
`--linker` and `--nm`. A compiled C sender uses DPORT MMIO to wake a WAITI peer;
an assembled context-saving ISR acknowledges its source and publishes a release
ACK through an actual ESP32-compiled C handler (windowed ABI). Both PRO-to-APP
and APP-to-PRO directions pass. No Python memory/interrupt callbacks:
256 mailbox runs, 4,224 interrupts,
128 exact replays, 512,128 retired instructions. Checks messages, source clear,
stack guards, general registers and ABI/window return. Not full ISR/RTOS, PID,
peripheral generators, APB timing or SoC boot qualification.

`xtensa_integer/run_controls.py` parses the same pinned `--dport-reg` and
`--soc-header` for APP clock/stall/boot-address register addresses, widths and
defaults: 384 field comparisons across both CPU bus views. Reset/ROM boot are
not qualified. Add `--exercise-controls` to `run_ipi_c.py` to have compiled PRO
firmware suspend/resume APP during message/interrupt traffic: 256 runs,
4,224 interrupts, 128 exact replays and 551,232 retired instructions. It checks
unchanged PC/registers on suspended turns and final control fields. Its counted
suspended turns are instruction attempts, not physical clock cycles. All machine
fixture runners now explicitly enable the documented APP clock field.

`xtensa_integer/run_ps.py` takes actual ESP32 `--assembler` and `--objcopy` tools.
It passes 3,456 PS read/write/exchange and RFI cases against a Python field/register
oracle, checking every operand register and reserved-bit masking. It does not
qualify full privilege/MMU behavior, physical RSYNC latency or processor reset.

`xtensa_integer/run_vectors.py` takes `--core-isa`, `--assembler` and `--objcopy`:
3,072 assembled VECBASE accesses, 960 parsed-vendor-offset dispatches and 128
static reset-vector comparisons. Aligned bases only; no low-bit/lock or complete
silicon reset qualification. Add `--include-reset` to `run_controls.py` to include
the opt-in APP reset register (512 total vendor-field comparisons).

`xtensa_integer/run_reset_c.py` takes the actual ESP32 `--compiler`, `--objcopy`,
`--linker` and `--nm`. Compiled PRO code releases APP into an original temporary
test-ROM program that initializes the supported CPU state, reads BOOT_ADDR and
calls a compiled worker: 128 cycles, 128 static ROM entries, 16 exact replays,
11,904 retired instructions. No Python memory callback or host APP PC shortcut.
This is not Espressif ROM, full reset/MMU/cache, SDK or board boot qualification.
See [reset/vector scope](../../docs/engineering/esp32-reset-vectors.md).

`xtensa_integer/run_flashmap.py` takes `--dport-reg`, `--soc-header`, `--core-isa`
and the pinned `--mmu-header`: 488 parsed-vendor translation comparisons, six
initial-control/flush comparisons and per-window mask checks. `run_flash_c.py`
takes the actual ESP32 compiler/objcopy/linker/nm paths: 256 compiled workers
execute different physical code/data at identical virtual addresses on the two
cores (64 exact replays, 11,264 retired instructions, 2,048 from flash).

Add `--flash-worker` to `run_reset_c.py` for compiled PRO MMU/cache setup followed
by original test-ROM handoff into a mapped worker: 128 cycles, 16 replays,
15,360 retired instructions (1,664 from flash). Neither path uses Python memory
callbacks. These checks qualify immutable PID0/1 mappings with disabled-cache
edits and explicit flush; they do not qualify physical flash/SPI, live remap,
cache tags/timing or real ROM/SDK boot. See the
[flash profile contract](../../docs/engineering/esp32-flash-mapping.md).

`xtensa_integer/run_context.py` takes the pinned `--core-isa`, actual ESP32
`--assembler` and `--objcopy`: 7,680 independent all-register THREADPTR/MISC/PRID
comparisons. `run_tls_c.py` takes the actual ESP32 compiler/objcopy/linker/nm,
plus hash-pinned ESP-IDF v4.4.7 `--port-source` and `--cpu-ll` files. Actual
local-exec C TLS runs through native RAM: 1,536 calls, six persistent task banks,
16 replays and 52,224 retired instructions. PRID is an explicit test input,
not a silicon value claim. Neither runner qualifies a scheduler or SDK boot.
See [context register scope and reproduction](../../docs/engineering/esp32-context-registers.md).

`xtensa_integer/run_alloca.py` takes actual ESP32 `--assembler` and `--objcopy`:
32,768 MOVSP operand/window/AllocaCause comparisons. Add `--dynamic-stack` to
`run_tls_c.py` for compiler-generated variable-size stack buffers alongside TLS:
1,536 calls/MOVSP executions, 16 replays and 253,812 retired instructions. The
compiled path keeps caller windows resident; native/assembler tests cover the
missing-caller exception. Complete vendor alloca-handler restoration remains
unqualified. Reproduction is in the context register contract above.

`xtensa_integer/run_flashspi.py` takes pinned ESP-IDF v4.4.7 `--spi-reg` and
`--soc-header`: 27 parsed register defaults and 384 read/address/IRQ comparisons.
`run_spi_c.py` takes actual ESP32 compiler/objcopy/linker/nm plus pinned
`--ll-header` and `--struct-header`. Seven unchanged SDK helper bodies drive
128 native-bus SPI1 reads; 32 also load and call a RAM worker. Thirty-two exact
replays retire 91,568 instructions. This is an opt-in single-line 03h read profile,
not flash programming, DMA, physical timing or actual ROM/SDK boot. See the
[SPI1 read contract and reproduction](../../docs/engineering/esp32-spi1-reads.md).

`xtensa_integer/run_w25q32.py --datasheet` hash-checks the external Winbond
W25Q32JV Rev.G PDF (needs `pdftotext`) and parses timing maxima and IQ ID:
2,631 command/time comparisons, 219 completed operations, 37 ignored writes.
`run_spi_c.py --program-workers` adds actual compiled write-enable/program/status
polling before reading back and calling generated code: 32 workers, 32 exact
replays, 125,856 retired instructions. Time is an explicit external fixture,
not derived from instruction counts. This named unprotected chip profile is not
a board BOM, full chip or ROM/SDK qualification. See
[programming scope and reproduction](../../docs/engineering/esp32-flash-programming.md).

`xtensa_integer/probe_rom_boot.py --rom --flash [--report]` is a diagnostic,
not a passing vendor qualification. With the pinned Espressif QEMU PRO ROM and
an actual unmodified ESP-IDF v4.4.7 hello_world image, native execution fails
after seven retired instructions at the missing eFuse read. The same image
boots both schedulers and the application in Espressif QEMU. The probe returns
1 for blocked/inconclusive boot, and does not invent MMIO or jump past reset.
See [boot verification and artifacts](../../docs/engineering/esp32-boot-verification.md).

`xtensa_integer/run_efuse.py` takes pinned ESP-IDF v4.4.7 `--efuse-reg`,
`--efuse-ll`, `--soc-header` and actual ESP32 compiler/objcopy/linker/nm tools:
3,968 register/protection comparisons and 128 compiled C calls/5,248 instructions
through seven unchanged SDK helpers. Explicit synthetic None-coded images only;
no owner keys or full burn/SDK qualification. Add `--efuse FILE` to the ROM probe
for the [new eFuse profile](../../docs/engineering/esp32-efuse-read-profile.md).

`run_w25q32.py --part gt25q32 --datasheet FILE` uses the hash-pinned Giantec V2.5
PDF and an independent array/time oracle. `run_spi_c.py --program-workers
--flash-part gt25q32 --flash-tick-period 257` stresses actual firmware BUSY reads:
128 reads, 32 programmed workers, 32 replays, 139,520 instructions, 432 observed
BUSY reads. The external-time schedule is artificial, not processor timing.
See [Giantec scope](../../docs/engineering/esp32-giantec-profile.md).
