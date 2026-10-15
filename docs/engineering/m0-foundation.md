# M0 foundation: what was built and how it was checked

This page records the first implementation against the baseline design. It
is the source for the M0 rows of [STATUS.md](../STATUS.md); update both
together.

## What exists

| Area | Files | Contract |
|---|---|---|
| Public API | `include/embsim/*.h` | SPEC-001: `es_session_*`, `es_catalog_*`, `es_serve_*`, versions, `es_strerror`, `es_free` |
| Utilities | `core/util/` | allocator hooks, vectors, string buffers, JSON parser and writer, SI and time parsing, diagnostics log, hash map |
| Time | `core/time/` | SPEC-002: rational clocks, the event heap with classes, delta-cycle bound, cancellation by generation |
| Engine | `core/engine/` | SPEC-003: components, pins with pin models and logic families, nets, four-value resolution with strengths, inertial delays, islands, bridges, the model API |
| Digital models | `core/model/digital.c` | gates, tristate, D flip-flop, clock and pulse sources, logic level, pull, transmission-gate switches, functional indicator, logic probe, UART virtual terminal |
| Registry | `core/model/registry.c` | primitive names to model operations; analog devices wrapped as components; same-named digital and electrical tiers merged |
| Analog engine | `core/analog/` | SPEC-004: MNA with Newton-Raphson, LTE step control, breakpoints, bridges with crossing bisection, the v1 device set |
| Catalog and loader | `core/project/` | SPEC-005 to SPEC-007 subset: libraries by path, component instantiation with parameter validation and `$param` pin models, boards as composites with a placeholder MCU, net merging through ports and functional wires, probes, settings |
| Session | `core/session/` | the command path with virtual timestamps and the input log, probes with ring buffers, snapshots and topology, pacing slices, notifications, scenarios with assertions and reports, the JSON-RPC method table |
| Host and protocol | `core/host/`, `core/protocol/` | the only OS code (files, clock, sockets, stdio, interrupts), the stdio sidecar and the loopback WebSocket server with the built-in console page |
| CLI | `cli/` | `run`, `validate`, `serve`, `sidecar`, `catalog`, `board`, `models`, `version` |

## What does not exist yet

The MCU runtime (every board's MCU is a passive placeholder and loading one
raises `mcu.no_runtime`), toolchain profiles, replay, Lua models, composite
and native models in the loader, topology transactions, binary waveform
frames (samples are sent as JSON arrays with `encoding: "json"`), the
Debug and Verify perspectives.

## Tests

`ctest` in a build tree runs the C unit tests (`tests/unit/`), the Python
tools' tests, the documentation and policy checks, and, when the examples
exist, the protocol conformance suite over both transports and the headless
examples. Recorded results live in STATUS.md with the revision they were
observed at.
