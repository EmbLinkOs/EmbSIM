# ESP32 explicit W25Q32JV programming profile

**Implemented: a bounded, explicitly selected W25Q32JV-IQ unprotected 4 MiB
standard-SPI profile, connected to experimental SPI1. This does not identify the
requested board's actual flash chip or add real ROM/ESP-IDF boot.**

## Real operation

NOR programming clears bits; erase restores the selected region to ones. Page
Program needs Write Enable and wraps within a 256-byte page. Erase aligns to the
selected sector/block. The chip runs its own self-timed operation after the SPI
transaction completes. Firmware polls status BUSY, and Write Enable is cleared
when the program/erase finishes. A controller's transaction completion and the
flash chip's programming completion are separate events.

The model implements this behavior from Winbond W25Q32JV Revision G, March 27,
2018. IQ/JQ identifies as EF 40 16; IM/JM has a different type byte and is not
silently substituted. The requested TECNOIOT board's flash marking/BOM remain
unverified. Selecting this part is an explicit test configuration, not a board
identity conclusion.

## Qualified subset

`w25q32.c` is an original protocol/state implementation with no ESP32 register
addresses. The caller owns its 4 MiB storage. `flashspi.c` supplies the separate
controller/register bridge. Supported standard-SPI commands:

| Command | Behavior |
|---|---|
| 03h Read Data | Sequential bytes within the provided array; idle only |
| 05h Read Status-1 | Repeated BUSY bit 0 / WEL bit 1; upper bits fixed to the explicit unprotected fixture configuration |
| 06h / 04h | Set/clear Write Enable when idle; ignored during BUSY |
| 9Fh | Exactly three IQ-profile JEDEC ID bytes, idle only |
| 02h | 1..256-byte Page Program, page wrapping, bitwise one-to-zero programming |
| 20h / 52h / D8h | Aligned 4 KiB / 32 KiB / 64 KiB erase |
| C7h / 60h | Whole-array erase |

Writes without WEL complete their SPI transaction but leave the array and chip
state unchanged. Accepted program/erase sets BUSY; supported no-receive commands
are ignored while BUSY. Status reads remain available. An ignored data/ID read
has unqualified electrical output; the model fails it explicitly instead of
inventing a pulled-up byte. Unsupported command shapes, receive extents or
operations also fail without changing output bytes or device state.

All 256-byte page offsets, wrap and repeated bit-clearing behavior are tested.
This functional bit model does not qualify manufacturer limits on repeated
programming, endurance, disturb, retention, partial power failure or damage.
Longer-than-256-byte program frames, array-end read wrapping/address aliases,
fast/dual/quad/QPI, SFDP, status2/3, protection/status writes, security/OTP,
suspend/resume, chip reset/power-down, startup timing, supply thresholds and
physical SPI/WP/HOLD waveforms remain unimplemented. Protection writes are
rejected rather than accepted without protecting memory. This is not a complete
W25Q32JV component model.

## Time and storage

The deterministic timing fixture uses the datasheet's **maximum** durations:

| Operation | Supplied time to completion |
|---|---|
| Page Program | 3,000 us |
| 4 KiB erase | 400,000 us |
| 32 KiB erase | 1,600,000 us |
| 64 KiB erase | 2,000,000 us |
| Chip erase | 50,000,000 us |

These are conservative fixture durations, not measured hardware timing or a
claim that every physical operation takes the maximum. Typical timing, voltage/
temperature variation and hardware calibration are not qualified. External time
is advanced with `esk_esp32_fabric_flash_advance`; `flash_deadline` reports the
remaining microseconds, with zero meaning idle. No instruction count, wall clock
or APP CPU clock gate implicitly advances or stops the chip's clock.

Program data is latched when accepted. The model commits array changes at
completion; intermediate cell states/power interruption are unqualified. Normal
reads cannot observe the intermediate array, and status stays BUSY until the
supplied deadline. Excess advance time is discarded after the single pending
operation completes; arithmetic does not wrap for a maximal host advance.

## ESP32 integration

Install exactly 4 MiB of backing, enable the SPI1 attachment, then explicitly
call `esk_esp32_fabric_w25q32_configure`. A different backing extent is rejected.
The default immutable attachment retains its previous behavior. Named-part
attachment/detachment requires both caches disabled and an idle chip. Changing
attachment resets chip fixture status, not the array or SPI controller state;
this host setup action is not a silicon power/reset claim. Image replacement is
rejected while the named part is attached. CPU-only APP reset preserves the
shared chip state, pending operation, data and independent time.

The selected profile expands SPI1 USER command, address, MOSI and MISO phases.
Its data buffer limits each transaction to 64 bytes, or 32 bytes in the selected
high buffer. Firmware must split programming at both page and controller-buffer
boundaries. Both caches must be disabled for every SPI1 transaction because the
SPI0/1 arbiter is still unqualified. Dedicated hardware flash commands and DMA
remain unsupported. Physical peripheral clocking/pin mux are not added.

The SPI1 completion flag/source 29 reports transaction completion, including
ignored writes without WEL. It does not become a separate chip-completion IRQ.
New programming invalidates the functional cache profile. Re-enable is rejected
while BUSY, even if firmware flushes early. Completion marks both caches dirty
again: a fresh clear/set flush after completion is required before enabling a
mapped view of the modified bytes. This is a conservative coherence contract,
not a hardware cache-tag or live SDK invalidation implementation.

## Evidence and reproduction

`test_esp32_w25q32` passes 1,205,963 native assertions, covering every page offset,
lengths through a full page, latches, ignored commands, exact deadline boundaries,
erase extents, JEDEC ID, unchanged failures, high transmit/receive buffers,
CPU-only reset preservation and post-program cache flush requirements.

`run_w25q32.py` hash-checks the external datasheet, parses its maximum durations,
command codes and IQ identity, and compares against an independent Python array
and command/time oracle: 2,631 comparisons, 219 completed operations and 37
writes ignored without WEL. Full array contents are checked before and after
completion. Neither vendor implementation nor datasheet contents are vendored.

`run_spi_c.py --program-workers` uses the actual ESP32 compiler and the same
seven unchanged SDK helper bodies as the [read profile](esp32-spi1-reads.md).
Compiled firmware holds a shared-controller atomic lock, enables writes, splits
at page boundaries, programs supplied code, polls BUSY, reads it back and copies
it to instruction RAM before calling it. Evidence: 128 reads, 32 programmed RAM
workers, 32 exact replays, 125,856 retired instructions and 6,144 scheduled turns
with the chip busy. The external test fixture supplies 1 ms time events every
37 scheduled turns; this artificial schedule is not a processor clock model.
There are no Python memory callbacks or host worker-PC shortcuts. The supplied
RAM payload is a generated test input, not a ROM/bootloader or serial programmer.

Primary reference: [Winbond W25Q32JV Revision G](https://www.winbond.com/resource-files/w25q32jv%20revg%2003272018%20plus.pdf),
inspected 2026-11-05, sections 7/8 and AC characteristics. PDF SHA-256:
`b2585095ac503e7cee7916ab3f025a51699b49e2cad7f6782e92b828c1cc09d8`.
Use the pinned Espressif tools and SDK headers in the read-profile contract.

```sh
python3 tests/vendor/xtensa_integer/run_w25q32.py \
  --datasheet /path/to/w25q32jv-revg.pdf
python3 tests/vendor/xtensa_integer/run_spi_c.py --program-workers \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld \
  --nm /path/to/xtensa-esp32-elf-nm \
  --ll-header /path/to/spi_flash_ll.h --struct-header /path/to/spi_struct.h
```
