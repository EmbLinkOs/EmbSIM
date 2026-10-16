# 05 - Workbench, Protocol and Command Line

**Status:** ACCEPTED. Contracts in SPEC-008 (protocol), SPEC-011 (workbench).
Decisions: ADR-007, ADR-008, ADR-013.

---

## 1. The session protocol

JSON-RPC 2.0 over three transports with identical semantics:

| Transport | Used by | Framing |
|---|---|---|
| stdio | the desktop workbench's sidecar, IDE integrations | newline-delimited JSON; binary streams as base64 or a side channel pipe |
| WebSocket | the browser workbench, remote tools | text frames for JSON, binary frames for streams |
| in-process | the CLI, the C API's embedding, tests | direct calls with the same method table |

Method families (SPEC-008): `session.*` (open, close, info, policy),
`project.*` (get, set, validate, topology transactions), `control.*` (run,
pause, step, advance, reset, speed), `input.*` (press, release, set, serial
write, wire edits), `probe.*` and `waveform.*` (add, remove, subscribe,
binary sample frames), `debug.*` (breakpoints, registers, memory, source
locations, continue, step), `catalog.*` (list, get, search, validate),
`record.*` (start, stop, replay), `toolchain.*` (detect, build),
`diagnostics` (a notification stream). Every method is versioned with the
protocol; a client negotiates capabilities on `session.open`.

Observations never block control: waveform streaming runs on its own queue;
a client that falls behind drops frames and receives a gap marker. The
protocol carries virtual time as a decimal string of picoseconds so a browser
is never limited by 2^53.

## 2. The command line

`embsim` is one binary linking the core, with subcommands:

| Subcommand | Does |
|---|---|
| `run PROJECT [--scenario S] [--until T] [--assert] [--json] [--record F]` | headless run; prints observations and assertion results; exit status reflects assertions and diagnostics |
| `replay RECORDING` | reruns a recording, reports divergence |
| `serve PROJECT [--port N] [--open]` | starts the WebSocket server and serves the workbench |
| `sidecar` | stdio protocol endpoint for a desktop shell |
| `validate PATH...` | validates project, component, board, MCU, library files against the schemas and semantic rules |
| `catalog list|info|search` | browses the catalog and libraries |
| `board list|info NAME` | boards and their ports, LEDs, buttons |
| `build PROJECT [--toolchain P]` | builds the project's firmware through a toolchain profile |
| `gdb PROJECT [--port N]` | runs with a GDB stub waiting |
| `trace`, `coverage`, `profile`, `stack` | the EmbCC simulator's analyses, when the backend supports them |
| `script FILE.lua` | runs a Lua script against a session (test scenarios, generators) |

Output is human text by default and JSON with `--json` for machines; errors
go to stderr with a stable prefix and exit codes documented in SPEC-008 §9.

## 3. The workbench

A single TypeScript application delivered two ways: inside a Tauri 2 desktop
shell with the `embsim sidecar` process, and from `embsim serve` in a browser.
The application never links simulation code; it is a protocol client.

### 3.1 Perspectives

| Perspective | Panels | Purpose |
|---|---|---|
| **Design** | schematic canvas, component library, inspector, board view, net list | build the circuit: place, wire, parametrize, choose a board, assign firmware |
| **Run** | board view with live hotspots, instruments (logic analyzer, oscilloscope, serial terminal, protocol decoders, meters), controls, inputs | interact while the firmware runs |
| **Debug** | source, disassembly, registers, memory, peripherals (register view with field decode), breakpoints, call stack, variables | debug firmware at source level with the circuit live |
| **Verify** | scenario editor, assertions, recordings, run reports, coverage | turn an experiment into a repeatable check |

A global toolbar carries run, pause, step, advance, reset, speed, the virtual
clock, the fidelity policy and the diagnostics badge; a command palette
(Ctrl/Cmd-K) reaches every action; panels dock, float, resize and persist per
project.

### 3.2 Rendering

- Schematic and board views are SVG scenes with pan, zoom, snap grid, wire
  routing, selection, undo and redo; component symbols are SVGs from the
  catalog with declared pin anchors; boards are SVG artwork with hotspots.
  Virtualisation keeps 500-component schematics at 60 Hz.
- Waveforms (oscilloscope, logic analyzer) are drawn on a canvas with WebGL
  where available, from binary sample frames, with cursors, measurements and
  zoom independent of the simulation rate.
- Live state (net colours, LED brightness, button state, display contents) is
  driven by observation notifications at a client-chosen rate; the model is
  never polled from the render loop.

### 3.3 Design system

A token-based design system (colour, type, spacing, elevation, motion) with
light and dark themes, WCAG AA contrast, a monospaced face for identifiers,
time and values, and a signal palette (low, high, floating, contention,
analog) that is also distinguishable without colour through labels and
glyphs. Keyboard operation covers every control; focus is stable while
observations refresh; reduced-motion preferences are honoured; layouts adapt
down to a 1024-pixel width without hiding required actions. The look is
restrained and precise: an instrument, not a dashboard.

### 3.4 Delivery

Desktop builds for Linux (AppImage, deb), macOS (dmg, universal) and Windows
(msi) from one Tauri configuration, bundling the `embsim` sidecar and the
catalog; the browser mode serves the same assets from the CLI. Both run
without network access.

## 4. Scripting

Two tiers: Lua inside the core for scenarios, generators and behavioural
models, sandboxed (no `io`, `os`, `debug`, bounded instruction count); and any
language outside through the protocol, with a maintained Python client
(`tools/embsim_client`) used by the test suites. Scripts enter inputs through
the same command path as a user and are therefore recorded and replayable.

## 5. Interoperability

- EmbLinkRTOS: board and SoC YAML import to drafts; the RTOS's CI uses
  `embsim run --json` as an emulated target; the RTOS's trace output can be
  captured from a serial or ITM probe.
- EmbCC: `embcc` is the default toolchain profile; `embdbg` connects to the
  GDB stub; EmbCC's golden fixtures become EmbSIM differential tests.
- Others: GDB and LLDB through the stub; VS Code through the protocol
  (FUTURE extension); KiCad netlists through the importer.
