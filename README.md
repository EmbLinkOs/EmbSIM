# EmbSIM

EmbSIM is the EmbLink virtual electronics workbench: it runs the real
firmware of a microcontroller on a model of its board, inside a circuit built
from real electronic components, with the time, voltages and logic levels the
hardware would have, and lets you press its buttons, turn its pots, probe its
wires and debug its code while it runs. The same experiment runs headless from
the command line in continuous integration and replays deterministically.

**Current phase: M2 workbench.** The core library, command line, protocol,
catalog and workbench run circuits (M0); a Blue Pill's STM32F103C8 runs
real firmware in them with source-level debugging (M1); the workbench
edits circuits against the engine, decodes serial buses and ships as a
desktop application (M2). Nothing is released
yet: [`docs/STATUS.md`](docs/STATUS.md) is the only inventory of what exists
and with what evidence.

![The workbench decoding the Blue Pill's serial output](docs/gallery/m2/decode-bluepill.png)

The [gallery](docs/gallery/README.md) shows the workbench at each milestone.

Start with [`docs/README.md`](docs/README.md) for the design reading order,
then [`docs/architecture/01-vision-and-principles.md`](docs/architecture/01-vision-and-principles.md).

## What it is designed to do

- **Real firmware.** Unmodified ELF and Intel HEX images from EmbCC, GCC,
  Clang and Microchip XC toolchains; source-level debugging through DWARF and
  the GDB remote protocol.
- **Real register behaviour.** Peripherals instantiated from register
  descriptions written from the reference manuals (vendor SVD files convert
  with a tool) with behavioural models; unsupported accesses are observable,
  never silently absorbed.
- **Real electronics.** A dependency-free analog engine (modified nodal
  analysis) for the circuit around the MCU: resistors, capacitors, diodes,
  LEDs, transistors, op-amps, regulators, with mixed-signal bridges to the
  MCU's pins; a four-value digital engine with strengths for logic.
- **Real interaction.** Buttons, switches, pots, sensors, serial lines and
  wires manipulated while the firmware runs, every action timestamped in
  virtual picoseconds, every run recordable and replayable.
- **One engine, every interface.** A graphical workbench (desktop and
  browser), a command line, scripts and other EmbLink tools drive one core
  through one session protocol.
- **Boards and components as data.** JSON descriptions validated by
  published schemas; a model SDK with declarative, scripted (Lua) and native
  tiers; mandatory fidelity declarations and provenance.
- **Linux, macOS and Windows** from one source tree; the core builds with
  GCC, Clang, TinyCC, MSVC and EmbCC.

The [component library guide](catalog/README.md) lists the first 26 named
manufacturer/package entries, category coverage, model limits and executable
fixtures. This is a first batch, not Proteus-scale catalog coverage or
manufacturer-qualified device accuracy.

## Repository layout

| Path | Content |
|---|---|
| `docs/` | architecture, specifications, requirements, engineering process, user guide, status |
| `include/embsim/` | the public C API (SPEC-001) |
| `core/` | `libembsim`: scheduler, nets, digital and analog engines, models, project loader, session, protocol, MCU runtime, host layer |
| `cli/` | the `embsim` command line |
| `catalog/` | the bundled `emblink` library: components, MCUs, packages, boards, toolchains, symbols |
| `schemas/` | JSON Schemas for every description kind |
| `ui/` | the workbench (TypeScript, React, Vite) |
| `desktop/` | the Tauri desktop shell |
| `sdk/` | the component and board author guide and templates |
| `tools/` | Python tools: validation, importers, fixture generators, the Python protocol client, repository policy |
| `tests/` | C unit tests, analog and digital fixtures, protocol conformance, determinism, differential suites |
| `examples/` | example projects and scenarios |
| `third_party/` | vendored, permissively licensed sources (none yet) |

## Building

```sh
cmake --preset dev            # GCC or Clang; see CMakePresets.json for others
cmake --build --preset dev
ctest --preset dev --output-on-failure
./build/dev/bin/embsim run examples/button-led/project.json --until 50ms --json
```

`make` wraps the common presets on Unix (`make`, `make test`, `make CC=tcc`).
The workbench needs Node 22 and pnpm (`cd ui && pnpm install && pnpm dev`);
the desktop shell needs Rust (`cd desktop && cargo tauri dev`). Python 3.10+
is needed for the tools and CI only.

## Part of the EmbLink platform

EmbSIM is designed with [EmbCC](https://github.com/EmbLinkOs/EmbCC) (whose
simulator cores it extracts with provenance) and
[EmbLinkRTOS](https://github.com/EmbLinkOs/EmbLinkRTOS) (whose board
descriptions it imports and whose CI it serves as an emulated target), and
remains fully usable with third-party compilers, debuggers and build systems.

## Licence and contributing

Apache-2.0 (see `LICENSE` and `NOTICE`). See `CONTRIBUTING.md` for the
process, the identity rule for commits, and the required checks.
Development process: [repository workflow](docs/engineering/repository-workflow.md),
[roadmap](docs/architecture/07-roadmap.md), [changelog](CHANGELOG.md).
