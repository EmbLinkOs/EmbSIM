# ESP32 explicit eFuse image and read/reload profile

**Implemented: a None-coded eFuse read/reload controller profile with explicit
image input, read protection and completion routing. No OTP programming, complete
eFuse/security integration or native SDK/board boot is claimed.**

## Architectural contract

ESP32 TRM v5.9 chapter 5 defines four eFuse blocks, software read latches, write
staging registers, coding/read protection and a read/program command handshake.
This implementation takes all 31 uncoded mapped words explicitly: BLK0 seven,
then BLK1/2/3 eight each. No factory MAC, keys, calibration, security, revision or
voltage values are inferred. Without attachment the MMIO range remains
unsupported. A coding field of 0 or 3 selects None; 3/4 and Repeat are rejected
unchanged. Image attachment copies its inputs and initializes the profile.
Detach is host fixture setup, not a physical eFuse erase.

RDATA is read-only according to the TRM register table. Some SDK field comments
call individual RDATA fields R/W; this does not override the register-level
software-read contract. RD_DIS bits 16..18 hide BLK1..3. Bit 19 hides block-0
FLASH_CRYPT_CONFIG, CODING_SCHEME and KEY_STATUS, preserving other fields.
The model keeps the supplied underlying image separate from its protected view.
These inputs are not assertions about a particular unit's unique fuse contents.

WDATA stores staging words but cannot program the image. The supported handshake
is CONF=0x5aa5 followed by CMD=1: the controller reloads the protected view,
sets CMD=1 until `esk_esp32_efuse_read_complete` supplies an external controller
completion event. That event reloads the protected latches, clears CMD and latches
read-complete RAW bit 0. Polling does not advance the operation. Completion while
idle/disabled fails unchanged; reissuing a command or changing read configuration,
clock/DAC or staging while pending is explicitly unsupported. STATUS state-machine
encoding while active remains unsupported. A zero command write does not cancel.
Physical read/program duration and bus clocks are unqualified; callers must supply
the external completion event rather than assuming instruction retirement is time.
RAW is independent
of ENA; ST and the level IRQ are RAW AND ENA. CLR is write-one-to-clear; reading
that write-only register is unsupported. Source 44 is owned by the machine while
the profile is attached, with synchronization after instruction commit or
immediately after host writes through the machine bus.

CLK and DAC divider fields hold configuration, without waveform qualification.
DAC pad-clock mode is rejected. CONF supports only inactive/reset and documented
read mode; FORCE_NO_WR_RD_DIS programming interactions are unqualified. WRITE
opcode, PGM_CMD, combined commands and unsupported modes fail unchanged. STATUS
and DEC_STATUS expose the qualified idle/None-coded zero state; no fabricated
ECC/burn success is returned. Only aligned 32-bit MMIO data accesses qualify;
fetch, CAS, narrow access and unknown registers fail. Initial controller fields
come from the pinned register defaults, not assumed chip identity.

CPU-only APP reset preserves the shared controller image/status/staging within
the existing bounded reset profile. Global reset domains, OTP/write protection,
3/4/repeat encoding, analog programming/supply failure, and direct hardware
consumers of fuse fields (including security, calibration and CPU restrictions)
remain unqualified. The image/read profile does not constitute a full SoC model.

## Evidence

`test_esp32_efuse` passes 8,443 assertions across all software-read protection
combinations and both None encodings, all read/staging words, unchanged rejected
images/commands, immutable host input, completion/masking/clear, CPU-only reset,
source ownership and a fetched READ_CMD store followed by level-2 IRQ entry.

`run_efuse.py` hash-pins ESP-IDF v4.4.7 register, low-level and SoC headers. It
passes 3,968 independent register/protection comparisons. Actual ESP32 compiled
C then executes seven unchanged selected SDK helper bodies through native MMIO:
128 calls, 6,208 retired instructions, on both serialized cores. The fixture supplies completion opportunities every
17 instruction attempts, an artificial external event schedule. Firmware calls
are serialized across the two cores because the controller is shared. Register
addresses and bit fields come from the vendor headers. A minimal MMIO macro
fixture supplies bus access, not a vendor controller implementation. Temporary
header/helper extraction retains Espressif's Apache-2.0 notice. Synthetic fuse
images contain no owner's keys, MAC or calibration. This is selected helper
acceptance, not the full burn stack or SDK boot.

## Real ROM probe and next blocker

The previous no-image probe still stops at eFuse after seven retired instructions.
With the explicitly selected
[unprogrammed None-coded test image](../../tests/vendor/xtensa_integer/fixtures/efuse-unprogrammed-none.json),
the same QEMU-distributed PRO ROM and actual ESP-IDF flash image now reach:

```text
retired instructions 440
failure PC           0x40000488
failure opcode       0x136330
fault                unsupported WSR.ATOMCTL (special register 99)
```

The [recorded probe](../../tests/vendor/xtensa_integer/evidence/boot-with-efuse-completion-2026-11-05.json)
is a **blocked boot**, not application success. The test fuse image is deliberately
unprogrammed; it is neither a dump of the owner's v3.1 unit nor a claim about
QEMU's full factory image. The ROM itself consumes the controller data. There
are no unimplemented-MMIO fallback values or host PC shortcuts. PRID/reset/DROM
tail fixture limitations from the [boot investigation](esp32-boot-verification.md)
still apply. The fixture supplies completion opportunities every 64 instruction
attempts and PRO PRID=0xcdcd from the pinned SDK documentation. Three reads
complete before the unsupported instruction. The schedule is not physical timing. Repeating with `--efuse-completion-period
128` reaches the same instruction after 503 retirements; period 1 completes
before the ROM sample and correctly reproduces its reset request after 30.

The previous synchronous implementation's 30-instruction RTC write was a **ROM
software-reset request**, not evidence of passing its eFuse handshake. Disassembly
shows the ROM expects to observe READ_CMD active before waiting for completion;
instant completion selected the failure/reset path. The new model preserves that
observable active state without inventing a read duration. Historical evidence
retains the old result; the event-driven probe supersedes it. RTC/power/reset,
ATOMCTL/cache/atomic permissions and subsequent boot requirements remain unfinished.

## References and reproduction

ESP-IDF v4.4.7 external inputs:

- `components/soc/esp32/include/soc/efuse_reg.h`, SHA-256
  `cfe37e8e692a22e41885f606bee7c4c12e0308abbdeb5f13f6cc92471453ff03`.
- `components/hal/esp32/include/hal/efuse_ll.h`, SHA-256
  `ce3dfed567383582c6ced10a9edc08de296c5b5d541ee171efdd07b0014e3f30`.
- `components/xtensa/include/xtensa/xtensa_context.h` documents PRO PRID=0xcdcd
  and APP=0xabab; SHA-256
  `a833937d99b2c5147e2836203643590ad2c717b2b406be31388a5a9a3c5c4aca`.
- `soc.h` and actual ESP32 toolchain pins are in the target investigation.

```sh
python3 tests/vendor/xtensa_integer/run_efuse.py \
  --efuse-reg /path/to/efuse_reg.h --efuse-ll /path/to/efuse_ll.h \
  --soc-header /path/to/soc.h \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld --nm /path/to/xtensa-esp32-elf-nm
python3 tests/vendor/xtensa_integer/probe_rom_boot.py \
  --rom /path/to/esp32-v3-rom.bin --flash /path/to/idf-hello-flash.bin \
  --efuse tests/vendor/xtensa_integer/fixtures/efuse-unprogrammed-none.json
```

The boot diagnostic returns 1 for blocked/inconclusive execution. It is not a
passing conformance test. Runtime code and fixtures do not embed vendor ROM.
