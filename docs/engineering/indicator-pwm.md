# Firmware-driven indicator acceptance

This milestone extends the named indicator DC evidence with a firmware-driven
ESP32-C3 RGB circuit and shared CLI/workbench waveform measurements. It does not
turn a static LED fit into a transient, thermal or photometric model.

## Measurement contract

Scenario assertions may select `measure: "mean"`, `"rms"` or `"duty_cycle"`
over a nonempty `between` window and use numeric `min`, `max` or `equals`
expectations. Mean and RMS integrate the recorded signal with sample-and-hold
semantics over **[start, end)**, clipping the first and last intervals. Duty cycle
is the fraction of time a digital probe is known high. These are time-weighted
measurements, not averages of samples or GUI observation frames.

A value at or before the window start is required. Unknown/high-impedance digital
intervals, nonfinite values, lost history or stream gaps invalidate the result;
they are not silently treated as zero. The workbench bounds measurements by the
current simulation time. Sampled analog/value measurements retain their sampling
resolution; refining that resolution is part of circuit acceptance. The same
contract applies to CLI and browser records, without asserting identical capture
rates or loss-free transport.

## Electrical and visual scope

The acceptance circuit uses the named common-cathode RGB package, individual
external limiting resistors and the MCU's existing finite GPIO drivers. Firmware
boots through the chip's ROM, programs LEDC registers and routes three channels through the GPIO matrix. Tests
must check frequency, duty, on-state current, mean current and driver voltage
drop, including off and full-on endpoints. Averaged electrical current is not
calibrated optical brightness or a manufacturer PWM/transient qualification.

Scope traces remain ideal inspection probes. Wire the separate
[loaded instrument components](instrument-loading.md) to include oscilloscope
input R/C, voltmeter loading, ammeter burden or generator output impedance.

## Circuit and independent bounds

The [example](../../examples/esp32c3-qualified-rgb/README.md) boots its flash
image through the ESP32-C3 ROM, routes LEDC channels 0/1/2 onto IO4/IO5/IO6 and
reads an external physical MODE button on IO0. Its 39 assertions cover the mixed
25/50/75% duties, off, full-on and return to mixed. The 250 ms boot allowance is
outside the measurement windows. This is bare-register firmware, not an ESP-IDF
or Arduino library conformance claim.

GPIO loading exposed a loader bug: `io.electrical.rout: 20` was ignored when a
GPIO model did not select a speed index, leaving the family default of 25 Ω.
Scalar resistance/slew now initialize the pin's base model; per-speed arrays
remain runtime overrides. Logic-family thresholds and worst-case VOH/VOL handling
are unchanged. A linear 20 Ω driver is a representative configured model, not
a manufacturer curve or qualification of the ESP32-C3's drive-strength settings.

For each channel, independent source points from the pinned Kingbright PDF are
linearly interpolated to solve `Vsource(I) + I*(100+20) = 3.3 V` within 2–20 mA.
The current uncertainty is the source curve's ±0.060 V divided by 120 Ω.
Expected mean and RMS are `I*D` and `I*sqrt(D)`. Mean bounds add 1% of on-state
current and RMS bounds 2%, plus 0.1 µA, for sampling/settling resolution. Loaded
GPIO voltage bounds use `3.3 - 20*I` and a further ±5 mV. These source-derived
bounds are distinct from observed native values.
Fully off windows require mean current within ±1 µA and RMS no greater than
1 µA; edge-sampling allowances do not relax the steady off checks.

Digital probes sample at 1 MHz (1 µs); numeric current/voltage at 200 kHz (5 µs).
At 1 kHz, a 25% pulse spans 250 digital and 50 numeric sample intervals. The
current waveform and its normalized display are static-junction approximations;
edge charge and real optical response are excluded. The 3D emitter continues to
show instantaneous observations and can alias PWM; no frame-averaging claim is
introduced by the electrical measurement strip.

Repeating the same circuit at doubled digital/numeric sample rates (2 MHz and
400 kHz) passes all 39 assertions. The two runs agree within 1 nA for every mean
and RMS current, and within 0.001 of duty fraction. This is numerical resolution
evidence for this circuit, not a smaller manufacturer tolerance or proof of
unmodeled edge/optical behavior.

The flash image's absence of ELF function symbols is tested as a rejected
coverage request. The ordinary analysis suite selects ELF-backed examples; all
examples, including this flash image, still execute in the headless suite.

## Reproduction

```sh
embsim run examples/esp32c3-qualified-rgb/project.json --scenario examples/esp32c3-qualified-rgb/scenarios/pwm.json --json
python tests/protocol/test_waveform_measurements.py --embsim build/ui-layout-native/bin/embsim
```

Native unit checks and browser waveform tests also cover clipped windows,
uneven event spacing, zero-duration same-time transitions, retained-history
loss, future endpoints, X/Z and nonfinite intervals, and picosecond timestamps
above JavaScript's safe-integer range. CLI contract tests require invalid modes,
expectations and out-of-run windows to fail. The scenario editor exposes the
same three measurement modes and clears the mode when timing changes away from
`between`. Scope and logic-analyzer measurements show capture coverage and no
numeric average when the full requested window is unavailable.

Probe metadata carries its declared physical unit and actual sampling rate;
numeric output units come from the component description, not a probe-name
suffix. Native device current/power quantities also retain their physical units.
Live meter tiles also honor the component's declared output units, including
named RGB channel currents that do not follow the older `i`/`ic` naming convention.
The scope uses one unit per vertical axis: selecting a voltage or current trace
switches the visible unit group while preserving all trace records. Measured run
reports retain the measurement name, unit and requested picosecond window.

## Complete capture delivery

Native inspection exposed a streaming truncation: the server sent at most 20,000
samples and advanced its cursor to the end of the entire ring. Large advances
therefore lost the unsent tail, and a browser could mistakenly hold the last
delivered value through the remaining window. Streams now flush every retained
sample in bounded chunks using absolute sample ordinals, preserving same-time
transitions across packet boundaries. Evicted unsent history starts a gap;
evicting previously delivered history does not.

Each frame carries `through_time_ps`. Intermediate chunks prove coverage only
through their last sample; the final chunk proves capture through the engine
clock. A constant signal sends an empty watermark when time advances without
new edges. The workbench caps measurement coverage at this producer frontier,
even when the clock notification has already arrived. A `restart` frame replaces
both the prior record and frontier after subscription/reset. Protocol regression
tests compare 30,001 streamed samples against native probe reads, then exercise
ring eviction, constant frontiers and restart behavior.

The toolbar, F7 and command palette also share the selected advance interval;
F7 no longer silently advances a fixed 1 ms when another amount is displayed.
