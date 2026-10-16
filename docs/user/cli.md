# The command line

`embsim` is one binary that links the core. Every subcommand is a client of
the session protocol (SPEC-008); `--json` switches any output to the
machine-readable report of SPEC-008 §8.

| Command | What it does |
|---|---|
| `embsim run PROJECT [--until T] [--scenario FILE] [--policy P] [--probe TARGET]... [--record FILE] [--coverage FILE] [--profile FILE] [--stack FILE] [--faults FILE] [--board ID] [--json]` | loads the project, runs it headless to `--until` (default 50 ms) or through the scenario's inputs and assertions, prints nets, outputs, assertions and statistics; with `--coverage`, `--profile`, `--stack` and `--faults`, writes the firmware's coverage, profile, stack depths and faults (below); exit 0 ok, 1 an assertion failed, 2 usage or load error, 3 a fatal diagnostic |
| `embsim run --target BOARD --image FILE [--until T] [--until-text TEXT] [--console] [--json]` | one board of the catalog (`emblink/arduino_uno`) running an image, with no project: the way an RTOS's or a library's test runner uses the simulator as an emulated target ([CI](ci.md)). `--console` prints the board's console as the run goes (to stderr with `--json`); `--until-text` ends the run at the millisecond the text appears on a console (ended reason `until_text`), exit 1 when it never does. With a project, `--image [ID=]FILE` replaces a board's image |
| `embsim replay RECORDING [--allow-drift] [--json]` | runs a recording again: its project from reset, its inputs at their times, to its end, comparing what the run observes with what was recorded, in order; exit 0 when all match, 4 at the first divergence (both observations printed, or in `divergence` with `--json`), 2 when the project or an image changed since the recording unless `--allow-drift` |
| `embsim record to-scenario RECORDING [-o FILE] [--assert probes,serial\|none] [--name N]` | a scenario from a recording: its inputs at their times, and assertions on each digital probe's last level and edges and each serial probe's text |
| `embsim validate PATH...` | loads each project through the real loader and reports its diagnostics (descriptions are validated against the schemas by `tools/validate.py`) |
| `embsim serve [PROJECT] [--port N] [--host H] [--ui DIR] [--open] [--allow-origin ORIGIN]...` | the WebSocket endpoint on loopback plus the workbench assets (the built `ui/apps/workbench/dist` when present, else a built-in console page); `--allow-origin` admits browser pages from another origin, such as a desktop webview (SPEC-008 §1). Without a project the session starts empty and the workbench opens one |
| `embsim sidecar [PROJECT]` | the protocol over stdin and stdout, one JSON object per line, for the desktop shell and IDEs |
| `embsim gdb PROJECT [--board B] [--port N] [--host H]` | runs the project and serves GDB's remote protocol for one board's MCU on loopback (default port 3333): `target extended-remote :3333` in GDB, `gdb-remote 3333` in LLDB; see [firmware](firmware.md) |
| `embsim build PROJECT [--board B] [--toolchain T]` | builds a board's firmware from its `firmware` block (sources, flags, linker script) with the named or the block's toolchain profile, writing the block's `image` |
| `embsim toolchains` | the toolchain profiles and whether each is found on this host, with its version |
| `embsim catalog list [KIND] \| search QUERY \| info ID \| libraries [--library DIR]` | browses the bundled catalog and extra libraries |
| `embsim catalog validate [DIR] [--library DEP] [--json]` | instantiates every part of a library and runs its fixtures (bundled catalog by default); exit 1 unless all are ok |
| `embsim catalog new component\|board NAME [--library DIR] [--lua]` | writes a part that validates as written (SDK, `sdk/README.md`) |
| `embsim catalog install DIR\|ARCHIVE.zip\|URL [--to LIBRARIES_DIR] [--force]` | validates a library and copies it into the user library directory: a directory, a zip archive of one (the library at its top or in its top directory), or an `http(s)://` URL to such an archive, fetched with `curl` (SPEC-014 §6) |
| `embsim board list \| info ID` | boards and their descriptions |
| `embsim models` | the model primitives the core implements, digital and electrical |
| `embsim version` | engine, protocol and ABI versions |

Times are written with units: `10ms`, `1.5us`, `12345ps`, `2s`. The bundled
catalog is found next to the executable (`../catalog`, `../../catalog`,
`../../../catalog` from a build tree), from `EMBSIM_CATALOG`, or from the
source tree the binary was built from. `--library DIR` adds a library;
projects also name libraries relative to their own file.

## Recordings

`embsim run ... --record FILE` writes what the run did (SPEC-012): the
engine, the project's and each image's SHA-256, the policy, every input in
order, and the observations, each with its time in picoseconds:

- `irq`: a core entered an exception or interrupt (`board`, `core` on a part
  with two, `n`: a Cortex-M's exception number, an AVR's vector, a RISC-V
  cause, 16 plus an interrupt's cause or vector on RISC-V and MIPS, a PIC24's
  vector number);
- `probe`: a digital probe's level changed (`0`, `1`, `Z`, `X`), its first
  level at reset included;
- `serial`: each serial probe's text at the end; `diagnostic`: each warning,
  error or fatal code; `end`.

A run that records starts from reset, as a scenario does, so `embsim replay`
sees the same thing from the same start. In the workbench, the protocol's
`record.start` and `record.stop` record from the moment recording starts;
replay compares from there.

## Coverage, profile, stacks and faults

`--coverage FILE`, `--profile FILE`, `--stack FILE` and `--faults FILE` watch every board's firmware (one
board's with `--board ID`) instruction by instruction and write, at the end
of the run, which source lines and functions ran and where the time went
(`-` writes to stderr). Functions come from the image's ELF symbol table,
lines from its DWARF line tables, so build it with `-g`; a HEX image or a
stripped ELF is refused.

- `--coverage-format text` (the default): per source file, its lines and
  functions hit, and the source with each line's count beside it, as gcov
  lays it out (`#####` a line with code that never ran, `-` a line with
  none). A line's count is its most-executed instruction's, a function's its
  first instruction's: the times it was entered.
- `--coverage-format lcov`: an lcov tracefile (`TN` the board, `SF`, `FN`,
  `FNDA`, `DA`, `LF`, `LH`), which `genhtml` and CI coverage services read.
- `--profile-format text` (the default): each function's own and inclusive
  instructions and cycles, and its calls, by own cycles. The cycles are the
  instructions' (and exception entries'); the time a core sleeps in WFI is
  no instruction's, and the header gives the machine's elapsed cycles beside
  them.
- `--profile-format collapsed` (or `collapsed-insns`): one line per call
  path, `Reset_Handler;main;putc 1234`, by cycles (or instructions), for
  `flamegraph.pl`, speedscope and inferno; a part's second core's paths
  start with `core1`, and several boards' with the board's ID.
- `--stack-format text` (the default): each core's stacks, their top, how
  deep they went and of how much (down to the link's `__stack_limit`, else
  the end of `.data` and `.bss`, else the RAM's end), an overflow, and the
  functions by depth: the deepest sp in each, its own frame (as
  `-fstack-usage` counts it) and its depth with its callees. A Cortex-M's
  process stack (PSP, an RTOS's threads) is reported beside its main stack.
  The main stack going past its limit is the error diagnostic
  `mcu.stack_overflow`, naming where; the run goes on.
- `--faults-format text` (the default): each fault the firmware took, as
  its core took it: a Cortex-M's HardFault, MemManage, BusFault or
  UsageFault with HFSR and CFSR in words (`FORCED`, `PRECISERR at
  0x30000000`), a RISC-V exception with mcause and mtval, a MIPS exception
  with BadVAddr, a PIC24 trap; the instruction, the registers (a Cortex-M's
  stacked frame), a backtrace from the image's `.debug_frame` (build with
  `-g`), else from the calls the core was seen to make, and the handler it
  goes to. Each is also the error diagnostic `mcu.fault`; the run goes on.
- `json` for any: what the protocol's `analysis.coverage`,
  `analysis.profile`, `analysis.stack` and `analysis.faults` return
  (SPEC-008 §5). With `--json`, the run's report also carries `coverage`,
  `profile`, `stack` and `faults` summaries.

Calls and returns are recognized from each core's instructions (BL and BX
on a Cortex-M, JAL and JALR on RISC-V, CALL and RET on an AVR, MIPS's JAL
and JR after their delay slots, a PIC24's CALL and RETURN), and exception
entries and returns from the core itself, so a handler's time is its own.
The analyses cost little: the Pico's two-core scenario runs in the same
wall time with both on.

```sh
embsim run examples/bluepill-button-led/project.json --scenario examples/bluepill-button-led/scenarios/press.json \
    --coverage cov.info --coverage-format lcov --profile prof.folded --profile-format collapsed
genhtml cov.info -o coverage-html
flamegraph.pl prof.folded > profile.svg
```

Diagnostics go to stderr as `embsim: <severity>: <code> (<subject>)` followed by
the message; their codes are listed in [diagnostics](diagnostics.md).
