# The command line

`embsim` is one binary that links the core. Every subcommand is a client of
the session protocol (SPEC-008); `--json` switches any output to the
machine-readable report of SPEC-008 §8.

| Command | What it does |
|---|---|
| `embsim run PROJECT [--until T] [--scenario FILE] [--policy P] [--probe TARGET]... [--record FILE] [--json]` | loads the project, runs it headless to `--until` (default 50 ms) or through the scenario's inputs and assertions, prints nets, outputs, assertions and statistics; exit 0 ok, 1 an assertion failed, 2 usage or load error, 3 a fatal diagnostic |
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
| `embsim catalog install DIR [--to LIBRARIES_DIR] [--force]` | validates a library and copies it into the user library directory |
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

Diagnostics go to stderr as `embsim: <severity>: <code> (<subject>)` followed by
the message; their codes are listed in [diagnostics](diagnostics.md).
