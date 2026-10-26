# Live schematic workbench

The schematic is the primary simulation surface. 3D development is deferred;
working with a board's terminals, controls, peripheral displays and measurements
must not require loading its 3D renderer. This implementation is the first live
schematic milestone against the [Proteus survey](../research/proteus-schematic-survey.md),
not completion of every surveyed capability or a 1.0 release.
Full completion is tracked in [issue #64](https://github.com/EmbLinkOs/EmbSIM/issues/64).

## Implemented behavior

**Design** has separate Select, Operate and Probe tools. **Run** opens a large
live schematic with controls, wired instruments and waveforms alongside it.
Operate exposes on-symbol controls; it does not drag parts or start wires.
Inspect retains terminal/net selection. Probe supports pointer and keyboard
selection of connected wires and pins. Optional 3D assets stay deferred.

Catalog `symbol.states` bind native outputs to safe SVG presentation attributes,
text or classes using the same mapping contract as artwork. State application
restores the original symbol when observations disappear. Styles and referenced
paint-server IDs are scoped to each instance. Two copies of a display can show
different values without restyling one another. Executable attributes and external
paint URLs are not supported state targets. The schema/semantic validator checks
output declarations, SVG selectors and supported attributes.

The live symbol batch covers generic/named single and RGB LEDs, bargraphs,
seven-segment displays, HD44780-compatible 16×2/20×4 LCDs, SSD1306 OLEDs,
MAX7219 and TM1637 modules. Text, segments and pixels come from their models;
they are not values fabricated by the renderer. Existing peripheral-model limits
still apply, including functional display models that do not solve individual
segment junction currents. Floating/unknown common leads no longer light the
functional seven-segment/bargraph models.

Two new **generic topology** parts use existing LED children:

- `emblink/bicolor_led`: antiparallel red/green dies, terminals 1/2. Reversing
  polarity selects the conducting die. It is not a manufacturer package claim.
- `emblink/led_matrix_8x8`: 64 independent junctions, row anodes R1–R8 and column
  cathodes C1–C8. Current limiting is external. Row/column wiring and loading
  determine illumination; the renderer does not inject a bitmap pattern.

Their electrical policy uses static junction models, while functional/timed
policy uses ideal known-level indication. Optical calibration, thermal effects,
damage, reverse breakdown and junction charge are not qualified.

`symbol.hotspots` expose declared runtime inputs: momentary buttons, latching
switches, bounded potentiometer movement and DS18B20 ambient stimulus. Pointer
capture isolates gestures from editing. Hold releases on pointer release/cancel,
focus loss, hidden document, disabled control and unmount. Arrow keys, Shift,
Home/End support numeric controls; runtime limits take precedence over artwork.
Drag coordinates account for zoom, rotation and mirroring. Native input acceptance
still decides what a control means: changing DS18B20 ambient temperature does
not skip its conversion or change its power-on temperature register immediately.
Other declared inputs remain available in the Controls panel.

Board module headers expose supported native member indicators and buttons
without moving any terminal anchors. Opening an internal board/composite sheet
also binds its child outputs and inputs to their original native instance paths.
This is inspection/operation of catalog internals, not arbitrary editable project
hierarchy.

## PWM exposure

Native electrical LED/RGB observations add cumulative `emission_integral` or
`red/green/blue_emission_integral`, measured in seconds of **illustrative normalized
emission**. Only accepted electrical steps update the history. Observing it is
read-only, reset clears it, and quiet held states extend to the current simulation
clock without advancing the engine.

Between two complete observations, the schematic computes
`mean = (integral_now - integral_previous) / elapsed_simulation_seconds`, bounded
to 0–1. RGB mixing uses the three measured means. **Interval average** states the
actual interval in its selector; **Instant** uses the native instantaneous values.
A selectable 1×/4×/16× illustrative display gain makes small emitters legible;
default 4× scales only rendered luminance, keeps zero dark and preserves RGB
channel ratios. It is not a physical LED parameter or measurement correction.
A valid interval requires two complete observations; startup/reconnect waits for
a baseline, and policy changes clear prior electrical exposure. Partial notifications
do not treat cached integrals as newly measured zero light.
The renderer does not replace electrical current, voltage, raw observations or
recorded measurements. This is an observation-interval exposure, not a fixed
human-vision window, calibrated radiometry, gamma-correct photometry or qualified
switching transients. Functional-only indicator models without integrals retain
instantaneous behavior.

## Differential voltage and label tracing

Run's Voltage comparison selects A/B by named-net dropdowns, on-sheet picks or
movable markers. Marker movement preserves the selected wire/pin drop point,
including movement within a named net with remote branches. It reports the signed difference of two finite solved voltages
from the same session observation. Logic-only/missing/nonfinite values show an
unavailable reading; a logic 1 is never assumed to mean 3.3 V. Escape, focus loss and invalid/canceled
marker drops preserve the prior connection. The two leads add no electrical load
and do not modify the project. Wired voltmeters/scope inputs remain the way to
include resistance/capacitance/burden in the solve.

Repeated net labels complete visual connectivity only when each label is within
one editing grid unit of its own net's drawn wire or pin. A lone or remote label
cannot suppress the airwire of an unlabelled cluster. Label rotation is honored
by the scene. The netlist remains the electrical source of truth.

## Acceptance and compatibility

- [Live circuit](../../examples/live-schematic/README.md): CLI checks real contact
  transitions, loaded wiper voltage and LED/matrix conduction.
- `tests/unit/test_analog.c`: 1 kHz/25% emission exposure, read-only observation,
  reset and existing analog/instrument regressions.
- Catalog fixtures: bicolor polarity/open return, matrix selected/neighbor/open
  paths and seven-segment/bargraph floating-common behavior.
- Scene/store tests: instance isolation, stale-state clearing, safe attributes,
  hold release, exposure arithmetic/RGB mixing, board anchors, voltage polarity,
  missing voltages and attached-label connectivity.
- `tests/browser/test_live_schematic.py`: actual native light/control/display
  outputs, measurement picking/dragging, compact viewport bounds and no 3D load.

LCD/OLED/MAX7219/TM1637 symbols are version 1.1.0 with enlarged readable display
areas. Named electrical terminals keep their identity; some right/bottom drawing
anchors moved. Older drawings can require wire repair/rerouting in Design.
A layout change is not an electrical reconnection. No automatic project-format
migration or preserved runtime-state serialization is claimed.

## Remaining gates for a complete schematic release

These are release requirements, not disabled controls advertised as implemented.
Each gate needs CLI/native acceptance where electrical behavior is involved,
browser/keyboard acceptance, saved-project compatibility, documentation and
explicit model limits.

| Area | Required next acceptance gate |
|---|---|
| Editing/wiring | Exercise modeless wiring, manual corners/junctions, move repair, repeat wiring, block copy/annotation/templates and grid preferences together on saved/reopened industrial-sized sheets. Qualify route performance and editing recovery, not just feature presence. |
| Large designs | Basic editable hierarchy, reusable parameterized blocks and native cross-sheet tracing are implemented in [editable hierarchy](editable-hierarchy.md). Remaining: explicit global-net and sheet-port objects, nested board firmware, bus-entry migration, managed block libraries and industrial-size editing/performance qualification. |
| Component qualification | Expand common IC/device coverage with source revisions, independent electrical/timing holdouts, loading, supply/reset, unsupported-range warnings and shared CLI/GUI conformance. A large searchable catalog alone does not imply real-world qualification. |
| Displays/interaction | Electrically loaded multiplexed seven-segment/bargraph/driver models; supply, contrast, scan timing, ghosting and open/short cases for displays; sensor stimulus/conversion coverage; interrupted control gestures in every supported orientation. |
| Measurement/instruments | Basic sampled Single/Normal acquisition and movable waveform probes are implemented in [acquisition](schematic-acquisition.md). Remaining: advanced trigger modes and qualified acquisition; movable **physical scope** probes with explicit capture discontinuities; differential acquisition, current direction based on solved signed branch currents, bandwidth/range/accuracy/loading models, counter/analyzer/terminal/generator integration. The ideal A/B readout is not a physical system multimeter or scope acquisition system. |
| Firmware debugging | Source/assembly stepping and breakpoints across supported cores, deterministic hardware-condition breakpoints, watches/traces, GDB integration and an explicit per-core/peripheral support matrix. Existing debugging facilities need integrated schematic acceptance. |
| Analysis | Native DC operating-point/sweeps, transient refinement, linearized AC, Fourier, noise/distortion and reference-conformance runs with independent analytic/reference circuits. Never manufacture spectra or extrapolate unsupported analyses from a screenshot. |
| Engineering workflow | Managed/versioned library dependencies, custom-part authoring with model qualification, assembly variants, reproducible BOM/report generation and PCB/schematic cross-probing with stable identity. Import/export support alone does not satisfy the whole workflow. |
| Product quality | Cross-platform builds and keyboard/accessibility, high-DPI/light/dark visual review, large-project frame/memory budgets, autosave/recovery and deterministic undo without changing circuit meaning. |

The [83-entry survey](../research/proteus-schematic-survey.md) remains the source-backed
comparison baseline. Passing one milestone must not silently mark other entries
complete. 3D is outside these release gates.
