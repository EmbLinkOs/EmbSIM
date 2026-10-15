# 02 - System Architecture

**Status:** LOCKED layering; ACCEPTED vocabulary and dependency rules.

---

## 1. Layered view

```
+--------------------------------------------------------------------------+
|  Clients                                                                 |
|   Workbench (desktop: Tauri shell; browser)  |  embsim CLI  |  CI runner |
|   scripts (Lua in-core, Python over protocol) |  IDEs, EmbLinkRTOS tools |
+--------------------------------------------------------------------------+
|  Session protocol    JSON-RPC 2.0 + binary streams, over stdio,          |
|                      WebSocket, or in-process (SPEC-008)                 |
+--------------------------------------------------------------------------+
|  libembsim (C99, dependency-free)                                        |
|   session | project | catalog | record/replay | probes | diagnostics     |
|   --------------------------------------------------------------------   |
|   scheduler and time  |  net graph: nets, pins, islands                  |
|   digital engine      |  analog engine (MNA)   |  mixed-signal bridges   |
|   --------------------------------------------------------------------   |
|   MCU runtime: core backends | register files (SVD/ATDF/EDC) |           |
|                peripheral models | pin multiplexer | package pin maps    |
|   loaders (ELF, HEX, DWARF) | debug (GDB RSP) | toolchain profiles       |
|   --------------------------------------------------------------------   |
|   model registry: primitive C models | composites | scripted (Lua)       |
|   --------------------------------------------------------------------   |
|   host layer (files, sockets, threads, clock for pacing only)            |
+--------------------------------------------------------------------------+
|  Catalog (data): components, MCUs, packages, boards, symbols, artwork,   |
|                  libraries; schemas; conformance fixtures                |
+--------------------------------------------------------------------------+
```

Cross-cutting, as data not layers: JSON Schemas for every description kind;
fidelity declarations; requirements to tests to evidence.

## 2. Layer responsibilities

| Layer | Owns | Must never |
|---|---|---|
| **Clients** | Presentation, interaction, pacing, files on the user's disk, toolchain invocation on request | Compute a simulation result; mutate a model except through a session command |
| **Session protocol** | Versioned methods, event and stream framing, capability negotiation, error codes | Carry a client-specific structure; bypass the session command path |
| **Session** | A loaded project under control: run, pause, step, advance, reset; the command path that stamps every external input with virtual time and sequence; observations; recording | Depend on a transport |
| **Project and catalog** | Loading, validating and resolving descriptions into instances; libraries; provenance | Contain behaviour |
| **Scheduler and time** | Virtual time, event queue, delta cycles, clocks, quanta for cores and analog islands | Read the wall clock |
| **Net graph** | Nets, pins, pin models, islands, resolution, contention and float diagnostics | Know a component's internals |
| **Digital engine** | 4-value logic with strengths, propagation, reactive models | Solve a voltage |
| **Analog engine** | MNA operating point and transient, device models, step control, convergence diagnostics | Schedule a digital event directly (it reports crossings to the bridge) |
| **Mixed-signal bridges** | Digital pin to electrical driver, electrical node to sampled logic, ADC and DAC terminals, threshold-crossing events | Hide a conversion parameter |
| **MCU runtime** | Core backends, memory, register files, peripheral behaviour, interrupts, pin multiplexing, package maps, unsupported-access policy | Exit the process; share state between instances; know a board |
| **Loaders and debug** | ELF, HEX, DWARF; GDB remote protocol; breakpoints and watchpoints; symbols | Change timing when attached |
| **Model registry** | Registration and lifecycle of primitive, composite and scripted models; parameter typing; fidelity declarations | Load native code implicitly |
| **Host layer** | The only OS-specific code: files, sockets, threads, high-resolution clock for pacing | Leak into a model |
| **Catalog** | Descriptions, artwork, fixtures, licences and provenance | Contain executable code except Lua models declared as such |

## 3. Concept vocabulary

| Concept | Definition |
|---|---|
| **Virtual time** | Unsigned 64-bit picoseconds since session reset. The only time models see. |
| **Clock** | A rational period (numerator and denominator in picoseconds) with a phase accumulator; drives a core or a timed model without drift. |
| **Event** | A scheduled state change at a virtual time with a priority class and a sequence number; ordering is total and documented (03 §2). |
| **Delta cycle** | Zero-time propagation round within one timestamp; bounded; overflow is a diagnostic. |
| **Net** | An electrical node joining pins. Has a resolved state: a logic value with strength in a digital island, a voltage in an analog island. |
| **Pin** | A terminal of a component instance, with a **pin model** (digital output with drive type and strength, digital input with thresholds and pull, analog terminal, power, passive, no-connect). |
| **Island** | A maximal set of nets solved by one engine: digital (event-driven) or analog (MNA). Bridges connect islands. Islands are recomputed on topology change. |
| **Bridge** | The conversion between islands at a pin: Thevenin driver, threshold comparator, sampled terminal. |
| **Component** | An instance of a component description with parameters, pins, state and a model. |
| **Component description** | A JSON file in the catalog: identity, pins, parameters, model binding, symbol, artwork, fidelity, provenance, tests (SPEC-005). |
| **Model** | The behaviour behind a component: a primitive implemented in C, a composite netlist of other components, or a scripted (Lua) behaviour. |
| **MCU** | A core, memory, clocks, peripherals and a pin multiplexer, described by an MCU description and backed by a core backend and peripheral models (SPEC-006, SPEC-009). |
| **Package** | The mapping from an MCU's signals to physical pins of a package variant. |
| **Board** | A composite component: an MCU in a package, on-board components, nets, connectors exposed as ports, artwork with interaction hotspots (SPEC-006). |
| **Project** | Boards and components placed and wired, firmware images, scenarios, probes, layout (SPEC-007). |
| **Scenario** | A list of timestamped external inputs and assertions; the replayable part of an experiment. |
| **Probe** | A named observation: a net, a pin, a register, a memory range, a variable, a serial stream; produces a waveform or a log. |
| **Session** | A project loaded and under control; the unit the protocol addresses. |
| **Fidelity tier** | `functional` (logic and connectivity), `timed` (documented delays and clocks), `electrical` (voltage, current, impedance, nonlinear behaviour). Declared per model with claims and validation references. |
| **Toolchain profile** | A description of how to detect and invoke a compiler and linker for a target, and what it produces (SPEC-010). |
| **Library** | A versioned directory of descriptions, models, artwork and fixtures that the catalog loads (SPEC-014). |
| **Recording** | A versioned file of a session's external inputs with timestamps and ordering, plus identities of everything that shaped the run; replay reproduces it (SPEC-012). |

## 4. Dependency rules

1. `libembsim` depends only on the C standard library and its own host layer.
   Optional subsystems (Lua models, the WebSocket server) are compile-time
   options with vendored, permissively licensed sources under `third_party/`.
2. The workbench and the CLI depend on the protocol, never on core internals.
   The CLI links the core in-process and speaks the same methods.
3. Catalog data never depends on a client. Artwork is SVG with documented
   conventions; a symbol works in any client.
4. Core backends extracted from EmbCC depend on the backend contract
   (SPEC-009) only: no `die()`, no file-scope mutable state, no `stdout`.
5. No layer reads the wall clock except the host layer, and only for pacing in
   the session's `run` command, which converts it to a virtual-time target.

## 5. From a project to a running machine

1. **Load.** The project file is validated against its schema; referenced
   libraries are resolved; every description is validated and its fidelity
   declaration checked for the requested policy (strict refuses `functional`
   where `electrical` is required).
2. **Instantiate.** Boards expand into their MCU, package and on-board
   components; composites expand recursively; each component instance gets
   typed parameters with units and defaults applied; pins get pin models.
3. **Wire.** Nets are built from the project's wires and the boards' internal
   nets; connectors join; diagnostics name unconnected required pins and
   conflicting power nets.
4. **Partition.** Islands are computed from the pin models on each net; bridges
   are placed where a digital pin meets an analog island or an analog
   terminal meets a digital island.
5. **Prepare firmware.** Each MCU's image is loaded, its toolchain and debug
   info recorded, its core reset; peripheral register files are instantiated
   from the vendor description and bound to behaviour models.
6. **Run.** The scheduler advances virtual time: core quanta, analog steps and
   discrete events interleave under the ordering of 03 §2; probes capture;
   diagnostics stream; recording captures inputs.
7. **Control.** Every external change (press, voltage, byte, wire edit) enters
   through the session command path, acquires a virtual timestamp and a
   sequence number, and is recorded. Topology edits pause at a safe boundary,
   validate, and apply atomically or not at all.

## 6. How a component arrives

A component author writes `component.json` (SPEC-005) with pins, parameters,
symbol, artwork, fidelity and provenance; chooses a model: a primitive the
core already implements (resistor, diode, NPN, logic gate, LED), a composite
of other components (a 74HC00 is four NAND gates and a power pin model), or a
Lua behaviour (a sensor with a transfer function and a protocol). They add
conformance fixtures: for electrical models, a netlist and the expected
operating point or waveform, generated from ngspice or from the datasheet;
for digital models, a stimulus and expected trace. `embsim catalog validate`
checks everything; the library is then usable by path, by project or by
installation into the user's library directory. No C is written for the
declarative tiers.

## 7. How a board arrives

A board author writes `board.json` (SPEC-006): the MCU and package, the
board's nets, every on-board component (regulator, crystal, LEDs, buttons,
pull-ups, series resistors, headers) as catalog component instances, the
connectors exposed as ports with their pin names, artwork (an SVG with
hotspots for buttons, LEDs, headers, switches) and provenance (schematic
reference, revision). An importer bootstraps a draft from an EmbLinkRTOS board
YAML, a Zephyr DeviceTree or a KiCad netlist. Validation checks every net
against the package's pins and every hotspot against a component. The MCU
itself arrives separately: an MCU description plus a register file from the
vendor's SVD, ATDF or EDC, bound to peripheral models that exist or are
written in C against SPEC-009.

## 8. Profiles of use

| Profile | Client | Core subsystems | Typical use |
|---|---|---|---|
| Headless | CLI, CI | all but server | `embsim run project.json --scenario smoke.json --until 2s --assert` |
| Serve | browser workbench | all, WebSocket server | `embsim serve project.json` |
| Desktop | Tauri workbench with sidecar | all, stdio protocol | double-click the app |
| Embedded | another program through the C API | chosen subsystems | EmbLinkRTOS HIL runner, an IDE |
