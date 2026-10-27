# Native captured-data frequency spectra

Analyze retained transient samples without running or changing the circuit again.
This adds calibrated one-sided FFT amplitude/phase review, not a small-signal AC
solver, hardware spectrum-analyzer model, noise density or distortion qualification.
The underlying component fidelity and finite sampling still bound every result.

## Capture and review

In **Transient capture**, choose Start/Stop and click **Use 256 FFT intervals**.
The preset keeps the endpoints and computes an exact picosecond interval; it
refuses a range that cannot be divided exactly. Capture the response. Fold
**Transient waveform and sample cursors** to focus the workspace, then open
**Frequency spectrum**. Choose a captured target, rectangular or periodic Hann
window, and whether to remove the arithmetic sample mean. Analyze the retained
record; no current project load, reset, advance or probe read is involved.

The viewer reports sample rate, bin spacing and Nyquist frequency. Select peak
volts or RMS-bin dBV, inspect a bin and narrow the displayed bin range. The dBV
floor clips the display only. Exports retain every raw bin, including values below
that floor. Zero bins have no finite dBV and no defined phase; the UI displays
−∞/undefined rather than inventing 0 dBV. Source waveform review remains available
in the same capture. Changing options marks the previous result as belonging to
its reviewed settings until recomputed.

CSV includes capture SHA-256, target, window/mean handling, frequency, complex
voltage, peak/RMS-bin amplitudes, dBV and phase. JSON additionally embeds the exact
native-serialized captured record and its SHA-256. The client verifies those bytes,
checks the record matches its retained capture, and verifies the embedded native
project/model identities. This establishes content lineage, not authentication of
an arbitrary edited file or physical manufacturer conformance.

## Exact record and scaling contract

A complete native capture must contain `N + 1` uniform samples with `N` a power of
two from 16 through 4,096. The FFT uses samples `0 … N−1` and excludes the final
endpoint, forming a half-open record. There is no silent padding, interpolation,
gap filling or adjustment of the capture's duration. Every selected voltage,
including the retained endpoint, must be finite/available. Other channels may
retain null voltages. Incomplete captures, malformed grids, unsupported options
and source/project/model hash drift are refused.

For sample spacing `dt`, `fs = 1/dt`, bin spacing `df = fs/N` and Nyquist is `fs/2`.
Bin spacing alone does not establish the ability to resolve nearby tones: the
window's main lobe also matters. Phase uses a cosine reference relative to the
first retained sample, not the current live clock or simulation time zero.

With optional arithmetic sample mean removal before windowing:

```text
Y[k] = sum(w[n] × (x[n] − mean_if_removed) × exp(−j 2π k n / N))
S = sum(w[n])
C[k] = Y[k] / S                 at DC and Nyquist
C[k] = 2 × Y[k] / S             at interior positive-frequency bins
peak_v = abs(C[k])
rms_v = peak_v                  at DC and Nyquist
rms_v = peak_v / sqrt(2)        at interior bins
dbv_rms = 20 × log10(rms_v / 1 V)
phase_deg = arg(C[k])           in [−180°, 180°], undefined at exact zero
```

The rectangular window is `w[n] = 1`. Periodic Hann is
`w[n] = 0.5 − 0.5 cos(2πn/N)`, matching a periodic analysis record rather than a
symmetric filter-design window. Coherent gain is `S/N`; equivalent noise bandwidth
in bins is `N × sum(w²) / S²`. Rectangular gives 1 / 1; Hann gives 0.5 / 1.5.
`mean_v` always reports the raw arithmetic mean, even when it is removed. With a
nonconstant window, windowed DC need not equal that arithmetic mean.

These are coherent **tone-amplitude** conventions. Hann spreads even coherent
energy into neighboring bins; off-bin tones additionally show scalloping loss.
Nearby/opposite-frequency lobes can overlap near DC or Nyquist. RMS-bin values
with coherent-gain correction cannot be summed as a broadband-power or noise-PSD
estimate. DC/Nyquist use the RMS of their sampled edge components; a Nyquist record
cannot reveal an unsampled quadrature or the original continuous waveform's RMS.
Tiny floating residuals can have unstable phase and are not a calibrated noise
floor. Aliasing remains in sampled data; no anti-alias filter is added.

## CLI, RPC and evidence

```sh
embsim transient examples/spectrum-rc/project.json \
  --request examples/spectrum-rc/response.transient.json > /tmp/rc-capture.json
embsim spectrum /tmp/rc-capture.json --target net:OUT --window hann > /tmp/rc-spectrum.json
python tools/validate.py /tmp/rc-spectrum.json --strict
```

Mean removal defaults on; `--keep-mean` retains it. CLI emits JSON, never modifies
input files, returns 0 on success and 2 for invalid captures/options or load errors.
It needs no original project/library paths and does not execute the captured
project's models, scripts or firmware. The normal installed engine runtime is
still required.

RPC/capability `analysis.spectrum` accepts `capture`, `target`, explicit
`window: rectangular | hann` and boolean `remove_mean`. It works without a loaded
project, independently of the current project's policy or clock. Only the captured
context must satisfy native board-free electrical transient evidence.

The `frequency_spectrum` artifact declares `method: radix2_coherent_amplitude`,
`record_convention: half_open_drop_endpoint`, `phase_reference: first_sample_cosine`,
window/mean settings, sample metadata, coherent gain/ENBW and ordered bins from
DC through Nyquist. Each bin exposes real/imaginary volts, peak, RMS-bin, dBV and
phase. The embedded capture document is bounded to 8 MiB; there are at most 2,049
output bins. Processing is synchronous, without mid-call cancellation. Native
normalization bounds FFT intermediates; unrepresentable voltage results are
rejected rather than emitted as infinity.

Both offline validator backends verify capture identities and grid/context,
recompute every complex coefficient with an independent direct DFT, and check
frequency, coherent gain/ENBW, peak/RMS/dBV, phase and zero semantics. Direct DFT
verification may take longer for the largest records. It proves consistency with
the retained data, not authenticity or electrical truth of user-edited samples.

## Independent acceptance

Synthetic mathematical vectors cover coherent two-tone amplitude/phase with DC,
mean removal/retention, both windows, zero records, DC and Nyquist edge scaling,
scalloping, alias folding, excluded-endpoint independence, the 4,096-point limit,
large/small amplitudes and nested identity drift. The native radix-2 code is checked
against standard-library direct DFT sums. Additional independent NumPy verification
agrees across 8,880 complex bins: 16 record/window/mean combinations at lengths
16, 64, 256 and 4,096, including an off-bin component. NumPy is verification-only,
not an engine or mandatory CI dependency.

The actual native `spectrum-rc` example applies a 1 kHz, 1 V-peak sine to 10 kΩ /
10 nF. After 16 ms warm-up, it captures 16–32 ms with 256 intervals. The analytic
transfer is `H(jω) = 1 / (1 + jωRC)`: output peak ≈0.846733 V and cosine-referenced
phase ≈−122.141908° (the sine input is −90°). Acceptance uses 20 µV and 0.01° bounds.
Its RMS bin is ≈0.598731 V, about −4.45537 dBV; this is absolute voltage, not gain.

Adding a 10 kΩ meter changes gain to
`0.5 / sqrt(1 + (2π × 1000 × 50 µs)²)` and phase to
`−90° − atan(2π × 1000 × 50 µs)`, verified natively. This proves the spectrum uses
the electrically loaded captured circuit rather than applying a visual correction.

`tests/protocol/test_spectrum.py` executes mathematical/physical bounds, stateless
operation, source preservation, CLI/RPC parity, invalid input and offline checks.
`tests/browser/test_spectrum_review.py` prepares the exact grid, captures real
waveforms, reviews peak/RMS/dBV/phase, changes the window and exports all bins while
retaining the 1 µs live bench. See [gallery](../gallery/README.md).
References: [NumPy DFT convention](https://numpy.org/doc/stable/reference/routines.fft.html)
and [SciPy periodic/symmetric Hann definitions](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.windows.hann.html).

## Remaining analysis gates

Arbitrary-length records/windows, streamed/background FFT, calibrated PSD/noise,
broadband RMS, automatic tone fitting, THD/distortion qualification, small-signal
AC sweeps, converged DC continuation, firmware/stimulus replay and broad hardware
reference conformance remain separate milestones. Existing component model and
sampling limits apply throughout; this is not completion of the analysis release gate.
