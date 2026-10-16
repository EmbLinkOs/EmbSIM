# The command line

`embsim` is one binary that links the core. Every subcommand is a client of
the session protocol (SPEC-008); `--json` switches any output to the
machine-readable report of SPEC-008 §8.

| Command | What it does |
|---|---|
| `embsim run PROJECT [--until T] [--scenario FILE] [--policy P] [--probe TARGET]... [--record FILE] [--json]` | loads the project, runs it headless to `--until` (default 50 ms) or through the scenario's inputs and assertions, prints nets, outputs, assertions and statistics; exit 0 ok, 1 an assertion failed, 2 usage or load error, 3 a fatal diagnostic |
| `embsim validate PATH...` | loads each project through the real loader and reports its diagnostics (descriptions are validated against the schemas by `tools/validate.py`) |
| `embsim serve PROJECT [--port N] [--host H] [--ui DIR] [--open]` | the WebSocket endpoint on loopback plus the workbench assets (the built `ui/apps/workbench/dist` when present, else a built-in console page) |
| `embsim sidecar [PROJECT]` | the protocol over stdin and stdout, one JSON object per line, for the desktop shell and IDEs |
| `embsim gdb PROJECT [--board B] [--port N] [--host H]` | runs the project and serves GDB's remote protocol for one board's MCU on loopback (default port 3333): `target extended-remote :3333` in GDB, `gdb-remote 3333` in LLDB; see [firmware](firmware.md) |
| `embsim build PROJECT [--board B] [--toolchain T]` | builds a board's firmware from its `firmware` block (sources, flags, linker script) with the named or the block's toolchain profile, writing the block's `image` |
| `embsim toolchains` | the toolchain profiles and whether each is found on this host, with its version |
| `embsim catalog list [KIND] \| search QUERY \| info ID \| libraries [--library DIR]` | browses the bundled catalog and extra libraries |
| `embsim board list \| info ID` | boards and their descriptions |
| `embsim models` | the model primitives the core implements, digital and electrical |
| `embsim version` | engine, protocol and ABI versions |

Times are written with units: `10ms`, `1.5us`, `12345ps`, `2s`. The bundled
catalog is found next to the executable (`../catalog`, `../../catalog`,
`../../../catalog` from a build tree), from `EMBSIM_CATALOG`, or from the
source tree the binary was built from. `--library DIR` adds a library;
projects also name libraries relative to their own file.

Diagnostics go to stderr as `embsim: <severity>: <code> (<subject>)` followed by
the message; their codes are listed in [diagnostics](diagnostics.md).
