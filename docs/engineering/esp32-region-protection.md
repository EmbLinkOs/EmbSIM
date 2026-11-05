# Original ESP32 instruction and data region protection

**Implemented: the opt-in fixed-region protection profile, its ten TLB maintenance
instructions, synchronized access attributes and fetch/load/store protection.
Page-table translation, a complete memory subsystem, MEMCTL/loop-buffer behavior
and native SDK boot remain unfinished.**

## Real configuration and operations

The pinned original-ESP32 `core-isa.h` selects `HAVE_MIMIC_CACHEATTR=1`, no region
translation or page-table MMU, one hardware ring and zero Xtensa instruction/data
cache bytes. This is distinct from the SoC's external-flash MMU/cache behind its
local instruction/data ports. Region protection must not be described as that
complete SoC cache or as a page-table MMU.

Xtensa ISA RI-2021.8 section 5.3 defines separate instruction/data tables, each
containing eight hardwired identity mappings of 512 MiB. Upper address bits 31:29
select the entry. All attributes reset to 2, bypass. `esk_xtensa_region_configure`
enables this profile explicitly; enabled CPU reset restores both tables to 2.
Legacy ABI fixtures remain unchanged until opt-in. One hardware ring means
PS.RING does not create lower privilege: its bits read/write as zero in PS/EPS
access and return, and privileged operations remain available. PS.UM still
selects the existing user exception path.

| Instruction | Observable result |
|---|---|
| WITLB / WDTLB | Write the selected entry's low four attribute bits |
| RITLB0 / RDTLB0 | Return zero; VPN is already the fixed index |
| RITLB1 / RDTLB1 | Return the region base OR its attribute |
| PITLB / PDTLB | Return the region base OR 1; every region always hits |
| IITLB / IDTLB | Leave mappings intact; hardwired entries cannot be removed |

All operand registers, including aliases between source/destination, follow
normal read-before-write and window-reference checks. Address operand low bits
are ignored. Write-data bits 31:12 are ignored; unused bits 11:4 must be zero,
otherwise the operation fails unchanged rather than inventing undefined effects.
Unused read-result bits are deterministically zero.

WxTLB readback changes on retirement. ISYNC commits the instruction access table;
DSYNC commits the data access table. Until the required barrier, the old access
table is the declared deterministic fixture policy. The architecture leaves that
interval undefined; software must not rely on this chosen old-table behavior.
MEMW/RSYNC/ESYNC do not substitute for these TLB barriers. There is no physical
pipeline, prefetch-latency or synchronization-delay qualification.

## Permissions and bus effects

The profile applies the ISA attribute rights before routing to local memory or
an external bus:

| Attribute | Read | Write | Execute |
|---|---|---|---|
| 0, 14 | yes | yes | no |
| 1, 2, 4, 5, 6 | yes | yes | yes |
| 3 | no | no | yes |
| 7..13, 15 | no | no | no |

A denied fetch raises cause 20; a denied load, 28; a denied store, 29. EXCVADDR
records the denied address and the configured kernel/user/double path saves the
faulting instruction address. The rejected data access does not call the bus.
Fetch permission is checked before each fetched byte; an instruction crossing a
region boundary can fetch preceding permitted bytes before a later denial.
Without exception configuration, rejection returns a protection fault unchanged.
Alignment and unsupported-instruction limitations remain those of the existing
engine; this is not qualification of every CPU memory fault or priority.

Protection covers the implemented byte/halfword/word loads/stores, density forms,
signed load, literal load, acquire/release, window-exception accesses and S32C1I.
The pure address descriptor is shared with ordinary execution, avoiding a second
set of normal load/store address calculations. S32C1I tests store capability only
and can raise cause 29, never cause 28, even though it reads during exchange.
Protection runs before atomic type classification or compare/exchange.

Local-port windows come from the pinned core configuration, not invented backing
memory. For local accesses the ISA consults only rights. There is no Xtensa cache
in this configuration. Cache-isolate attribute 14 data operations outside local
ports fail explicitly because an external isolate implementation is unqualified.
Device/SoC cache state and unavailable memory remain the provider's responsibility;
setting region rights does not allocate RAM or make unknown MMIO work.

`window_execute` consumes supplied instruction bytes and checks their data
accesses. `window_step` additionally performs actual instruction fetch protection.
CPU interrupts are still polled before fetch; exception transitions do not retire
the faulting instruction. No instructions are silently treated as NOPs except the
architecturally non-removing invalidate behavior for these fixed tables.

## Evidence

- `test_xtensa_regions`: **17,068 assertions** across both tables, every region
  and all attributes; maintenance readback, synchronization/reset, ignored upper
  and rejected low bits, one-ring PS behavior, the permission matrix, zero data
  bus effects on denial, atomic store priority and all supported memory forms
  through kernel/user/double exceptions.
- `run_regions.py`: **266,240 independent GNU-assembled instruction/register/
  alias comparisons**. It hash-checks actual original-ESP32 core configuration.
- Actual ESP32 GCC compiles original C that reads/writes/probes/invalidates data
  entries, executes DSYNC and restores permissions before storing results.
  **32 calls, 480 retired instructions**, all 16 attributes on both cores, pass
  through the native bus without Python memory callbacks. Host ABI function
  setup is explicit; this is not SDK boot or a physical timing comparison.
- Existing flat-memory, windows, exceptions, atomic, timer, TLS, reset, mapped
  flash, SPI programming and eFuse compiler/vendor fixtures also rerun.

The subsequent [MEMCTL profile](esp32-memctl-loop-buffer.md) now reaches RTC
reset-state read after 6,692 instructions. The historical region-only result
below retains its original source scope.

The external real-ROM probe at this milestone completes both instruction/data maintenance loops
and ISYNC/DSYNC, retires **545 instructions**, then stops at unsupported
**RSR.MEMCTL**, opcode 0x036120, PC 0x400004f2. The
[hash-recorded result](../../tests/vendor/xtensa_integer/evidence/boot-with-regions-2026-11-05.json)
uses the same external ROM/SDK image, explicit synthetic eFuse image, PRO PRID and
artificial completion schedule as the [boot investigation](esp32-boot-verification.md).
Both readback and synchronized access tables are `[15,1,1,2,2,2,2,2]`,
recorded separately for instruction/data. No PC shortcut or fallback MMIO is introduced. This does not verify complete
ROM-to-application boot; MEMCTL and subsequent requirements remain open.

## References and reproduction

Xtensa ISA Summary RI-2021.8, section 5.3, table 95 and instruction descriptions;
external PDF SHA-256
`474c4aaf2d429ff79c48127e738c4110cecd8072133169ad6d00a050edf244b7`.
Original ESP32 `core-isa.h` SHA-256
`60d20f76c7730b24041b9f55b16e0062f30bf85c7661c7ed215f050fedcdb344`.
No vendor implementation or ROM is included in runtime source.

```sh
python3 tests/vendor/xtensa_integer/run_regions.py \
  --assembler /path/to/xtensa-esp32-elf-as \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld --nm /path/to/xtensa-esp32-elf-nm \
  --core-isa /path/to/xtensa/config/core-isa.h
```
