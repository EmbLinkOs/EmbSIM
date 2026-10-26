# Wired instruments and electrical feedback

Instrument terminals participate in the electrical solve. A trace is still an
ideal observation; inserting a load requires wiring a catalog component. Hiding
or adding a plot never changes the netlist. These are generic equivalent circuits
with configurable values, not full commercial instrument qualification.

## Electrical contract

All four components require **electrical policy**. Both + and − leads must be
wired for a complete measurement. During placement and incremental wiring,
detached instrument/source leads keep their terminal slots and private floating
nodes; they never become an implicit ground return. Solver gmin regularizes
these nodes, so tiny numerical leakage can remain. Voltage is V(+) − V(−); current is positive entering +. The negative lead
is an explicit circuit connection, not an implicit earth reference.

| Catalog component | Equivalent circuit and default | Observations |
|---|---|---|
| `emblink/voltmeter` | 10 MΩ across the leads, configurable `input_resistance` | `voltage` (V), `current` (A, loading current) |
| `emblink/ammeter` | 1 Ω series insertion/shunt resistance, configurable `shunt_resistance` | `current` (A), `voltage` (V, burden) |
| `emblink/scope_input` | 1 MΩ ∥ 15 pF, configurable `input_resistance` and total effective `input_capacitance` | `voltage` (V), `current` (A, resistive plus displacement current) |
| `emblink/function_generator` | Thevenin source with 50 Ω series output, configurable `output_resistance` | `value` (V, open-circuit waveform), `voltage` (V, loaded terminals), `i` (A, entering +) |

The shared `instrument_input` primitive stamps a finite resistance and optional
parallel capacitance. Capacitance uses the existing accepted/rejected-step
history and LTE-controlled integration; DC initializes it from the operating
point. Zero capacitance is allowed. Resistance must be finite and positive;
capacitance must be finite and nonnegative. The source primitive's optional
`rout` uses its existing MNA branch equation with series resistance. Ordinary
`emblink/vsource` retains zero output impedance and its existing behavior.

Generator dc/pulse/sine/pwl values are **open-circuit voltages**. A 2 V peak source
with 50 Ω output gives a 1 V peak across a matched 50 Ω termination. There is no
implicit amplitude doubling when changing the load. A scope can model a resistive
50 Ω termination by setting input resistance to 50 Ω and selecting the desired
input capacitance. A probe's attenuation/compensation network is not automatically
synthesized: enter the effective input R/C explicitly or wire its circuit.

## Visual behavior

Run's **Wired instruments** panel shows each component's loading values, lead
nets and engine voltage/current readings. **View leads** selects the part in the
live schematic; **Add trace** observes its voltage (current for an ammeter).
Missing leads disable tracing from this panel and suppress stale readouts.

Selecting a wire highlights its entire named net, including terminal rings,
junctions and net labels. Selecting a part, focusing/hovering a pin or highlighting
a net exposes pin names without rotating the text with the symbol. Observed logic
levels appear beside those names. Pausing retains live Design observations.

Unconnected declared digital inputs without a declared pull produce a schematic
warning. Digital I/O pins are not statically assumed to be inputs. Live warnings
use active receiver identities from the engine, including pins inside boards:

- **Floating input:** an active digital receiver sees Z.
- **Unknown input:** an active receiver sees X; this alone does not prove conflict.
- **Conflicting drivers:** known active 0/1 drives disagree. Digital nets consider
  the strongest drive strength; analog islands compare active digital output
  intents and retain the solved voltage. Released/open-drain-high outputs do not
  count as active drives. Analog supply-input bridges are excluded from this check.

Warnings can locate and highlight their net without adding probes or rewiring it.
They describe the current observation; transient problems between observation
frames remain in the engine diagnostic/waveform evidence where available.
`observe.snapshot`, `observe.update` and topology net state include `contention`
and `receivers`; mixed nets with digital receivers also expose their sampled
`logic`. Receivers with different thresholds can disagree, producing aggregate X.

A zero-valued analog voltage does **not** prove a floating node. This check does
not diagnose all possible analog shorts, output overcurrent, power-pin conflicts,
voltage-domain incompatibility, supply loss or missing analog return paths.

## Acceptance and source basis

The [loading bench](../../examples/instrument-loading/README.md) executes 13
assertions against independent divider, Ohm's-law and finite-ramp RC targets.
Twelve component fixtures cover resistance changes, reversed/differential leads,
ammeter burden, matched/high-impedance generator loading, sine amplitude and
scope transient behavior. Native tests check current KCL, reset history, refined
RC convergence and conflict clearing when a driver releases. Native protocol
checks exercise functional and mixed electrical observations. UI tests cover
receiver metadata, X/Z/conflict distinctions, pin labels, net highlighting and
wired-instrument actions.

[Tektronix's ABCs of Probes Primer](https://www.tek.com/en/documents/whitepaper/abcs-probes-primer)
explains resistive/capacitive loading and equivalent input circuits. Its linked
PDF, `ABCs-of-Probes-Primer-60W-6053-17.pdf`, was consulted on 2026-10-26.
Defaults here are representative configurable engineering values; the analytic
acceptance equations do not claim a particular manufacturer's specified accuracy.

## Remaining limits

No ADC quantization, noise, calibration, range switching, bandwidth transfer
function, overload clipping, current limiting, fuse/protection behavior, thermal
or damage model is provided. The scope equivalent has two differential terminals;
bench-scope protective earth/chassis coupling must be wired separately. Lead and
cable inductance, probe compensation and distributed transmission lines require
explicit additional components. A real ammeter connected across a supply may
blow a fuse; this model computes the shunt load and does not simulate that fuse.

## Reproduce the checks

```sh
python3 tools/catalog/run_fixtures.py --embsim build/dev/bin/embsim \
  --component voltmeter --component ammeter --component scope_input \
  --component function_generator
python3 tests/protocol/test_electrical_feedback.py --embsim build/dev/bin/embsim
python3 tests/browser/test_instrument_loading.py --embsim build/dev/bin/embsim \
  --chromium /usr/bin/chromium
```

Build the workbench first for the browser workflow. It verifies signed readings,
trace creation without project edits, full-net highlighting, centered lead
navigation, contention clearing and floating receiver feedback against the native
backend, including disconnecting one meter lead and restoring it with undo.
Projects with no wired instruments initially fold that panel to its rail.
[Native captures](../gallery/README.md#wired-instrument-loading-and-electrical-feedback)
record that workflow. **Fit captured waveforms** selects the available capture
interval; analog/digital plots and analog cursor readings stop at the lesser of
the engine clock and producer capture frontier, including empty constant-signal
watermarks. Renderer tests exercise a partial producer delivery ahead of its
last sample, rather than assuming the engine clock proves complete delivery.
