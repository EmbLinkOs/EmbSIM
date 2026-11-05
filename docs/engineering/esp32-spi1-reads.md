# Original ESP32 SPI1 immutable-flash read profile

**Implemented: opt-in single-line SPI1 USER 03h reads into the hardware data
buffer, with functional completion status and matrix routing. No runnable board,
actual ROM/SDK boot, flash programming or physical SPI timing is added.**

## Real operation and supported configuration

ESP32 has four SPI controllers. SPI0 services mapped external-memory accesses;
SPI1 shares its external signal bus through an arbiter. SPI1 firmware can instead
configure a command, address and receive phase, start USER, poll completion and
copy the receive buffer. This path is needed before mapped flash is available.

The model's attachment is the same immutable, privately copied backing installed
by `esk_esp32_fabric_flash`. No JEDEC ID, chip capacity, erased extent or PCB flash
part is inferred. `esk_esp32_fabric_spi1_configure` explicitly enables/disables
this attachment; before enabling, SPI1 MMIO remains unsupported. Disabling and
re-enabling preserve its state. The standalone machine has no configured pin mux,
flash wiring, peripheral clocks or global reset domains; their correctness is
not implied by opting into this functional fixture.

Qualified transfers require both CPU caches disabled, single-line slow-read
configuration, CS0 only, an eight-bit 03h command, a 24-bit address, no dummy or
MOSI phase, default clock/timing fields and byte-sized receive data. Requests
receive 1..64 bytes through W0..15, or 1..32 bytes through W8..15 with
MISO_HIGHPART. USER addresses are left-aligned: the transmitted 24-bit address is
`ADDR >> 8`, independent of the low padding byte. SDK
`spi_flash_ll_set_usr_address` supplies ones in that padding byte.

Every received byte must fall within the supplied backing. Missing/out-of-range
bytes fail without wrapping, padding or invented data. Both caches must be
disabled because SPI0/1 arbitration is unqualified. The model rejects fast/dual/
quad/three-wire/full-duplex, bit/byte reordering, held CS, DMA, dedicated hardware
commands, identification/status/program/erase/power/security operations and other
unqualified modes. An unsupported transaction changes neither the receive buffer
nor completion/counter state. A command is not reported successful merely
because its start bit was written.

## Register and completion contract

Aligned 32-bit data access only; MMIO fetch and CAS fail. Supported registers:

| Register offset from SPI1 0x3ff42000 | Behavior |
|---|---|
| 0x00 CMD | USER bit 18 starts the qualified operation; completion clears it |
| 0x04 ADDR | Full-width storage, high 24 bits used by this USER profile |
| 0x08 CTRL | Slow/fast selection storage with other defined fields restricted to reset values; only slow read transfers qualify |
| 0x18 CLOCK | Documented reset value; other writes unsupported |
| 0x1c USER | Supported phase/high-buffer configuration; other defined fields restricted to reset values |
| 0x20 USER1 | 24-bit address configuration and dummy-length storage; dummy phase unsupported |
| 0x24 USER2 | Command width/value storage; unsupported command/width rejected at start |
| 0x28 / 0x2c MOSI/MISO_DLEN | 24-bit length fields; MOSI phase unsupported; receive lengths checked at start |
| 0x34 PIN | Reset CS0 configuration; other defined modes unsupported |
| 0x38 SLAVE | TRANS_DONE raw flag, its enable and four-bit operation counter |
| 0x80..0xbc W0..15 | Full-width shared data buffer |

The SDK register comment calls USER1's address length read-only, while its helper
writes it. This profile qualifies only unchanged 24-bit writes and rejects other
lengths rather than guessing silicon behavior. Reserved bits of supported
registers are ignored/zero; unimplemented registers remain unsupported. Writable
unsupported fields fail unchanged. Receive bytes pack into consecutive low-to-high
byte lanes as the SDK buffer-copy path expects. Unclocked bytes retain deterministic
fixture values, which are not a silicon retention guarantee.

A successful read completes synchronously, sets TRANS_DONE, increments the
four-bit operation counter and clears CMD. This is a functional abstraction,
without an observable busy interval or clock/CS waveform claim. TRANS_DONE and
its enable are ordinary writable bits; firmware clears raw completion by writing
zero. The level request is raw AND enable, on original-ESP32 matrix source 29.

The machine owns source 29 while the profile is enabled and rejects host input
injection into that slot. Routing is synchronized after instruction commit;
host writes through the machine bus synchronize immediately. Both CPUs see the
same controller/buffer; software must arbitrate access. CPU-only APP reset
preserves the shared SPI state/request within the existing bounded reset model.
Disabling the attachment relinquishes the source at the next synchronization.
Raw fabric/debug changes require the next machine synchronization; they are not
independently scheduled hardware events. Other sources retain their host-input
behavior.

## Evidence and limitations

`test_esp32_flashspi` passes 37,466 assertions covering transfer lengths, high/low buffers, ignored address
padding, completion/enable/counter wrap, unsupported controls/commands, missing
backing/bounds, cache restrictions, unchanged failures, opt-in ownership,
shared bus access, CPU-only reset preservation and routing to both CPUs. A
fetched S32I starts a read, retires once, and then enters its level-2 completion
interrupt on the following step with the correct saved PC.

`run_flashspi.py` independently parses 27 register reset values and field
positions from pinned SDK headers and passes 384 address/receive/IRQ comparisons,
including crossing 64 KiB boundaries and exact backing-end reads. No vendor
controller implementation is linked into the runtime.

`run_spi_c.py` compiles original C using the actual ESP32 compiler, the unmodified
SDK register struct and seven unchanged selected SDK helper bodies. Both cores
serialize their shared-controller reads using compiled acquire/exchange/release
atomics. They copy received bytes to native RAM, verify a checksum and clear
completion. Some runs copy a generated code image across a flash-page boundary,
write it to instruction RAM with word stores, and call its compiled entry.

Evidence: **128 compiled reads, 32 SPI-loaded RAM workers, 32 exact replays and
91,568 retired instructions**. Expected receive bytes, checksum/function results,
stack/window return, cleared lock/source and allocation cleanup are checked.
There are no Python memory callbacks or host worker-PC shortcuts. Selected
helper execution is not the whole SDK initialization/common-command stack, an
actual Espressif ROM, full interrupt-driven driver or bootloader acceptance.
Retired instructions are not physical cycles. No hardware capture was collected.

## Optional named programming attachment

The default read-only profile above keeps its existing contract. An explicitly
selected [W25Q32JV-IQ fixture](esp32-flash-programming.md) adds a bounded named-chip
command backend, transmit phases and separately advanced programming time. Its
scope and omissions are separate; this does not retroactively qualify arbitrary
chips, the board BOM, full SDK initialization or physical SPI timing.

## References and reproduction

Inspected 2026-11-05: ESP32 TRM v5.9 chapter 20, and these external ESP-IDF v4.4.7
files. They are hash-checked test inputs, not runtime dependencies:

| Source | SHA-256 |
|---|---|
| [soc/esp32/include/soc/spi_reg.h](https://github.com/espressif/esp-idf/blob/v4.4.7/components/soc/esp32/include/soc/spi_reg.h) | `8fdea138d074362521d9e052894ce3c775326e95006c51ff8a84c2821a715a70` |
| [soc/esp32/include/soc/spi_struct.h](https://github.com/espressif/esp-idf/blob/v4.4.7/components/soc/esp32/include/soc/spi_struct.h) | `97fffd958e992ea25af94e08bb6e5b6c7487671b3e070efa68e80d216db383b2` |
| [hal/esp32/include/hal/spi_flash_ll.h](https://github.com/espressif/esp-idf/blob/v4.4.7/components/hal/esp32/include/hal/spi_flash_ll.h) | `d1331e0d99ff2377e80e7ec4b8f314453345e68d894f5d2b3139c0e8069ae724` |
| [soc/esp32/include/soc/soc.h](https://github.com/espressif/esp-idf/blob/v4.4.7/components/soc/esp32/include/soc/soc.h) | `7a7148c288d7c289c961810e5617fa7b14d105b28349a45f772150b1e0845dd3` |

The temporary helper extraction retains Espressif's Apache-2.0 notice, and the
register struct is copied unchanged into the temporary build. No SDK
implementation is vendored in this repository. See the
[target investigation](esp32-dual-core.md) for the pinned Espressif toolchain.

```sh
python3 tests/vendor/xtensa_integer/run_flashspi.py \
  --spi-reg /path/to/spi_reg.h --soc-header /path/to/soc.h
python3 tests/vendor/xtensa_integer/run_spi_c.py \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld \
  --nm /path/to/xtensa-esp32-elf-nm \
  --ll-header /path/to/spi_flash_ll.h --struct-header /path/to/spi_struct.h
```
