# Configure physically loaded instruments

Run's wired-instrument cards now configure their actual input, shunt or output
impedance and terminal connections through the canonical project transaction.
They can add signed voltage/current traces at a selected sample rate and list
qualified project-hierarchy instances with native resolved block parameters.

## Physical meaning and editing rules

- Voltmeter input resistance, ammeter shunt resistance, scope parallel R/C and
  generator output resistance remain existing native component parameters. These
  controls change the circuit solve, not a display multiplier.
- The lead selectors name local nets on the instrument's sheet. Both leads may be
  explicitly disconnected; disconnecting an exported port target requires first
  remapping that interface. There is no fabricated ground or earth connection.
- Scope presets are **effective loading at the circuit terminals**: representative
  1× (1 MΩ ∥ 100 pF), 10× (10 MΩ ∥ 12 pF) and 50 Ω termination (C = 0). These
  are configurable equivalents, not named probes or qualified compensation,
  attenuation error, bandwidth, common-mode limits or calibration. Traces report
  native terminal voltage in volts; selecting 10× does not divide that voltage.
- Both voltage and current traces are available. For an ammeter the default trace
  is current and the alternate is burden voltage. Other meters default to signed
  differential voltage and offer current as the alternate. Generator current is
  signed into its output, matching its existing native declaration.
- Trace sampling accepts integer rates from 1 Hz to 10 MHz. More sampling does
  not establish instrument bandwidth or remove existing buffer/delivery bounds.
  Adding an observation trace adds no second electrical load.
- Numeric SI inputs reject invalid/nonpositive resistance and negative capacitance.
  Native preflight checks component parameter ranges. Unsupported/stale settings
  fail without replacing the active circuit. An unchanged configuration preserves
  the existing document/time instead of materializing equivalent default values.

**Applying an actual impedance or lead change restarts the simulation and clears
live/frozen acquisition**, as stated beside Apply. Export completed evidence
before changing physical topology. This reuses the existing transactional rebuild;
it is not uninterrupted firmware hot-plugging. Moving an ideal observation probe
remains a different operation, with existing source epochs and no circuit load.

## Hierarchy

The cards traverse concrete project sheets, including repeated block instances.
Readouts and trace targets use qualified native identities (`A.METER.voltage`,
not `METER.voltage`), while loading values use the native instance parameter map.
An instrument bound with `$name` exposes its resolved value read-only; edit the
parent block instance parameter to change it without destroying the binding.

Lead and unbound-parameter edits inside a block edit its shared definition. The
configuration form states the definition and number of affected instances.
**Open instrument sheet** navigates to that concrete sheet. Installed catalog
composite internals are not expanded into separate project instrument cards.

## Loaded acquisition acceptance

The existing native instrument-loading circuit demonstrates independent equations:

- A 10 V source through 1 MΩ with a 1 MΩ voltmeter reads 5 V. Moving its negative
  terminal to a separate 5 V source makes its positive node 7.5 V and its actual
  differential reading 2.5 V.
- A 5 V / 100 Ω load with a 100 Ω ammeter shunt carries 25 mA.
- A 2 V open-circuit generator with 100 Ω output resistance into 50 Ω produces
  2/3 V, rather than changing only its displayed amplitude.
- A scope's 10 MΩ ∥ 12 pF preset through 1 MΩ has final value 100/11 V and
  τ = (1 MΩ ∥ 10 MΩ) × 12 pF = 10.909 µs. A 4 V sampled trigger freezes an
  actual native device-output capture; CSV near 21 µs matches the independent
  loaded RC equation. Continued simulation leaves the completed CSV unchanged.
- Changing to 1 MΩ ∥ 100 pF gives final value 5 V and τ = 50 µs. Terminating
  with 50 Ω instead gives 10 × 50/(1,000,000 + 50) V.

A second [hierarchical circuit](../../examples/hierarchical-instruments/README.md)
checks two shared measurement stages with independent native parameter overrides:
1 MΩ and 10 MΩ meter inputs produce 5 V and 100/11 V. Browser acceptance verifies
both readouts, protects the bindings, creates a qualified current trace and opens
the correct instrument sheet. Native scenario assertions check the same circuit.

```sh
python tests/browser/test_instrument_controls.py \
  --embsim build/ui-layout-native/bin/embsim --chromium /usr/bin/chromium \
  --screenshots /tmp/embsim-instrument-controls
```

These tests establish generic linear loading, native acquisition integration and
editing behavior. They do not establish commercial instrument accuracy/protection,
advanced trigger modes, protocol-analyzer electrical models, arbitrary-frequency
response or uninterrupted firmware topology changes. See the
[underlying equivalent models and limits](instrument-loading.md).
