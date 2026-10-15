# Product specification — draft 0.1

## Purpose

Provide a reproducible virtual workbench for firmware developers, circuit
engineers, educators, and automated verification. GUI and CLI must operate on
the same saved project and produce the same simulated outcomes for the same
ordered inputs. This document is the initial design baseline, open to revision.

## Required workflows

1. Select a supported board and load an unmodified firmware image.
2. Connect external components to named board connector pins in a schematic.
3. Run, pause, reset, step instructions or events, and inspect state.
4. Press and release switches, turn potentiometers, inject bus traffic, and
   change supported connections while firmware is running.
5. Observe LEDs, displays, serial output, waveforms, registers, and faults.
6. Save the complete experiment, run it without the GUI in CI, assert results,
   and replay all external inputs deterministically.
7. Add a board or component with documented schemas, model contracts, and tests.

Live topology changes must pause at a simulation boundary, validate the proposed
change, and either apply it atomically or retain the previous topology. Physical
hot-plug effects require suitable electrical models; editing a wire alone does
not reproduce them.

## Fidelity contract

Every model declares a version, provenance, supported features, known omissions,
validation references, and separate accuracy claims for functionality, timing,
and electrical behavior. Unsupported accesses must be observable; strict mode
must fail instead of silently treating missing peripherals as working hardware.

Digital functional models represent logic and connectivity. Timed models add
documented delays and clock behavior. Electrical models represent voltage,
current, impedance, nonlinear devices, supply dependencies, and applicable
temperature behavior. Accuracy claims include their tested operating range.
No tier automatically inherits physical accuracy from another.

## Component coverage strategy

Build a tested catalog incrementally: wires and power rails; switches and pulls;
resistors, capacitors, inductors, diodes and LEDs; BJTs and MOSFETs; logic ICs;
memories, displays, sensors and actuators; communication transceivers and power
devices. Separate generic physics/behavior from a manufacturer's part parameters,
package pins, schematic symbols, and graphical artwork. A red LED's appearance
must not supply its current/voltage model.

Separate CPU ISA, MCU die/peripherals, package pin mapping, development board
wiring, and user circuit. Two boards using one MCU must reuse its model without
duplicating peripheral behavior. Board revisions and fitted component variants
must be explicit.

## Reliability requirements

Linux, macOS and Windows are required targets. Platform validation must run in
CI. A polished, responsive and accessible workbench is a core deliverable;
see [platform support](PLATFORMS.md) and [interface direction](INTERFACE.md).

- Integer virtual timestamps and a documented order for simultaneous events.
- Bounded resource use and diagnostics for nonconvergence and event storms.
- No simulation dependence on rendering rate or wall-clock speed.
- Reproducibility records include firmware, model, parameter and project hashes,
  input timestamps, ordering, engine version, and relevant solver settings.
- Clear diagnostics for contention, floating inputs, unsupported behavior,
  voltage violations, and invalid configurations where the model supports them.
- Project files carry schema versions; upgrades preserve the original input.
- Native extensions are trusted code. Future untrusted extensions require
  isolation and resource limits before they can execute safely.
- Supported operating systems and performance claims must be established by CI
  and representative benchmarks rather than inferred from language portability.

## First acceptance scenario

A supported MCU runs firmware that reads an external switch and drives an LED
through a resistor and transistor. During execution, GUI interaction changes the
switch; the firmware sees it through real GPIO register behavior. CLI scenarios
reproduce the same events and observations. Waveforms show both firmware and
electrical behavior. This is the target integration scenario; the initial
digital button/indicator prototype covers only its connectivity/control layer.

## Boundaries

RF, EMC, PCB parasitics, thermal distribution, manufacturing variation, and
destructive hardware behavior are separate future modeling domains. A circuit
is only validated for the modeled effects and operating conditions. Broad
catalog coverage will follow verified implementations, not placeholder claims.
