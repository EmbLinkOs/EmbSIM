# Architecture — draft 0.1

## Layers

```text
Browser GUI / CLI / automation clients
                |
        session commands and observations
                |
    project + model runtime (Python initially)
       |                |                |
 digital C kernel   MCU adapters     analog coordinator
       |                |                |
 events + nets     ISA / MMIO       electrical solvers
```

The C99 kernel owns digital event ordering, time and driver resolution. The
initial Python runtime validates JSON, instantiates supported components, and
exposes CLI/HTTP controls. Python is orchestration, not the future instruction
execution loop. The GUI is a client; it contains no simulation equations.

## Time and causality

Virtual time is unsigned integer nanoseconds in the first kernel. Events order
by `(timestamp, insertion sequence)`. A drive at the same timestamp as another
drive follows that order; observations occur after processing events through the
requested timestamp. Advancing backwards is an error. The initial kernel has
fixed documented capacities and rejects overflow before modifying state.

Before integrating CPU clocks, extend time handling with rational clock phase
accumulators or a finer documented time base so fractional-nanosecond periods
do not drift. CPU adapters must stop at external event boundaries. An execution
quantum cannot cross an observable input edge without an explicit accuracy
contract. Zero-delay propagation will require delta cycles, convergence checks,
and event-storm limits when reactive component models are introduced.

## Connectivity

A net joins any number of named component pins. Each output has an independent
driver. The digital states are `0`, `1`, `Z` (undriven), and `X` (unknown or
conflicting). Opposing active drivers resolve to `X`; released drivers do not
override others. The prototype's LED is an ideal logic indicator, not a diode:
it reports lit only on `1`, with its observed logic also exposed.

Future pin models distinguish drive strength, open drain, pulls, input thresholds,
output impedance, analog terminals, alternate functions and supply domains.
Cross-domain bridges convert voltage to sampled logic and digital output to an
electrical driver using the selected model parameters. A boolean pin callback
cannot substitute for this contract.

## Firmware and board composition

Reuse EmbCC CPU interpreters and selected peripherals behind a backend API for
load/reset/run-to-boundary/step/registers/memory/interrupts. First audit global
state, allocation/teardown, per-instance ownership, error paths that exit the
process, and instruction-cost assumptions. Multi-board and multicore simulation
must not share hidden mutable CPU state.

MCU models own clocks, register behavior, interrupts and muxes. Package models map
die signals to physical pins. Board descriptions instantiate package, components,
rails and connector maps. User projects instantiate boards and external circuits.
SVD/ATDF/EDC imports provide register metadata; separately tested behavioral
models provide side effects. Firmware remains independent of the GUI and does
not require source rewriting.

## Analog and mixed signal

Evaluate an established SPICE solver through an adapter before writing a new
nonlinear solver. The integration must define event breakpoints, timestep
acceptance, threshold crossings, rollback/retry, convergence failures, and repeatability
across supported solver versions. CPU and analog time must commit together;
uncoordinated independent loops are insufficient. Solver choice remains an ADR
pending dependency/license review and a timed GPIO/RC/ADC experiment.

## Control, observations and storage

All external changes enter the session command path and acquire virtual time
and ordering. GUI transport and wall-clock pacing cannot directly mutate models.
Initial HTTP API: `GET /api/state`, `POST /api/control` for set/advance/run/pause/reset.
The development server binds loopback and serves only bundled UI assets.

Future API versions add waveform subscriptions, probes, breakpoints, topology
transactions, and complete input recording. Record files must distinguish
reproducible external events from derived observations. Checkpoints need explicit
versioned state serialization across CPU, device, scheduler and solver models;
they cannot be implemented by retaining GUI state.

## Extensions

Version schemas separately from native ABI. Declarative descriptions can compose
supported models without executing user code. New behavior initially requires
reviewed C/Python code and tests. Define a sandboxed extension ABI only after
profiling and model lifecycle requirements are established; native shared-library
loading is not an adequate security boundary.

## Current implementation limits

Single digital session; no firmware backend, analog solver, reactive logic gates,
schematic editor, dynamic rewiring, persistent replay format, or checkpointing.
HTTP traces are in-memory diagnostic input records. Live runs use a fixed virtual
quantum for presentation pacing; they make no real-time performance guarantee.
