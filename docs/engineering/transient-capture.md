# Isolated native transient capture

Capture a circuit's time response without replacing the live project or clearing
its clock, outputs or probe history. This is an ideal observation of a separate
native run, not a physical oscilloscope. Connected instrument components still
contribute their actual electrical loading to that run.

## Schematic workflow

Open **Transient capture**, choose Start, Stop and Sample interval, then add up to
eight net/pin voltage targets. A terminal picker includes concrete sheet references;
manual names use `net:OUT` or `pin:A.R.2`. The source must be paused, board-free and
under electrical policy, with no open project transaction. Capturing a derived
assembly preserves its retained origin and its actual DNP circuit behavior.

The isolated run resets at zero, simulates any warm-up before Start and advances
the native solver to each requested sample time, including both endpoints. Native
adaptive integration and breakpoints remain inside each advance; the requested
sample interval is not a replacement integrator timestep. Project-defined source
waveforms and initial controls apply. Current runtime changes, queued inputs,
firmware, host connections and debugger state are not replayed.

The viewer overlays captured traces, permits per-trace visibility, and zooms by
first/last sample index. **Fit capture** restores the whole recorded range. Exact
A/B cursors select recorded samples, including samples outside the current view;
Δt is signed B minus A. The table exposes each trace's A/B voltages, B minus A
voltage and the minimum/maximum **captured samples in the view**. It does not infer
unsampled peaks, settling or bandwidth.

Differential review subtracts two captured voltages; the dashed derived trace and
cursor values add no physical load. An unavailable operand leaves a gap. This
mathematical trace does not stand in for the common-mode range, bandwidth or input
loading of a real differential instrument.

Lines join recorded sample values only. Null voltages break a line; X/Z logic is
retained separately from a numeric analog solution. A purely digital/floating net
without an analog solution is unavailable, never fabricated zero. Finite sampling
can miss narrow pulses or alias signals. Select an interval appropriate to the
circuit and existing model fidelity; this is not an anti-alias filter or an
accuracy/qualification claim.

**Exact captured samples** pages through all raw records in groups of 100; it
retains integer picoseconds beside formatted times. CSV exports every raw sample,
separate logic/voltage columns, picoseconds/seconds and the captured source SHA-256.
JSON retains the request, raw samples, native diagnostics and exact native
engineering manifest, including project/model descriptions and their identities.
Differential review/zoom does not alter exported raw data.

The graphical client verifies native source identities and refuses a capture
whose native project text differs from the prepared inputs. Schematic changes
mark inputs stale; refresh before another capture. Existing results and exports
continue to describe their captured source. Source identity is not a signature
or a complete library/firmware archive. Relative resources retain project context.

## Requests, limits and partial results

```sh
embsim transient examples/rc-filter/project.json \
  --request examples/rc-filter/response.transient.json > /tmp/rc-response.json
python tools/validate.py /tmp/rc-response.json --strict
```

CLI emits JSON without modifying source files. Return codes: 0 complete capture,
1 inspectable incomplete capture, 2 usage/load/context/request error. Optional
`--library DIR` loads an existing library, without downloading dependencies.

RPC `analysis.transient` advertises capability of the same name. Required fields
are string `start`, `stop`, `step` and unique `probes`; optional metadata is
`schema_version: 1`, `kind: transient_request`. Bounds:

- `0 <= start < stop <= 100 ms` after reset.
- A positive step dividing `stop − start` exactly: 2–4,097 uniform samples.
- 1–8 existing `net:` / `pin:` targets, at most 191 bytes each.
- At most 256 compiled components / 1,024 nets; no boards or non-electrical policy.

Execution is synchronous and cannot be cancelled midway through an RPC call.
Circuit stiffness and scheduled events still affect wall time. Demo transport
refuses to fabricate waveforms. Source probes retain their declared configuration
inside the temporary session; the capture records native net values directly and
is not limited by or drained from their waveform rings.

The result is `kind: transient_capture`, `method: reset_uniform_sample`, with
`source`, `request`, `ok`, `ended_at_ps`, `failure`, `diagnostics` and `samples`.
Each successful record has exact `at_ps` and ordered measurements containing
`target`, `logic`, `voltage`. On load/reset/runtime error, capture stops and retains
only the actual successful prefix. No future or interpolated rows fill the gap.
`ended_at_ps` is the temporary session's reached time; `failure` and native
diagnostics explain incompletion. An error may occur within the final advance;
the reached time is not a claim about the exact instant of the fault. The GUI
labels this prefix incomplete.

Standalone schemas and both offline validator backends check exact grids, bounds,
ordered targets, finite/null voltages, complete/partial status, bounded prefix
endpoints and source hash/context integrity. They verify artifact consistency,
not the physical correctness or authenticity of arbitrary edited data.

## Independent acceptance

`examples/rc-filter` has a 3.3 V pulse, 10 kΩ resistor and 10 nF capacitor. During
the first rising half-cycle, the independent ideal-step equation is
`Vout(t) = 3.3 × (1 − exp(−t / 100 µs))`. Native capture samples 0–500 µs at 5 µs
and checks every output against that equation within 200 µV absolute tolerance.
The tolerance accounts for the configured finite 10 ns source rise and native
integration error; it is not a manufacturer tolerance.

Warm-up capture at 100–200 µs verifies that Start does not reset the capacitor at
100 µs. Adding a 10 kΩ voltmeter changes final amplitude to 1.65 V and the time
constant to 50 µs: `1.65 × (1 − exp(−t / 50 µs))`, checked at 100 µs. This exposes
real measurement loading instead of applying a post-processing voltage correction.

`tests/protocol/test_transient.py` checks those equations, deterministic repeats,
unchanged live state/probe history, maximum picosecond grid, DNP assemblies,
floating voltages, unsupported contexts, malformed requests and CLI/RPC parity.
A test-only Lua model raises an error at 12 µs between requested 10/15 µs samples;
only the 0/5/10 µs prefix survives, the source remains at zero and CLI returns 1.
Both complete and partial artifacts pass offline checks.

`tests/browser/test_transient_capture.py` uses the actual native server to capture
101 samples, review differential/zoom/cursors, inspect raw times, export all data
and reject a nonuniform request while retaining the live 1 µs bench clock. UI tests
check differential missing-value semantics, source-bound CSV, plot gaps and finite
geometry. See [gallery](../gallery/README.md).

The native analog engine's existing integration/fixture evidence remains in
[SPEC-004](../specs/SPEC-004-analog-engine-and-bridges.md). This adds the capture
workflow, not new global electrical fidelity. The
[ngspice control-language tutorial](https://ngspice.sourceforge.io/ngspice-control-language-tutorial.html)
illustrates the real separation of transient analysis, observation and plotting;
this artifact does not claim ngspice compatibility or independent parity for all
models/circuits.

## Remaining work

Firmware/stimulus replay, arbitrary long/background acquisition, triggered
physical instrument qualification, continuous peak detection, anti-alias filters,
uncertainty estimation, converged DC continuation, small-signal AC, Fourier/noise/
distortion and broad reference-conformance suites remain separate milestones.
