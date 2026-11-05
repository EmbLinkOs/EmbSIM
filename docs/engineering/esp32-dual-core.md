# Original ESP32 dual-core target: identity and implementation gates

**Status: firmware inspection, experimental Xtensa execution, shared internal RAM, interrupt routing, APP controls and bounded reset/vector and read-only mapped-flash profiles implemented; no runnable board target is added.** Requested by
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
It is not wired into a CPU constructor or board catalog; the app cannot boot
firmware on this target yet. This integer helper alone does not rotate windows;
the higher-level engine below owns the physical register file.

`test_xtensa_integer` checks an assembled sequence, arithmetic extremes, aliases,
input errors and unchanged state on unsupported execution (111 assertions).
`tests/vendor/xtensa_integer/run.py` independently assembles 9,728 instruction
cases with GNU binutils and compares every output register against Python
integer arithmetic. It varies register aliases, immediates and 32-bit extremes
with a fixed seed. The expanded runner uses the actual Espressif ESP32 tools
for the additional
ISA options. Earlier LX106 binutils checks covered only the shared base subset;
LX106 is not a substitute for the newly tested LX6 options or an ESP32 system
qualification suite. No assembler code or decoder tables are copied.

Reproduce on Linux with a host C compiler and separate Xtensa binutils:

```sh
python tests/vendor/xtensa_integer/run.py \
  --assembler /path/to/xtensa-esp32-elf-as \
  --objcopy /path/to/xtensa-esp32-elf-objcopy
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
An optional external-assembler/linker referee checks 4,800 execution cases and
1,152 rejected accesses against independent Python PC/register/bus expectations.
Linker relaxation is disabled to preserve the fixture's explicit instruction
addresses. The earlier 9,728 integer cases continue to pass.

```sh
python tests/vendor/xtensa_integer/run_execute.py \
  --assembler /path/to/xtensa-esp32-elf-as \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld
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
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld \
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
  exception dispatch is opt-in as described below. PS fields unrelated to these mechanisms
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

## Implemented: SAR and selected XEA2 system behavior

The execution state now includes SAR, EXCCAUSE, EXCVADDR and DEPC. SAR is six
bits wide on WSR/XSR; SSR uses only five source bits, SSL writes 32 minus the
five-bit shift, SSA8L/SSA8B select byte shifts, and SSAI supplies an immediate.
SLL/SRL/SRA/SRC handle the defined 0..32 SAR range without host-language
shift overflow. SAR values above 32 remain readable/writable but executing those
shifts returns unsupported because the ISA leaves the result undefined. XSR.SAR
exchanges the register atomically within this single-instruction functional
model; other XSR special-register forms remain unsupported.

RSR/WSR support the new fields. SAR and loop registers are accessible without
privileged-register checks; implemented privileged register operations and RSIL
honor the configured PS.RING/EXCM state. RSIL returns old PS and changes
INTLEVEL. The optional CPU input profile below adds functional interrupt masking
and delivery; it does not establish cycle-exact interrupt latency. MEMW/ISYNC/ESYNC are serialized by the already synchronous
bus and register execution; there is no prefetch/cache/pipeline or asynchronous
external-effect qualification. EXTW is still unsupported.

An explicit opt-in XEA2-with-DEPC profile configures kernel, user and double
exception vectors. It implements:

- SYSCALL, known ILL/ILL.N opcodes, and privilege faults for the implemented
  privileged instructions. Unimplemented valid instructions (tested with ADD.S)
  remain unsupported; they are never silently translated to illegal opcodes.
- Kernel versus user selection by PS.UM on initial entry, EPC1 capture and EXCM
  entry. A nested exception captures DEPC, preserves EPC1 and selects the double
  vector. EXCCAUSE updates; EXCVADDR changes only on explicitly address-bearing
  injection, not on an unrelated syscall.
- RFE clearing EXCM and returning via EPC1; RFDE returning via DEPC while
  retaining EXCM. Saved PCs name the faulting instruction; a syscall handler
  explicitly advances its saved PC if it wants to resume after the instruction.
- An explicit exception-injection API for future configured bus/SoC models.
  The caller supplies the architecturally established cause/address semantics.

The profile is disabled by initialization until vectors are configured. Those
addresses are test/host inputs, not an inferred ESP32 VECBASE or ROM layout.
Generic memory/fetch failures remain diagnostics because the current callback
cannot distinguish protection, translation and bus error causes accurately.
Automatic MMU/bus fault dispatch, debug exceptions and complete
privilege/system behavior remain unimplemented. CPU interrupts are supported only
with the separate explicit profile below. No CPU constructor or board
catalog target is added.

`test_xtensa_system` passes 785 assertions: shift boundaries, masking/exchange,
window checks on real operands, syscall kernel/user/double selection, saved PC
advance and nested returns, EXCVADDR retention, explicit illegal instructions,
unsupported valid opcodes, privilege faults and synchronous fence acceptance.
A fetched program executes SYSCALL, runs a memory-resident handler that reads
and advances EPC1, returns via RFE, and resumes the next instruction with the
expected result. The existing 224 deep windowed-ABI runs and all six vendor
spill/fill paths still pass with the expanded state.

`run_system_c.py` uses the actual ESP32 GCC 8.4.0 toolchain to compile unchanged
C left/right/arithmetic shifts, rotation and funnel functions. It passes 7,040
runs (37,818 fetched instructions), including unsigned extremes, signed negative
inputs, all masked shift counts and 0/32 funnel boundaries. It also checks caller
stack, register-window and return state. These are functional CPU tests, not
hardware timing, interrupt delivery or SoC boot qualification.

```sh
python tests/vendor/xtensa_integer/run_system_c.py \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld \
  --nm /path/to/xtensa-esp32-elf-nm
```

## Implemented: PS field masking and exchange

RSR/WSR/XSR.PS expose only implemented fields: INTLEVEL/EXCM/UM/RING/OWB in
bits 11..0 and CALLINC/WOE in bits 18..16 (mask `00070FFF`), as defined by the
Xtensa ISA PS register table. Reserved bits read zero and are discarded on
writes. XSR returns the old PS in its operand register and installs that
register's prior value, with the same field mask. RFI copies only implemented
PS bits from EPS without rewriting EPS storage.

Existing register-window and privilege checks precede the exchange. Mask changes
are one state transition; a pending interrupt forbidden by both old and new
masks does not enter through an invented intermediate state. Subsequent eligible
polls use the new mask. RING changes are qualified only from EXCM in these
fixtures; full MMU/privilege behavior, synchronization/pipeline latency and a
silicon reset sequence remain outside this subset.

`test_xtensa_ps` passes 1,304 assertions across all operand registers, reserved
bits, RFI levels 2..7, privilege rejection, window-overflow priority and atomic
mask changes. `run_ps.py` passes 3,456 independently GNU-assembled PS/RFI cases
against a Python register/field oracle. Actual compiled mailbox/control C
handlers continue to pass with the masked PS transitions.

```sh
python tests/vendor/xtensa_integer/run_ps.py \
  --assembler /path/to/xtensa-esp32-elf-as \
  --objcopy /path/to/xtensa-esp32-elf-objcopy
```

## Implemented: ESP32 CPU interrupts and explicit compare-clock ticks

`interrupts.c` implements the configured ESP32 LX6 CPU input controller. The
32 input levels/types and EXCMLEVEL=3 are checked against Espressif's toolchain
`xtensa/config/core-isa.h`, not inferred from the C3 or an arbitrary priority map.
Expected configuration SHA-256:
`60d20f76c7730b24041b9f55b16e0062f30bf85c7661c7ed215f050fedcdb344`.
This external test input comes from the already hash-verified ESP32 toolchain.
Its permissive vendor configuration notice is not a dependency of the app;
no vendor controller implementation is copied or linked.

The interrupt profile requires configured general exception vectors and separate
caller-supplied level 2..7 vectors. It is disabled by initialization. Configuration
resets controller/context values for deterministic test setup, not a claim about
silicon reset values. Disabling the general exception profile also disables the
interrupt profile and releases its simulated WAITI suspension.

- Level inputs remain pending while asserted; external edges latch rising edges
  until INTCLEAR. Software inputs 7/29 are set only through INTSET, and other
  INTSET bits have no effect. Internal timer/profiling inputs can be driven by
  the device input API; that does not make them physical GPIOs.
- Maskable delivery uses INTENABLE and the higher of PS.INTLEVEL and the EXCM
  floor of 3. The highest eligible level wins. A vector represents a level,
  not an invented winning input number; software can inspect all pending bits.
- The NMI input 14 latches an edge, bypasses masking and consumes that edge on
  acceptance. A held input does not create repeated edges. Sampling/glitch timing
  and precise silicon NMI status-read timing remain unqualified.
- Level 1 enters the configured general exception path with cause 4. Higher
  levels save EPCn/EPSn, set INTLEVEL/EXCM and enter the configured vector.
  RFI 2..7 restores the saved PC and PS. No general registers are automatically
  pushed to a host shadow stack; handler code saves what it needs.
- INTERRUPT/INTSET, INTCLEAR, INTENABLE, EPC2..7, EPS2..7 and EXCSAVE1..7 are
  available through the implemented special-register instructions. INTCLEAR has
  no invented read value. Delivery does not automatically acknowledge a maskable
  source, so held levels/rejected acknowledgement can immediately retrigger.
- WAITI updates the mask and saves the next PC before suspension. Step returns
  event 18 without fetching while no eligible input exists. Pending-before-WAITI
  races, level masking and post-WAITI return addresses are checked. This does
  not model sleep current, regulators, radio power management or clock gating.

CCOUNT/CCOMPARE0..2 use **explicit processor-clock advances** through
`esk_xtensa_clock_advance`; instruction retirement does not fabricate elapsed
cycles. Compare hits latch CPU inputs 6/15/16 and remain pending until the
corresponding CCOMPARE write. INTCLEAR and CCOUNT writes do not acknowledge them.
The counter wraps at 32 bits, with zero/multiple-wrap/64-bit tick advances
supported. `esk_xtensa_clock_deadline` returns the nearest unlatched compare,
including a full wrap when compare equals the current count.

Clock advances continue during WAITI, allowing a compare event to wake the CPU.
Bulk advances capture hits but do not execute handlers midway through the advance;
a host scheduler must split at deadlines when interrupt service timing matters.
Software reads/writes are applied at the functional boundary; their precise
silicon clock/pipeline phase is not qualified. CCOUNT/CCOMPARE are architecturally
undefined at reset, so deterministic zero initialization is only test setup.
No instruction timing, clock-frequency integration or cycle-accurate latency is
claimed until the real core/runtime scheduler is connected.

The native interrupt test passes 11,571 assertions: all 32 inputs across 16 mask
levels, EXCM on/off and enable on/off, nesting, reassertion, edges/software/NMI,
WAITI races, register/privilege behavior and a fetched handler. That handler saves
A2 in EXCSAVE3, acknowledges software input 29, restores A2 and returns via RFI;
the interrupted program resumes with its expected result. The timer test passes
81 assertions covering deadlines, sticky requests, compare acknowledgement,
wraps/huge advances, CCOUNT register access and timed WAITI wake-up.

Optional referees provide 2,048 comparisons against the vendor's input level/type
configuration, and 8,192 clock/deadline comparisons against 263,719 individual
oracle ticks with vendor timer-input mapping. The earlier 224 windowed-ABI runs
and 7,040 compiled bit-manipulation runs still pass with the expanded state.

```sh
python tests/vendor/xtensa_integer/run_interrupts.py \
  --core-isa /path/to/xtensa-esp32-elf/include/xtensa/config/core-isa.h
python tests/vendor/xtensa_integer/run_timer_clock.py \
  --core-isa /path/to/xtensa-esp32-elf/include/xtensa/config/core-isa.h
```

These CPU input tests alone do not establish SoC routing or cross-core
interrupt generation. The separate matrix implementation below covers those
functional paths. Full ISR/RTOS context switching, debug/NMI cycle timing and
board power behavior remain separate qualification gates. No runnable board
target is registered.

## Implemented: extended integer operations, bit branches and conditional store

The integer kernel now adds MOVEQZ/MOVNEZ/MOVLTZ/MOVGEZ, signed/unsigned
MIN/MAX, MULUH/MULSH, QUOU/QUOS/REMU/REMS, SEXT and NSA/NSAU. Conditional
moves retain the old destination when not taken; register aliases are supported.
Signed division truncates toward zero and remainder follows the numerator's
sign. INT_MIN/-1 produces the architectural wrapped quotient (and zero remainder)
without invoking host C overflow; compiled C tests exclude that undefined C
expression and the native instruction fixture tests it directly.

Divide-by-zero preserves the destination and faults before instruction retirement.
The configured XEA2 profile dispatches cause 6; without it, execution returns a
specific divide diagnostic. Register-window exceptions retain their higher
priority. Unsupported valid FPU instructions (ADD.S) remain diagnostics rather
than being turned into illegal instructions now that QUOU is implemented.

The little-endian branch decoder also adds BANY/BNONE/BALL/BNALL, BBC/BBS
and BBCI/BBSI. Immediate bit numbers are not register references during window
checks; register bit indices use their low five bits. Taken-flow tracking retains
correct loop-exit behavior. Other endianness/configurations are not qualified.

SCOMPARE1 supports RSR/WSR/XSR and retains per-execution-state values. S32C1I
computes the aligned word address, supplies expected/desired values to a single
bus compare-exchange callback and returns the original memory word in its
register operand whether or not the comparison matches. Aliasing the result and
base register uses the old base. Missing/unsupported callbacks fail explicitly;
there is no read/write fallback that pretends to be atomic. Callback errors and
alignment failures leave CPU state unchanged, while window overflow takes
priority before touching memory.

Callbacks must not re-enter or mutate the currently executing CPU state. Input
and clock changes for that CPU are queued for instruction boundaries; these
execution APIs do not provide concurrent host-thread state access. A future SoC
bus/event bridge must honor that ownership contract.

The callback must exclude conflicting writes from **all** bus agents. It is
responsible for allowed memory regions and real indivisibility; serialized test
callbacks do not establish host-thread safety. This profile uses strong compare-
exchange and does not inject the implementation-dependent spurious-failure
alternative. Caches, memory types/protection, hardware arbitration, pipeline
ordering, interrupt/RTOS preservation of SCOMPARE1 and full SMP remain unqualified.
MEMW in the current synchronous model does not establish a real cache-coherence
or multicore ordering model. No shared SoC RAM bus or dual-core scheduler is
registered by these tests.

Native acceptance adds 128 arithmetic/bit-branch assertions and 67 conditional-
store assertions: signed limits, division faults/window priority, bit endpoints,
success/failure comparisons, base/result alias, offset wrap, unsupported regions,
missing callbacks, fault retention and exactly one callback without normal bus
read/write fallback. Existing system/window/interrupt/timer checks still pass.

The expanded assembler referees now pass 9,728 integer cases and 4,800 flow/bit-
branch cases, plus the existing 1,152 rejected accesses. Actual ESP32 GCC compiles
exchange, compare-and-swap, increment and signed/unsigned division/remainder C
routines for 832 successful executions (5,248 fetched instructions). Sixty-four
pairs of execution states share test RAM and alternate instruction steps so both
load the same value before conditional store; one must retry. Across the suite,
384 CAS callbacks include 128 comparison mismatches and no ordinary stores.
Unsigned wrap cases are included. This proves functional execution and the
callback contract under that interleaving, not a completed dual-core SoC.

```sh
python tests/vendor/xtensa_integer/run_atomic_c.py \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld \
  --nm /path/to/xtensa-esp32-elf-nm
```

## Implemented: synchronous acquire/release memory operations

L32AI and S32RI implement aligned 32-bit load-acquire and store-release with an
unsigned 8-bit word-scaled offset (0..1020 bytes). Address/source aliasing uses
the pre-instruction registers; window-reference checks precede bus access.
Misalignment or refused callbacks preserve architectural state and report the
existing load/store fault diagnostic rather than inventing an MMU exception.

The callback provider must complete reads/writes before returning and expose
completed effects to subsequent turns. This serialized functional contract
already supplies the required acquire/release ordering. It does not support
posted writes, stale private caches or asynchronous transactions; extending the
bus to those requires explicit completion/barrier semantics. No physical cache,
pipeline or timing fidelity follows from this implementation.

`test_xtensa_ordering` passes 30,732 assertions across every register pair,
minimum/maximum offsets, failure retention, alignment and window-overflow
priority. The independent actual ESP32 assembler runner now passes 4,800
execution cases and 1,152 refused accesses, including acquire/release.
The native machine runner below additionally executes actual compiler-generated
L32AI/S32RI in spinlocks protecting non-atomic read/modify/write critical sections.

## Implemented: internal RAM and two-context execution harness

`core/mcu/esp32/fabric.c` implements a bounded native memory provider from
ESP32 TRM v5.9 section 3.3.2 and ESP-IDF v4.4.7's
[SoC address definitions](https://github.com/espressif/esp-idf/blob/v4.4.7/components/soc/esp32/include/soc/soc.h).
It is not the complete SoC bus. RAM is initialized to zero for deterministic
fixtures, not as a claim about silicon power-on contents.

| Region | Address range (end exclusive) | Implemented access |
| --- | --- | --- |
| SRAM0, non-cache portion | `40080000–400A0000` | Instruction fetch and aligned data words |
| SRAM1 data | `3FFE0000–40000000` | Data bytes/halfwords/words and CAS |
| SRAM1 instruction | `400A0000–400C0000` | Fetch and aligned data words, reversed word ordering |
| SRAM2 | `3FFAE000–3FFE0000` | Data bytes/halfwords/words and CAS |
| RTC fast | `3FF80000–3FF82000`, `400C0000–400C2000` | CPU0 only; data view and instruction alias |
| RTC slow | `50000000–50002000` | Both cores, data and instruction fetch |
| ROM0 | `40000000–40060000` | Read-only fetch/aligned data words, only with supplied ROM |
| ROM1 | `3FF90000–3FFA0000` | Read-only data, only with supplied ROM |

SRAM1 instruction words reverse the data view's order without reversing byte
lanes: `3FFE0000` aliases `400BFFFC`, and `3FFFFFFC` aliases `400A0000`.
The instruction engine now uses an optional distinct fetch callback; existing
flat providers retain the read fallback. This lets narrow instruction fetches
work while rejecting narrow data accesses to instruction-only views.

A host-configured per-core boot-remap view aliases the first 32 KiB of ROM0 to
SRAM1 instruction addresses `400B0000–400B8000`, disabling that original
instruction alias for the selected core while preserving its data view and the
other core's mapping. This is not a DPORT register implementation. Cache SRAM,
unknown peripheral addresses and unmapped holes fail explicitly. Flash/MMU
windows require the separate opt-in mapped-flash profile below.
ROM must be provided externally at its exact region size; none is fabricated or
bundled. Host RAM loading preflights the entire range before writing. A failed
ROM allocation preserves the installed ROM.

`core/mcu/esp32/machine.c` owns two separate window/exception/interrupt/clock
profiles sharing this provider. Explicit debug controls configure an entry,
pause/resume a core and step one instruction boundary at a time. A round executes
CPU0 then CPU1 and stops at the first failure; earlier successful turns remain
committed. A failing core pauses at its fault. Paused cores report event 19;
WAITI retains event 18 and does not block the peer. Only retired instructions
increment each core's counter; exceptions and suspended turns do not. Clock
advancement remains explicit and private to each profile.

All calls have one serialized owner. Native SRAM1/2 CAS performs its read and
conditional write within one callback, so instruction-interleaved contexts
cannot intervene. This is not host-thread-safe memory, cache/coherence,
arbitration, an instruction-to-cycle model, DPORT reset/release, physical clock timing, retention,
DMA or a complete SMP machine. Reconfiguring one profile preserves shared RAM
and remap settings and is explicitly not a silicon reset sequence. No CPU
constructor, board picker entry, Espressif ROM execution or ESP-IDF boot is added.

Native fixtures pass 360,531 memory assertions and 86 machine assertions: every
SRAM1 word and byte lane, remap isolation, RTC
restrictions, rejected access widths/holes, all-or-nothing loading, allocation
failure, shared conditional stores, independent state, fault ordering and
one-core WAITI/timer wake while its peer continues. The separate compiled runner
uses no Python memory/CAS callbacks: actual ESP32 GCC output runs atomic counter
loops and acquire/CAS/release spinlocks protecting ordinary shared writes, checking final count, the sum of distinct
returned increments, unsigned wrap, caller/window/stack preservation, retirement
and exact replay equality. It passes 512 task executions and 128 schedule replays
(229,322 instructions). This is functional interleaving evidence, not full SMP
or real-hardware qualification.

```sh
python tests/vendor/xtensa_integer/run_machine_c.py \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld \
  --nm /path/to/xtensa-esp32-elf-nm
```

## Implemented: interrupt matrix and firmware-generated cross-core interrupts

`core/mcu/esp32/matrix.c` implements the register-routing contract from TRM v5.9
chapters 8 and 12. Each core has 69 numbered source slots; slots 41/42 are
reserved and rejected. GPIO slots 22/23 have separate per-core input levels;
other defined sources are shared. Peripheral generators are not implemented by
this controller: external source levels must be supplied explicitly.

| Register group | Address range (inclusive, 32-bit registers) | Behavior |
| --- | --- | --- |
| Software-generated sources 0..3 | `3FF000DC–3FF000E8` | Bit 0 asserts/clears shared sources 24..27 |
| PRO source status 0..2 | `3FF000EC–3FF000F4` | Raw source levels, independent of routing and masks |
| APP source status 0..2 | `3FF000F8–3FF00100` | Same shared sources, core-local GPIO slots |
| PRO source maps | `3FF00104–3FF00214` | Five-bit CPU input selector, except reserved slots |
| APP source maps | `3FF00218–3FF00328` | Independent selector for the other core |

Map fields initialize to documented reset value 16, disconnecting the sources.
Inputs 6, 7, 11, 15, 16 and 29 are CPU-internal: mapping a peripheral to any of
these disables that route without driving or clearing the internal input.
Other destinations OR all active mapped sources. Clearing one shared source
cannot deassert an input while another mapped source remains asserted. Active
sources can be remapped. Only defined field bits are stored/read; status writes
are ignored. Reserved map holes and unrelated DPORT registers remain unsupported.
The qualified bus profile requires aligned 32-bit accesses and rejects narrow
or misaligned transactions; MMIO instruction fetch and CAS are unsupported.

The two-context machine now exposes a unified data bus with these registers.
Matrix changes from firmware writes reach CPU inputs only after instruction
commit, avoiding mutation of a copied executing CPU state. Inputs are synchronized
before each runnable turn and after each instruction, for both profiles, including
a debug-paused peer. Separate high/low firmware writes therefore latch an edge
even when the peer has no intervening execution turn. Host bus/input calls are
allowed only between turns and synchronize immediately. Direct CPU `irq_line`
calls for peripheral input numbers conflict with this ownership; use the machine's
source/GPIO/NMI-mask APIs. Internal clock/software/profiling inputs remain
CPU-local. This is a serialized functional model, not host-thread-safe routing.

PID NMI-mask inputs are explicitly supplied per core and suppress only the
matrix NMI output; raw status remains readable. The PID controller itself,
source pulse shaping/minimum widths, APB/arbitration latency, hardware reset and
power domains, other DPORT registers, peripheral source generators, flash/cache,
SoC boot and real-hardware timing remain unqualified or absent. No vector address
or peripheral behavior is invented from a source number. No board target is
registered by this change.

Proof:

- `test_esp32_matrix`: 64,374 assertions across every defined source/destination,
  reset fields, core isolation, GPIO-specific status, shared OR, remapping,
  NMI masks, software-source fields, RO status and invalid accesses.
- `test_esp32_ipi`: 103 assertions for a fetched sender, peer WAITI wake,
  handler acknowledgement/RFI, paused-peer edge capture, same-core MMIO changes
  surviving instruction commit, shared edge-input OR and internal-input isolation.
- `run_matrix.py`: 4,288 independent vendor-addressed routes and 512 combined
  source/map/mask comparisons. It parses the hash-pinned vendor headers below
  for register addresses/defaults/source slots and CPU-internal input types.
- `run_ipi_c.py`: actual ESP32-compiled windowed C sender and C handler plus assembled
  entry/return wrapper,
  256 mailbox runs, 4,224 routed interrupts, 128 exact instruction/event replays
  and 512,128 retired instructions, in both directions. The ISR saves/restores
  integer registers on an EXCSAVE2 stack, uses EXCSAVE3 for the interrupted SP and calls the C handler
  with EXCM cleared and interrupt level 2 retained. The handler clears the source and
  publishes a release acknowledgement. Checks cover all messages, source clear,
  stack guards, general registers, ABI return and window state. This is not a
  complete production ISR, nested exception or RTOS context-switch proof.
- The native shared-RAM atomic/spinlock acceptance still passes unchanged.

External reference headers (ESP-IDF v4.4.7, used as facts, not linked code):

- [soc/dport_reg.h](https://github.com/espressif/esp-idf/blob/v4.4.7/components/soc/esp32/include/soc/dport_reg.h),
  SHA-256 `38deaa4ef02016ee9cdf490a9bfe4b587f11d1d1e6a2329dc858a50b9b248817`.
- [soc/soc.h](https://github.com/espressif/esp-idf/blob/v4.4.7/components/soc/esp32/include/soc/soc.h),
  SHA-256 `7a7148c288d7c289c961810e5617fa7b14d105b28349a45f772150b1e0845dd3`.
- The hash-pinned actual ESP32 `core-isa.h` described above supplies CPU input
  types. No vendor controller implementation or ROM is embedded.

```sh
python tests/vendor/xtensa_integer/run_matrix.py \
  --dport-reg /path/to/esp-idf/components/soc/esp32/include/soc/dport_reg.h \
  --soc-header /path/to/esp-idf/components/soc/esp32/include/soc/soc.h \
  --core-isa /path/to/xtensa-esp32-elf/include/xtensa/config/core-isa.h
python tests/vendor/xtensa_integer/run_ipi_c.py \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld \
  --nm /path/to/xtensa-esp32-elf-nm
```

## Implemented: APP clock/stall controls and ROM-consumed boot address

The unified machine bus now implements these TRM v5.9 chapter 12 fields,
checked against the same pinned ESP-IDF `dport_reg.h` above:

| Register | Address | Defined field | Initial field value |
| --- | --- | --- | --- |
| APPCPU_CTRL_B | `3FF00030` | Bit 0: APP clock enable | 0 |
| APPCPU_CTRL_C | `3FF00034` | Bit 0: APP run-stall | 0 |
| APPCPU_CTRL_D | `3FF00038` | 32-bit ROM-consumed boot address | 0 |

APP therefore requires an explicit clock-enable write before fixture execution.
`machine_configure` injects a debug architecture profile and entry, not a hardware
boot/reset operation: it preserves these controls, memory, remap and matrix.
Reserved control bits read zero and are ignored on writes. Only aligned 32-bit
data accesses are qualified; malformed accesses do not change control state.

A debug-paused core reports event 19. Otherwise APP reports event 20 if its clock
is disabled, or event 21 if stalled with its clock enabled. These turns do not
fetch, deliver interrupts or retire instructions; PC/registers and shared RAM
survive. A firmware store that disables its own execution commits before the
next suspended turn. PRO can resume APP through the same registers. Clock gate
has priority over stall; debug pause has priority over both. These are functional
instruction-boundary decisions, not a model of clock-domain synchronizer or
pipeline drain latency. Input routing remains functional at explicit boundaries;
short-pulse capture while the CPU clock is gated is not silicon-qualified.

`machine_clock_advance` accepts supplied clock ticks and discards APP ticks while
its clock is disabled, without catch-up on re-enable. The machine deadline API
reports no APP compare deadline while gated. With its clock enabled, supplied
ticks continue advancing the functional counter during stall, WAITI or a host
debug pause. This does not establish physical debug-clock policy or elapsed
wall time. The lower-level `clock_advance(core)` remains an explicit debug bypass
that does not honor machine gates. No ticks are inferred from instructions.
Pending requests remain pending while suspended and can enter on resume.

The boot-address register stores all 32 bits and never directly changes PC.
The TRM explicitly says ROM code jumps to this address after APP boots. Actual
Espressif ROM execution is not implemented. APPCPU_CTRL_A reset/release at
`3FF0002C` now requires the separate opt-in supported reset profile below;
clock enable alone is not a real cold-boot sequence. No synthetic reset state, ROM jump, reset cause/retention domain,
oscillator/PLL/APB conversion, power consumption or complete board boot is added.

Proof:

- `test_esp32_controls`: 163 assertions for initial/read/write fields, invalid
  accesses, self gate/stall and resume, suspended-round isolation, state/memory
  preservation, gate-aware clock/deadline behavior and pending interrupt delivery.
- `run_controls.py`: 384 field comparisons through both CPU bus views, using
  parsed pinned-vendor register addresses, field widths and defaults.
- `run_ipi_c.py --exercise-controls`: PRO C firmware stalls and gates APP around
  each message, writes/reads the boot-address field and raises a held source
  before releasing APP. The baseline bidirectional workload still passes, while
  the control probe additionally checks unchanged PC/registers on suspended
  turns and final control fields. It passes 256 mailbox runs, 4,224 interrupts,
  128 exact replays and 551,232 retired instructions. Its 27,456 gated and
  10,560 stalled turns are attempted instruction boundaries, not physical cycles.
- Baseline mailbox acceptance remains 512,128 retired instructions and the native
  shared-RAM/spinlock acceptance remains 229,322 instructions. Their fixture
  setup now explicitly enables APP's clock instead of bypassing its default.

```sh
python tests/vendor/xtensa_integer/run_controls.py \
  --dport-reg /path/to/esp-idf/components/soc/esp32/include/soc/dport_reg.h \
  --soc-header /path/to/esp-idf/components/soc/esp32/include/soc/soc.h
python tests/vendor/xtensa_integer/run_ipi_c.py \
  --compiler /path/to/xtensa-esp32-elf-gcc \
  --objcopy /path/to/xtensa-esp32-elf-objcopy \
  --linker /path/to/xtensa-esp32-elf-ld \
  --nm /path/to/xtensa-esp32-elf-nm \
  --exercise-controls
```

## Implemented: supported reset and relocatable-vector profiles

The [reset/vector contract and reproduction](esp32-reset-vectors.md) describe
explicit static reset-input selection, defined PS/LCOUNT/VECBASE state,
undefined-register fixture policy, aligned SR231 read/write/exchange and dynamic
exception/interrupt/window entry. APP reset hold/release is an opt-in DPORT
profile with commit-safe self-reset, clock/deadline suppression, peripheral
source preservation and no boot-address PC shortcut. Legacy debug fixtures
retain their existing contract until reset is enabled.

Evidence includes 2,281 CPU and 104 machine assertions, 3,072 independent
assembled VECBASE accesses, 960 vendor-offset dispatches, 128 reset-address
comparisons and 512 vendor-control-field comparisons. A compiled supervisor and
worker pass 128 reset cycles through an original temporary test-ROM program
(11,904 retired instructions, 16 exact replays). This is not Espressif ROM,
complete silicon reset, MMU/cache or real SDK/board boot qualification.

## Implemented: read-only mapped flash for PID0/1

The [flash mapping contract and reproduction](esp32-flash-mapping.md) describe
the two normal-mode tables, actual cache-control/mask addresses, immutable backing,
flush sequencing and access checks. Compiled firmware can program mappings in
internal RAM and execute code/read data at flash virtual addresses. Unsupported
live-remap/cache-tag/timing, SPI programming, PSRAM/high-PID and lock/page modes
remain explicit gaps; this does not qualify full SDK cache/mmap behavior.

Evidence: 7,289 native assertions, 488 vendor-fact translations, six control/flush
comparisons and 256 actual compiled mapped workers with per-core code/data
mappings (11,264 retired instructions, 64 replays). The original test-ROM reset
path also passes 128 cycles into a flash-mapped worker (15,360 instructions,
16 replays). Actual Espressif ROM/SDK/board boot remains unimplemented.

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

## Implemented: CPU task-context registers and compiler TLS

The [context register contract](esp32-context-registers.md) adds unprivileged
RUR/WUR.THREADPTR, privileged MISC0..3 read/write/exchange and an explicit
read-only PRID fixture input. Undefined THREADPTR/MISC reset values follow the
declared deterministic policy; full silicon PRID remains unresolved. Native
context tests (9,723 assertions), 7,680 independently assembled comparisons and 1,536 actual ESP32
C TLS calls pass through native RAM (six persistent banks, 16 exact replays,
52,224 retired instructions). MOVSP now checks the preceding three windows and
raises AllocaCause 5 before a stack move when restoration is needed. Another
13,475 native assertions and 32,768 assembled comparisons pass; the TLS runner's
`--dynamic-stack` variant passes 1,536 compiled dynamic-buffer calls and
253,812 instructions. Firmware owns base-save restoration and retry. No full
vendor alloca handler, scheduler, complete context switch or real ROM/SDK boot
claim follows.

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
