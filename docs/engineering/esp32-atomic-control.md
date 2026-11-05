# ESP32 CPU atomic-operation control

**Implemented: opt-in six-bit ATOMCTL register access, local-data-RAM and RCW
conditional stores, and configured LoadStoreError exceptions. Internal cache
operations, complete memory attributes/MMU and native SDK boot remain absent.**

## Contract and real operation

Xtensa ISA RI-2021.8 section 4.3.13 and table 190 define conditional-store
ATOMCTL, privileged special register 99. It resets to 0x28 and supports RSR,
WSR and XSR. Original ESP32 core configuration enables S32C1I. This profile
implements its six low bits; Exclusive Access's bit 8 is not implemented.
Writes with upper/reserved bits set fail unchanged because their effects are
undefined for this profile. All six low bits read back, including field values
whose conditional-store operation is reserved or unsupported.

| Field | Bits | Value 0 | Value 1 | Value 2 | Value 3 |
|---|---|---|---|---|---|
| BY, bypass | 1:0 | LoadStoreError | bus RCW | reserved | reserved |
| WT, writethrough | 3:2 | LoadStoreError | bus RCW | internal cache, unqualified | reserved |
| WB, writeback | 5:4 | LoadStoreError | bus RCW | internal cache, unqualified | reserved |

Local DataRAM ignores these fields and performs the atomic operation directly.
Register 0x15 selects RCW for all three external memory types. Reset 0x28 selects
internal operation for WT/WB and an exception for BY; it does not grant arbitrary
atomic access to all memory. Unimplemented cache operations and reserved
operations fail explicitly, rather than being implemented as a bus transaction.

`esk_xtensa_atomctl_configure` enables the profile with reset value 0x28.
Supported original-ESP32 CPU reset also enables/resets it. Existing ABI-only
fixtures remain disabled until explicit opt-in; they retain their previously
qualified direct conditional-exchange contract. Register state is per CPU.
Privileged access raises cause 8 through the configured exception path. Window
checks still precede any conditional-store bus activity. No new clock-cycle,
cache, pipeline or synchronization-delay claim is made.

## Memory provider and observable access

`esk_xtensa_memory.atomic_type` classifies the complete aligned word without
performing a load/store. An enabled profile requires this callback; missing,
unknown or invalid classification fails unchanged. The provider supplies LOCAL,
BYPASS, WRITETHROUGH or WRITEBACK; the CPU never infers cacheability from an
unimplemented MMU or supplies a permissive fallback.

LOCAL and RCW use the provider's indivisible compare/exchange callback, returning
the original word on match or mismatch. There is no ordinary read/write fallback.
Mode 0 raises LoadStoreError cause 3, recording EXCVADDR and the faulting PC,
without invoking compare/exchange or advancing the instruction. Kernel/user/
double dispatch follows the existing exception profile. Without that profile it
returns the explicit atomic fault. Internal-cache and reserved modes fail without
changing CPU or memory state. Existing alignment-fault dispatch limitations still
apply; this is not qualification of every CPU memory exception or priority.

The native ESP32 fabric classifies its qualified writable SRAM1/SRAM2 data windows
as LOCAL. They lie inside DataRAM0 in the pinned original-ESP32 core configuration
(0x3ff80000, size 524288). Instruction aliases, RTC memories, MMIO, external
flash/PSRAM and holes remain unsupported atomic targets; their attributes or
atomic interconnect support are not guessed. The machine forwards classification
through the same core-specific bus view used for accesses.

## Evidence

- `test_xtensa_atomic`: **23,665 assertions**, including all six-bit register
  values and all operand registers/forms, reserved-upper-bit rejection, reset,
  privilege, local bypass, all BY/WT/WB field combinations, matched/mismatched
  RCW, cause-3 entry and unchanged unsupported/missing-classification failures.
- Fabric tests cover both cores' SRAM endpoints and explicitly rejected targets.
- `run_atomctl.py`: **3,072 independent GNU-assembled register comparisons**.
  Actual ESP32 GCC then compiles a C function that writes/reads ATOMCTL, performs
  S32C1I on native shared SRAM, checks the original/current value and restores
  the register. All 64 control values on both cores pass: **128 calls and 2,816
  retired instructions**. This checks that LOCAL ignores external type fields,
  including zero/reserved modes. The bus is native, with no Python memory
  callbacks. Host ABI function setup is explicit; this is not application boot.

The subsequent [region profile](esp32-region-protection.md) advances the probe
to RSR.MEMCTL after 545 instructions. The historical atomic-only result below
retains its earlier source scope.

The real-ROM probe at this milestone retires **451 instructions**, including WSR.ATOMCTL=0x15,
then stops at unsupported **WITLB**, opcode 0x506340, PC 0x400004c9. The
[recorded probe](../../tests/vendor/xtensa_integer/evidence/boot-with-atomctl-2026-11-05.json)
uses the same external ROM, actual SDK image, synthetic None-coded eFuse image,
PRO PRID and explicit read-completion schedule documented in the
[boot investigation](esp32-boot-verification.md). No PC shortcut or unknown-MMIO
fallback is introduced. WITLB/memory-attribute behavior is the next observed
blocker, not the only remaining requirement for complete boot.

## References and reproduction

External Xtensa ISA summary RI-2021.8, sections 4.3.13 and table 190; PDF SHA-256
`474c4aaf2d429ff79c48127e738c4110cecd8072133169ad6d00a050edf244b7`.
No manual text or vendor implementation is included in runtime source.
ESP32 `core-isa.h` toolchain pin is recorded in the
[target investigation](esp32-dual-core.md).

```sh
python3 tests/vendor/xtensa_integer/run_atomctl.py \
  --assembler /path/to/xtensa-esp32-elf-as \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld --nm /path/to/xtensa-esp32-elf-nm
```
