# Original ESP32 per-core SDK mask-ROM profile

**Implemented: explicit per-core 448 KiB mask-ROM images, instruction access
through 0x4006ffff and a coherent final-64-KiB DROM alias. The default TRM-only
384 KiB instruction view remains available. This is not physical ROM-content
verification, APP reset/SDK boot, complete SoC mapping or calibrated timing.**

## Resolving the mapping discrepancy

The ESP32 TRM v5.9 table 3.3-2 calls 0x40060000..0x4006ffff reserved and lists
384 KiB ROM0 plus 64 KiB ROM1. The previous fabric implemented that static table.
However, ESP-IDF v4.4.7 `soc.h` defines `SOC_IROM_MASK_HIGH` as 0x40070000, and
its ROM linker script provides actual functions in the extension, including
`tdefl_get_prev_return_status` at 0x400608d0 and `tdefl_get_adler32` at 0x400608d8.

Espressif QEMU's `hw/xtensa/esp32.c` independently confirms the SDK image layout:
448 KiB IROM at 0x40000000, with DROM at 0x3ff90000 aliasing its last 64 KiB.
It has distinct CPU-specific ROM regions and loads separate PRO/APP images.
The native ROM's actual CALL8 at 0x40007729 enters 0x40065c18, previously outside
the fabric window. Instruction bytes are present there in the external image.

This change adds a named, explicit SDK/QEMU mapping profile. It does not silently
reinterpret the TRM-only fixture or claim the TRM reserved range is universally
usable on every silicon revision. Neither a simulator reference image nor the
owner's chip/flash ID authenticates their physical mask-ROM contents.

## Observable behavior and internal model

`esk_esp32_fabric_mask_rom(f, cpu, data, length)` requires exactly 0x70000 bytes
for the selected CPU. The API copies those supplied bytes; it fabricates no ROM,
missing tail or peer image. PRO and APP may use different inputs. Each image has
two coherent, read-only views:

| View | Address range, inclusive | Image offset | Supported access |
| --- | --- | --- | --- |
| IROM | 0x40000000..0x4006ffff | 0..0x6ffff | Fetch and aligned data words |
| DROM | 0x3ff90000..0x3ff9ffff | 0x60000..0x6ffff | Data bytes/halfwords/words; no fetch |

Low-32-KiB boot remap still takes precedence and uses the existing reversed-word
SRAM view. It does not redirect the extension or DROM. ROM stores and RAM-load
attempts fail without modifying either view; CAS/atomic memory classification
remain SRAM-only. Narrow instruction-window data operations, unaligned data
words, unsupported fetches and accesses crossing the end of the window fail
without changing the caller's output. Adjacent cache RAM remains unsupported.

`NULL, 0` detaches one core's profile, exposing its legacy ROM0/ROM1 backing
again. The other core, SRAM, RTC memory and remap controls survive. Replacement
allocates/copies first, so allocation failure preserves the old image, both
aliases and allocator accounting. Legacy ROM installation stays independent;
it does not overwrite an attached profile.

Each CPU uses one immutable backing allocation for both views. This models
aliasing directly instead of keeping two independently mutable copies. It is an
original C99 implementation of the mapping facts; no QEMU implementation code,
GPL runtime or vendor ROM bytes are copied into this repository.

This is a host setup/debug API. Configure with CPUs paused, before initializing
CPU profiles or after explicitly invalidating their instruction buffers. The
fabric does not reset/invalidate CPU state, perform silicon ROM writes or invent
cache-coherence behavior when replacing a test image. Calls are serialized and
must not re-enter. Full cache/MPU/power/reset-domain behavior, address-decoder
latency, instruction timing and physical ROM authenticity remain unqualified.

## Proof and reproduction

- **1,170 native assertions** check per-core selection, IROM/DROM content,
  alias edges, mixed-width fetch across the old boundary, copy isolation, RO/CAS
  rejection, output preservation, low-ROM remap, peer retention, legacy fallback,
  invalid inputs, allocation failure and leak-free teardown.
- **18,840 independent per-core fetch/data/alias comparisons** use separate
  pinned external PRO/APP images, random offsets and their differing bytes.
  The reference parses the SDK range and checks the pinned QEMU mapping/alias.
- **1,024 actual ESP32 C calls / 16,896 retired instructions** execute two
  unchanged external ROM getter bodies on both native cores. The compiler uses
  the unmodified SDK miniz header to write the real structure fields, then calls
  the ROM symbols at their linked addresses. All four defined status values and
  full-width Adler fields are exercised. Host code does not implement a getter
  callback or skip its PC. This qualifies those getters and mappings, not the
  compression engine, all ROM routines or APP reset/SDK boot.

```sh
python3 tests/vendor/xtensa_integer/run_mask_rom.py \
  --pro-rom /path/to/esp32-v3-rom.bin \
  --app-rom /path/to/esp32-v3-rom-app.bin \
  --soc-header "$IDF_PATH/components/soc/esp32/include/soc/soc.h" \
  --rom-symbols "$IDF_PATH/components/esp_rom/esp32/ld/esp32.rom.ld" \
  --miniz-header "$IDF_PATH/components/esp_rom/include/esp32/rom/miniz.h" \
  --qemu-source /path/to/esp32.c \
  --compiler xtensa-esp32-elf-gcc --objcopy xtensa-esp32-elf-objcopy \
  --linker xtensa-esp32-elf-ld --nm xtensa-esp32-elf-nm
```

External SDK inputs are from
[ESP-IDF v4.4.7, commit 38eeba213aa695aabfd6d89aa9f5078dbe5a94c3](https://github.com/espressif/esp-idf/tree/38eeba213aa695aabfd6d89aa9f5078dbe5a94c3).
Reference mapping source is
[Espressif QEMU commit 40edccac415693c5130f91c01d84176ae6008566](https://github.com/espressif/qemu/blob/40edccac415693c5130f91c01d84176ae6008566/hw/xtensa/esp32.c),
the `esp-develop-9.2.2-20260417` release. Headers/images/source are external test
inputs, never bundled simulator runtime code. The SDK header is used unchanged
in a temporary compilation directory.

| External input | SHA-256 |
| --- | --- |
| PRO ROM binary | `9115bbcd03da9f54d3baca5ad94d588a43da54ed332347137ba3dc9010df258b` |
| APP ROM binary | `6d3240e7cdb1ec9c1d683515e0a8578e9fb8ece3e7736cb9a996512c656e8442` |
| SDK `soc.h` | `7a7148c288d7c289c961810e5617fa7b14d105b28349a45f772150b1e0845dd3` |
| SDK `esp32.rom.ld` | `0160cb9e14e83de87ad7363bd4977a9dd353b22e868a9bd4474e167ee86e58ec` |
| SDK `rom/miniz.h` | `3d43ce51f596fbb8226a55c0c7f63693606c953a45f3c92ac7f33d4147ccf74d` |
| QEMU `hw/xtensa/esp32.c` | `dcbdb0c67e2398e73e0d17b2f51d0f300161829b7bffd1c34b3114b168846d3f` |

## Native ROM boot boundary

Add **`--mask-rom-profile`** to the existing native diagnostic with explicit
synthetic eFuse/RTC/GPIO/UART-bank fixtures. The external release image has
455,722 bytes. The diagnostic explicitly pads its absent tail to 448 KiB with
zeros to match QEMU's empty-region fixture policy; this is not measured silicon
content. The API itself requires every supplied byte and performs no padding.
APP is not enabled by this PRO-only diagnostic.

The ROM now retires **6,796 instructions / 6,797 attempts**, executing the
formerly blocked entry at 0x40065c18. It next fails a **TIMG0 RTCCALICFG1 read at
0x3ff5f06c**, PC **0x40008591**, opcode **0x2222**. The result is still blocked:
no calibration result, clock frequency or ready event is fabricated to pass it.
[Hash-recorded evidence](../../tests/vendor/xtensa_integer/evidence/boot-with-mask-rom-2026-11-05.json)
retains `boot_verified: false`. Without the new flag, the old 6,790-instruction
mapping failure remains reproducible. Full native ROM/SDK boot and calibrated
instruction timing remain open.
