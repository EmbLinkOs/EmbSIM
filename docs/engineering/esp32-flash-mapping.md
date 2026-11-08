# Original ESP32: read-only mapped-flash profile

This opt-in profile adds firmware-visible flash mappings to the
[experimental ESP32 foundation](esp32-dual-core.md). Compiled code can configure
MMU entries and execute/read through them on both cores. It does not register a
board target, emulate a physical flash chip or establish real ROM/SDK boot.

## References and scope

Addressing and controls come from ESP32 TRM v5.9 sections 3.3.3–3.3.4,
4.3.2.2 and chapter 12, plus pinned ESP-IDF v4.4.7 headers:

- [soc/mmu.h](https://github.com/espressif/esp-idf/blob/v4.4.7/components/soc/esp32/include/soc/mmu.h),
  SHA-256 `59842853da8931e4a5fd61ec0cad565a38b691d53a96110439d9d0aadaf920b9`.
- [soc/dport_reg.h](https://github.com/espressif/esp-idf/blob/v4.4.7/components/soc/esp32/include/soc/dport_reg.h)
  and [soc/soc.h](https://github.com/espressif/esp-idf/blob/v4.4.7/components/soc/esp32/include/soc/soc.h),
  with hashes recorded in the main foundation document.
- The pinned ESP32 `core-isa.h` supplies instruction/data bus window names and
  addresses; its hash is also recorded there.

`esk_esp32_fabric_flash` copies a provided backing image of 1..256 whole 64 KiB
pages, at most 16 MiB. The supplied extent is authoritative; missing bytes,
capacity, a JEDEC identity or a board's flash part are not inferred. The private
image is immutable during mapped execution. Replacement requires both caches
disabled, preserves mappings and requires a fresh flush before re-enable.
Allocation failure preserves the old image and all controller state. No SPI
read/program/erase protocol, write protection, quad mode, encryption, secure boot
or physical flash loading is implemented by this interface.

The profile qualifies normal-mode PID0/1 mappings. A PID controller, PID2..7,
single-IRAM mode, PSRAM, alternate page modes, locks, cache pool mux/reuse,
cache tags/replacement and cache/SPI cycle timing remain unsupported. Changes to
unimplemented defined control fields fail without changing the register.

## Virtual and physical mappings

Each core has 256 implemented entries with 64 KiB pages. Bits 7..0 select one
of 256 physical pages. Bit 8 set means invalid. Other entry bits must remain
zero in this profile. Entries start at the invalid encoding `100` as a fixture
policy, not a claim about silicon MMU-table reset contents.

| Region | Virtual addresses | Local entries | Cache mask |
| --- | --- | --- | --- |
| DROM0 data | `3F400000–3F7FFFFF` | 0..63 | CTRL1 bit 4 |
| IRAM0 instruction window | `40000000–403FFFFF` | 64..127 | CTRL1 bit 0 |
| IRAM1 instruction window | `40400000–407FFFFF` | 128..191 | CTRL1 bit 1 |
| IROM0 instruction window | `40800000–40BFFFFF` | 192..255 | CTRL1 bit 2 |

On-chip memory retains priority in the first instruction window. Only
`400C2000` and above can reach flash: ROM, internal SRAM, cache-pool holes and
RTC fast memory below that address cannot be replaced by a flash table entry.
APP cannot gain access to PRO-only RTC fast memory through this mapping.

DROM0 supports data bytes, halfwords and words; instruction fetch is forbidden.
Instruction windows support fetch bytes, including cross-page instructions, and
aligned data words for literal loads. Narrow data accesses there are unsupported.
Data accesses require natural alignment. Reads preflight all requested bytes;
invalid/masked entries or physical addresses outside the supplied extent fail
without returning fabricated values or wrapping to another page. Writes and CAS
through mapped flash are unsupported. These failures remain ordinary emulator
bus diagnostics; hardware stall/MMU-error interrupt behavior is not modeled.

| Interface | PRO addresses | APP addresses |
| --- | --- | --- |
| Implemented MMU table | `3FF10000–3FF103FC` | `3FF12000–3FF123FC` |
| CACHE_CTRL | `3FF00040` | `3FF00058` |
| CACHE_CTRL1 | `3FF00044` | `3FF0005C` |

Either CPU's privileged PID0/1 bus view can program either table. Other table
ranges remain unsupported. MMIO requires aligned 32-bit data access; MMIO fetch
and atomic stores are not supported.

## Access gates and flush sequence

CACHE_CTRL starts with documented field value `10`: cache disabled, FLUSH_ENA
set and FLUSH_DONE clear. CACHE_CTRL1 starts at `8FF`, with the supported bus
masks set. Read-only/reserved control bits ignore writes. Only cache enable,
flush and the four flash bus masks are implemented; unqualified defined RW
fields must retain their documented default values.

A supported mapping sequence is:

1. Execute configuration code from internal RAM and disable the destination cache.
2. Write the destination table and clear the required CTRL1 bus masks.
3. Clear FLUSH_ENA, then set it while the cache remains disabled.
4. Observe FLUSH_DONE and enable the cache.

Flush completion is synchronous in this functional profile. No busy duration,
power-on flush progression, hit/miss latency or instruction-to-clock conversion
is claimed. A self-disabling access commits before a subsequent mapped fetch
fails. Internal RAM/ROM accesses remain available independently of flash masks.

**Live cache remapping is not qualified.** Hardware/SDK software can update
mappings with appropriate invalidation, but this profile rejects table writes
while the destination cache is enabled and requires a fresh flush after changes.
It also rejects starting flush with an enabled cache. Immutable backing plus
this restricted transition sequence avoids inventing live-remap coherence or
cache contents. Dynamic `spi_flash_mmap`/cache-invalidate compatibility needs
further implementation; successful reads here do not establish that support.

CPU-only reset leaves this provider state outside the CPU within the current
model. Broader reset/retention domains remain unqualified. Actual Espressif ROM,
SPI boot/loading, cache SRAM pool access and complete SDK startup are still
missing. The [bounded reset profile](esp32-reset-vectors.md) continues to fetch
the static reset entry; attaching flash never creates a ROM or boot-address PC
shortcut.

## Acceptance and reproduction

- `test_esp32_flashmap`: 7,289 assertions across both complete normal-mode tables,
  all physical pages in a 16 MiB backing, masks, cross-page fetches, on-chip
  precedence, invalid/unsupported operations, extent bounds, private-copy
  semantics and transactional allocation/replacement failures.
- `run_flashmap.py`: 488 translations using parsed vendor window/table facts,
  plus six parsed initial-control/flush comparisons and per-window mask checks.
- `run_flash_c.py`: 256 actual ESP32-compiled mapped workers, 64 exact PC/result
  replays and 11,264 retired instructions, including 2,048 instructions fetched
  from flash. Compiled internal-RAM code programs each core's table/controls;
  the same virtual code/data addresses then resolve to different physical pages
  and return the expected per-core results. No Python memory callbacks.
- `run_reset_c.py --flash-worker`: compiled PRO code configures APP's MMU/cache
  while reset is held, then releases it into the original test-ROM program.
  Firmware reads BOOT_ADDR and calls the mapped C worker. It passes 128 reset
  cycles, 128 static ROM entries, 16 exact PC/event replays and 15,360 retired
  instructions (1,664 from mapped flash). Its 5,904 held-reset turns are
  instruction attempts, not physical cycles. Shared-state, source and stack
  checks remain in effect. The test ROM is original fixture code, not Espressif
  ROM or evidence of real cold boot.

```sh
python tests/vendor/xtensa_integer/run_flashmap.py \
  --dport-reg /path/to/esp-idf/components/soc/esp32/include/soc/dport_reg.h \
  --soc-header /path/to/esp-idf/components/soc/esp32/include/soc/soc.h \
  --mmu-header /path/to/esp-idf/components/soc/esp32/include/soc/mmu.h \
  --core-isa /path/to/xtensa-esp32-elf/include/xtensa/config/core-isa.h
python tests/vendor/xtensa_integer/run_flash_c.py \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld \
  --nm /path/to/xtensa-esp32-elf-nm
python tests/vendor/xtensa_integer/run_reset_c.py \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld \
  --nm /path/to/xtensa-esp32-elf-nm \
  --flash-worker
```
