# EmbCC simulator assessment

Inspected upstream `EmbLinkOs/EmbCC` at
`aa4f10a3f5a94456bd757bcfa0d9c25f461590d9` on 2026-10-15.
Source: `tools/embsim`, shared parser `tools/embsvd`, instruction costs in
`tools/bench/cost.h`, and `tests/golden/embsim*` fixtures/harnesses.

Approximately 35,000 lines across 88 C source files implement CPU interpreters,
MMIO devices, selected board components, loading, debugging and analyses.
Families include Cortex-M, RISC-V, AVR, MIPS, PIC24 and dsPIC. Board/peripheral
coverage varies. SVD, ATDF and EDC import register metadata, not complete physical
device behavior.

Reuse candidates: instruction semantics, exception/interrupt handling, bus access,
ELF/HEX loaders, peripheral knowledge, GDB protocol, DWARF analysis, trace and replay
concepts, and existing expected-output fixtures. Preserve their provenance when
bringing source into this repository; no source has been imported yet.

Redesign boundaries: kernel/session lifecycle, structured errors, arbitrary net
connectivity, time synchronization, live input command handling, versioned board
descriptions, fidelity declarations, independent GUI, and analog coordination.
`struct board_io` supplies callbacks rather than a reusable electrical graph.
CLI button settings run before execution. Current instruction cycles are estimates.
Some unmodeled peripheral addresses silently read zero.

## Observed validation

- Default `make embsim` fails under this environment's GCC with warnings as errors.
- `boards.c:512`: possible null `%s`; the preceding `die` terminates but lacks a
  compiler-visible no-return contract.
- `pic32-gpio.c:279`: multi-statement `REG` macro under unbraced `if` statements;
  register lookup executes outside the condition and can replace a valid lookup.
- `make TARGETS=arm embcc embld` fails linking EmbCC on missing
  `embdbg_decode_one` references.
- Diagnostic simulator build succeeded with only the two warning categories
  changed to nonfatal. A minimal Cortex-M HEX idle-loop run completed as expected.
- Existing SVD register-map output matched `regs-map.txt` exactly.
- Full ISA, peripheral, replay and debugger suites were not run. No broad target
  correctness claim follows from these smoke checks.

The assessment checkout was kept outside EmbSIM. No upstream source was changed.
