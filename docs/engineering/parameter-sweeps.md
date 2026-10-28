# Native parameter sweeps

This tool answers questions such as “how much does my meter load this divider?”
or “how does changing R affect capacitor charging at 100 µs?” by running separate
native circuits. It does **independent reset-and-sample analysis**, not a DC
operating-point/convergence solver. A requested sample time does not establish
settling, accuracy, stability or device conformance.

## Workflow

In the schematic, open **Parameter sweep**. Choose a concrete fitted component
and declared numeric parameter. The form proposes a range around its resolved
value, bounded by catalog limits. Choose endpoints, 2–64 linearly or
logarithmically spaced points and a time after reset. Positive endpoints are
required for logarithmic spacing; descending ranges are supported. The plot uses
linear axes regardless of sample spacing and connects only available samples.
Its lines are visual guides, not additional simulated points.

Add terminals from the selected component or type comma/newline-separated
`net:NAME` and `pin:INSTANCE.PIN` targets. Fully qualified sheet references work,
for example `pin:A.R.2`. Up to eight unique measurement strings are accepted.
Run while the source is paused. Each point loads an independent circuit, resets
at zero and advances to the specified time. Connected instrument resistance,
capacitance, burden and source impedance remain part of that circuit's solve.
The analysis's net/pin reads are ideal observations and add no instrument load.
Runtime control changes, scheduled inputs and host connections are not replayed.
The source project's configuration, clock, probes and outputs are not replaced.

The plot selects a measured voltage; the table retains exact point ordering,
resolved values, actual sample times, logic states and diagnostics. An X/Z logic
state on an analog island does not erase its numeric analog solution. A purely
digital/floating target without a numeric analog solution has null voltage, never
an invented zero. A failed electrical point has null resolved/voltage results and
explicit diagnostics; usable neighboring points still run. Failed/unavailable
points break the plotted line rather than being interpolated across.

CSV retains empty missing values and separate logic columns. JSON evidence
includes the request, every point and the exact native engineering manifest of
the captured source, including loaded model descriptions and SHA-256 identities.
The UI verifies that source and refuses a result from different native project
bytes than those reviewed. Changing the schematic marks the captured inputs stale;
refresh before another run. Existing exports continue to describe their captured
source, rather than claiming to be current.

## CLI and protocol

```sh
embsim sweep examples/assembly-variants/project.json \
  --request examples/assembly-variants/loading.sweep.json > /tmp/loading.sweep-results.json
python tools/validate.py /tmp/loading.sweep-results.json --strict
```

The CLI emits JSON, leaves input files unchanged and returns 0 when every point
samples successfully, 1 for an inspectable result containing failed points and 2
for usage, unsupported context or request errors. `--library DIR` adds an existing
library. It does not download models or bundle relative inputs.

RPC `analysis.sweep` takes `target`, `parameter`, `values`, `sample_at` and `probes`.
The optional metadata is `schema_version: 1`, `kind: parameter_sweep_request`.
Explicit values accept finite numbers or native SI strings. The method advertises
capability `analysis.sweep`; demo transport refuses to fabricate results.
The graphical range generator is convenience only; native parsing/loading remains
authoritative. Board-free electrical circuits only: pause first, finish/abort any
project transaction, and choose a fitted project-owned catalog part. Composite
package internals and board internals are not editable sweep targets. Independent
project sheet instances reuse the assembly materializer, preserving sibling values,
ports and wires. A derived assembly may be analyzed without changing its retained
origin; an unfitted part cannot be the target.

Limits are 1–64 explicit values, eight targets, 0–1 ms after reset per point,
10 ms aggregate simulated time, 256 compiled components and 1,024 nets. Runs are
synchronous, bounded and not cancellable mid-call. They may still take wall time
for a stiff circuit or many scheduled events. This is not a background job service.

Standalone request and result schemas are in `schemas/parameter-sweep*.schema.json`.
Offline validation verifies source hashes, finite values, time budgets, ordered
measurements, successful sample times, summary status and null failure results.
It checks artifact consistency; it does not rerun physics or authenticate results.
The evidence is not a signature or a firmware/library archive.

## Independent acceptance

The hierarchical assembly example uses a 10 V source, a 1 MΩ series resistor in
each sheet and meters with 1 MΩ / 10 MΩ input resistance. Sweeping A.R through
100 kΩ, 1 MΩ, 2 MΩ and 10 MΩ gives:

| A.R | Expected A voltage | Expected B voltage |
| --- | --- | --- |
| 100 kΩ | 100/11 V | 100/11 V |
| 1 MΩ | 5 V | 100/11 V |
| 2 MΩ | 10/3 V | 100/11 V |
| 10 MΩ | 10/11 V | 100/11 V |

These independent bounds follow Ohm's law and the loaded voltage divider
`Vout = 10 × Rmeter / (Rseries + Rmeter)`, not the implementation's output.
Native acceptance uses 20 µV absolute tolerance. Sweeping meter input resistance
separately proves that the measurement device changes the circuit.

The `rc-filter` example is sampled at 100 µs with 10 nF and R = 1 kΩ, 10 kΩ,
100 kΩ. Expected values follow `3.3 × (1 − exp(−t/(R C)))`, with 100 µV absolute
tolerance for the source's finite 10 ns rise and native integration error. Repeating
1 kΩ after the other runs must reproduce the first sample exactly, exposing any
capacitor-history leak. Invalid negative resistance between valid points must
produce an explicit failure without poisoning the next run.

`tests/protocol/test_sweeps.py` executes these analytic checks, CLI/RPC parity,
actual meter loading, source/time/probe preservation, independent sheets, derived
DNP assemblies, unsupported contexts, malformed requests and null floating voltages.
`tests/browser/test_parameter_sweeps.py` uses the native server to inspect the plot,
exports, failures and unchanged 1 µs clock. UI tests cover range generation and CSV
missing-value semantics; both offline schema backends check artifacts. See the
[gallery](../gallery/README.md).

The workflow is informed by the ngspice
[control-language tutorial](https://ngspice.sourceforge.io/ngspice-control-language-tutorial.html)
and its separation of parameter alteration, analysis and plotting. No ngspice
result parity, SPICE compatibility or physical-hardware correlation is claimed.

## Remaining analysis work

Converged DC continuation, multidimensional sweeps, firmware/stimulus replay,
long transient acquisition, AC small-signal linearization, Fourier/windowing,
noise, distortion, temperature-corner qualification, uncertainty and external
reference-conformance suites remain separate milestones. Existing model fidelity
limits still apply to every point. A plotted response is not a qualification mark.
