# Native linear AC sweeps

The native CLI/RPC and workbench solve linear sinusoidal circuit response across
frequency. This is a phasor MNA solve of initialized native linear models, distinct
from analysis of captured FFT bins. Nonlinear devices are rejected rather than
assigned an invented small-signal response.

## Engineering workflow and external behavior

On a real bench, engineers drive a network with a sinusoidal source, account for
its source resistance and the instrument's input impedance, then compare output
amplitude and phase across frequency. AC analysis expresses that same linear
steady-state question using complex voltages. The selected internal voltage or
current source has a real unit phasor; other independent source perturbations are
zero. Their impedances remain in the circuit. Source DC/transient waveforms and
capacitor/inductor initial conditions do not define the AC perturbation.

**AC sweep** is available beside transient capture in the schematic workspace.
Choose an independent source, a common reference net/pin and up to eight output
net/pin voltages. Set a logarithmic or linear frequency grid, run the sweep, choose
an output and use the exact-point frequency cursor. Both Bode plots use a log
frequency axis even for linearly spaced samples. Phase is wrapped; jumps greater
than 180° and zero responses break the drawn line. Plot lines interpolate for
review only; exported points are never interpolated or cropped.

The source/project is isolated and reset on a temporary native engine. Analysis
does not advance, reset, reload or change the live circuit, runtime inputs, clock,
probes or recorded history. A paused electrical-policy board-free circuit is
required, with no open editing transaction. UI preparation and results verify
native manifests against the prepared project and refuse stale responses.
Changing options marks the reviewed result; its exports retain the old settings
until recomputed. No demo or browser substitute fabricates AC results.

Voltage-source response is voltage gain in V/V relative to the internal 1 V
source; current-source response is transimpedance in ohms relative to the internal
1 A source. It is not normalized to a separately measured loaded input. Current
source polarity is + to − inside the source, so current drawn from a passive
node to ground produces a negative node response. Every output subtracts the
chosen reference voltage before magnitude/phase conversion.

## Model and numerical contract

Supported native elements:

- Resistors use their initialized conductance, including native fixed-temperature
  corrections and clamps.
- Capacitors use `Z = ESR + 1/(jωC)`; inductors use `Z = DCR + jωL`.
- Independent voltage sources retain series output resistance; independent
  current sources use native terminal polarity.
- Wired instrument inputs use their actual parallel `R || C`, including the
  voltmeter, ammeter burden equivalent and scope input when they use that model.
- Ground markers, structural composite shells and passive unpopulated footprints
  do not add AC excitation. Native child devices retain their own equations.

The solver reads initialized model state through an explicit native accessor;
it does not duplicate parameter binding in the UI. The internal abstraction is
an equivalent real block matrix `[G -B; B G]`, avoiding a C99 complex dependency
on MSVC/TinyCC. Voltage sources and inductors get branch-current equations.
No transient approximation, numerical differentiation, artificial shunt or FFT
is used. The temporary DC initialization can use normal native convergence aids,
but the AC equations do not inherit DC gmin regularization.

Dense row-equilibrated partial-pivot elimination rejects normalized pivots at or
below `1e-14` and checks a `1e-10` normwise backward-error bound on unscaled rows.
These are numerical refusal criteria, not an accuracy specification or condition
number estimate. Nonfinite/unrepresentable coefficients or results, singular or
floating circuits and excessive workloads fail the complete request: no partial
successful sweep is exported.

For complex differential response `H`, magnitude is `hypot(real, imag)`, dB is
`20 log10(abs(H))` referenced to 1 V/V or 1 ohm, and phase is
`atan2(imag, real)` in degrees. Exact zero has zero magnitude and null dB/phase
(displayed as −∞/undefined); no numerical noise floor is invented.

Limits: 1–256 strictly ascending numeric frequencies in 1 mHz–1 THz; 1–8 unique
output targets; 256 compiled components, 1024 nets and 128 complex unknowns per
island. The dense work budget requires `frequency_count × Σ(2 × unknowns)³` at
most 268435456; reduce points or circuit size if exceeded. UI single-point grids
require equal endpoints. Runtime is synchronous without mid-call cancellation.
Frequency bounds are software limits, not qualified component bandwidth.

Nonlinear junctions/op-amps/regulators, switches and interactive time-varying
models, digital bridges (except a zero-reference ground marker), scripted models,
MCUs/firmware, dependent sources, noise, distortion, frequency-dependent loss,
small-signal bias linearization, phase unwrapping/group delay, automatic pole or
bandwidth fitting and hardware/manufacturer qualification are not implemented.
Unsupported native components and bridges cause explicit failure.

## Native protocol and evidence

```sh
embsim ac examples/ac-rlc/project.json \
  --request examples/ac-rlc/response.ac.json > /tmp/rlc-ac.json
python3 tools/validate.py /tmp/rlc-ac.json --strict
```

CLI returns JSON and 0 on success, 2 on invalid request/load/solve; it never
modifies inputs. Capability/method `analysis.ac` accepts `source`, `reference`,
`frequencies_hz` and `probes`, with optional version/kind (`ac_sweep_request`).
Unknown fields and unsupported versions fail. `ac_sweep` results declare
`method: linear_phasor_mna`, `excitation: unit_real_source_phasor`, units, exact
request, ordered complex responses and the native source manifest. Schemas are
[request](../../schemas/ac-sweep-request.schema.json) and
[result](../../schemas/ac-sweep.schema.json).

CSV retains every frequency/channel, source/reference, units, complex values,
magnitude/dB/phase and the manifest hash. JSON embeds complete native source
identities. Both offline validator backends check schema, source/model identities,
request ordering, units and magnitude/logarithm/phase consistency. The UI verifies
hashes, exact requested grid/targets and each derived field. Those checks establish
content lineage and internal consistency, not authentication or independent proof
of arbitrary edited complex responses. Physical correctness has separate tests.

## Independent evidence

`tests/protocol/test_ac.py` compares complex response against analytic RC,
differential output, physically loaded R∥C, ESR/DCR/Rout series RLC and current
source equations. It checks other source perturbations are zeroed, resonance,
repeatability, hierarchical sibling independence with physical meter loading,
CLI/RPC parity, unchanged live state, invalid inputs, unsupported
and floating circuits, 256 frequency points, 128 complex unknowns and budget
refusal. Both validator backends reject corrupted hashes, JSON, units, grids,
channels and derived measurements.

`tests/analog/ac-linear-reference.json` holds 123 independently generated ngspice
44.2 complex points for RC, loaded RC and resonant series RLC. They are compared
by native acceptance, never used as runtime answers. Regenerate/check with
`python3 tools/analog/ac_reference.py --ngspice PATH [--check]`. ngspice is an
external GPL referee only; no ngspice code/library is included or linked.
At the RLC resonance, the native 10 Ω / 10 mH / 1 µF fixture gives 10 V/V and −90°.
The native browser fixture `tests/browser/test_ac_review.py` verifies real RC
Bode review, exact cursor values, complete exports, zero/single-point behavior,
settings changes, invalid-grid refusal and an unchanged 1 µs bench.

Reference: [ngspice AC analysis manual](https://nmg.gitlab.io/ngspice-manual/analysesandoutputcontrol_batchmode/analyses/ac_small-signalacanalysis.html).
The general AC workflow linearizes nonlinear devices around a DC operating point;
this milestone deliberately admits only linear native networks, whose coefficients
are independent of bias. Existing model fidelity limits continue to apply.
