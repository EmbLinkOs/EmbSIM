# Sampled acquisition and movable probes

Run now offers **Live**, edge-triggered **Single**, and repeating **Normal**
acquisition in the Acquisition and probes panel. The schematic remains the circuit
surface; optional 3D is unnecessary. The client consumes native samples and
capture frontiers. It does not solve a parallel circuit or advance the clock.

## Capture contract

Choose a numeric source, rising/falling/either edge, trigger level, full hysteresis
band, span, pre-trigger percentage and holdoff. Digital sources require known
0/1 transitions and use a fixed midpoint. Analog/value sources use their own
units: a current probe's level is amperes, not volts. The Schmitt detector must
observe the opposite side before accepting a crossing of the chosen band.

Arming ignores historical edges before the current simulation clock. The trigger
is timestamped at the first qualifying **sample**, without sub-sample interpolation.
Complete pre-trigger source history is required. The post-trigger interval waits
for every numeric trace's producer-proven frontier, including constant-signal
watermarks; an engine-clock observation alone cannot complete a capture. A lost
source interval invalidates a pending capture. Unknown logic, nonfinite readings
and explicit gaps reset edge qualification. Reset, source changes and history
loss restart pending acquisition. Repeating mode rearms after the window end plus
holdoff; packet boundaries do not change the selected edges.

A completed capture owns copies of all participating records. Single capture
freezes the scope, logic analyzer and CSV/VCD exports while firmware and meters
continue. The T cursor marks the sampled trigger time. Pan/zoom and cursor
inspection work on the frozen data. Normal keeps the most recent completed frame
while awaiting the next. Rearm explicitly replaces the acquisition; Live returns
to received samples and clock following. CSV leaves unobserved readings blank.
Plots and cursor reads stop at the capture frontier and break at explicit losses.
Missing analog JSON readings remain unknown rather than becoming zero volts.
Frequency is unavailable across an incompletely observed interval. VCD seeds the
left endpoint and marks digital losses X; analog VCD export requires complete
coverage because its real-valued format cannot portably represent missing values.

## Moving an observation probe

Drag a waveform marker onto a connected wire/pin, or select its net by keyboard
in Acquisition and probes. Escape, window blur and invalid drops preserve the
connection. A successful native `probe.move` keeps the handle, name, kind, rate
and subscription, clears the old samples and seeds the new source at the current
virtual time. No circuit transaction, simulation reset or extra electrical load
is introduced. Failed resolution/allocation preserves the previous source and
capture. Moves during recording are rejected because replay does not yet record
source-retarget operations. Frozen evidence remains frozen until explicitly rearmed.

`capture_epoch` distinguishes reset/source identity from packet sequence. Both
probe metadata and waveform frames carry it. New epochs replace the live record;
late frames from older epochs cannot splice in obsolete samples. Metadata arriving
after a new frame must not clear that frame. Resubscription retains the existing
explicit restart contract. Old clients may ignore the additive epoch fields. Deterministic scenario run
reports omit these live transport identities, so repeated circuit runs retain
identical reports. Connecting to a replacement session resets client epoch identity.

The markers are **ideal observations**. Physical loading still requires wiring
`voltmeter`, `ammeter`, `scope_input` or `function_generator`; see
[instrument loading](instrument-loading.md). Selecting another net does not
reassign the terminals of a physical scope input.

## Evidence and remaining work

The [acceptance circuit](../../examples/acquisition-bench/README.md) uses an actual
50 Ω generator, series resistor, capacitor and loaded scope input. Its CLI
scenario independently checks three loaded RC voltages. The native browser
workflow checks sampled trigger timing, complete coverage, the RC response in CSV,
unchanged exports after another 8 ms, and keyboard/pointer retargeting with new
epoch data. Protocol tests exercise stable identity/subscription, failed and
same-target moves, recording rejection, repeated callback cancellation and reset.
A native session fixture forces scheduler exhaustion and verifies that the old
source, ring, samples, callback and epoch remain unchanged. Session, move and
stream tests also run locally with address/undefined-behavior sanitizers.
Unit tests cover chunk independence, hysteresis, unknowns, missing prehistory,
post-trigger loss, reset, same-time events, copy isolation and epoch ordering.

This is software acquisition from ideal observations, not a qualified commercial
scope. Auto timeout, pulse-width/pattern/protocol triggers, segmented acquisition,
ADC quantization, transfer bandwidth, sample jitter, calibrated trigger latency,
noise and protection remain unimplemented. No sub-sample accuracy or zero-dead-time
acquisition is claimed. Trigger precision is limited by probe sampling. Buffers
remain bounded (native 65,536 samples per probe, client four million by default);
missing retained history is not reconstructed. Cross-sheet hierarchy, advanced
analysis and engineering release gates remain separate milestones.

```sh
embsim run examples/acquisition-bench/project.json \
  --scenario examples/acquisition-bench/scenario.json --json
python3 tests/protocol/test_probe_move.py --embsim build/dev/bin/embsim
# Build the workbench before the native browser check.
python3 tests/browser/test_acquisition.py --embsim build/dev/bin/embsim \
  --chromium /usr/bin/chromium
```
