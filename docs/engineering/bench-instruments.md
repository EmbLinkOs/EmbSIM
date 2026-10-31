# Bench instruments: a logic analyzer, a power profiler and an oscilloscope

Owner's request (2026-10-31): a real 8-channel logic analyzer of our own, a
real instrument that follows a board's power consumption as it runs, and a
real oscilloscope that works in real time; then the workbench taken to
v0.1. This plan follows ADR-033 (instruments are used as the real ones
are: components wired in by their leads, with their real characteristics)
and builds on the wired instruments already in the catalog
(`docs/engineering/instrument-loading.md`: voltmeter, ammeter, scope input,
the USB power meter).

## 1. Which real instruments

Each is a widely used, documented instrument, modelled by its published
specifications. A figure the maker does not publish is an assumption,
named as one in the part's fidelity.

### Saleae Logic 8 (logic analyzer)

From Saleae's Logic 8 data sheet and its support pages:

| Property | Value |
|---|---|
| Digital channels | 8, each with a ground lead |
| Input impedance | 1 MΩ ∥ 10 pF |
| Threshold | fixed: low below 0.6 V, high above 1.2 V (no selectable threshold on the Logic 8) |
| Sample rates | 100, 50, 40, 25, 20, 10, 8, 5, 4, 2, 1 MS/s; the highest not on all channels at once (Saleae's example: 3 channels, digital only, at 100 MS/s) |
| Digital bandwidth | 25 MHz |
| Input voltage | 0 to 5 V recommended, −25 V to +25 V absolute maximum |
| Memory | streams to the computer (the capture's length is the computer's memory) |

What is modelled: the eight leads loading the circuit (1 MΩ to ground
∥ 10 pF), the threshold with its hysteresis, sampling at the chosen rate
exactly as a sampler does (an edge is seen at the first sample after it; a
pulse between two samples is missed), the rate's limit by enabled
channels, a trigger on a channel's edge with a pre-trigger share, a
capture length, and export as Logic 2's CSV and as VCD (both open in
PulseView and Logic 2). The workbench draws it as Logic 2 does: channel
rows, rate and duration, Start, the trigger, the protocol decoders
(UART, SPI, I²C) already in the workbench.

Not at first: the Logic 8's analog inputs, the channel-count limit for each
rate beyond Saleae's published example (an assumption, named), abuse
beyond the absolute maximum (ADR-034).

### Nordic Power Profiler Kit II (power profiler)

From Nordic's PPK2 product page, product brief and user guide:

| Property | Value |
|---|---|
| Modes | Source meter (it powers the board) and ampere meter (in series with the board's own supply) |
| Source meter output | 0.8 to 5.0 V, up to 1 A |
| Ampere meter | the supply 0.8 to 5 V, 1 A limit |
| Current range | 200 nA to 1 A, five ranges switched automatically during measurement |
| Resolution | 100 nA to 1 mA, by range |
| Sampling | 100 kS/s |
| Accuracy | better than ±20 % on average currents |
| Logic port | 8 digital inputs, sampled with the current |

What is modelled: both modes (a source with its current limit; a shunt in
series), the five ranges switched by the current with each range's
resolution, 100 kS/s samples streamed live, average, peak and charge over
a window, the logic port, export as CSV. The workbench draws it as
Nordic's Power Profiler app does: the live current graph, a selected
window's average and charge, the digital channels underneath.

Not at first: each range's shunt and the voltage it drops (not published;
an assumption, named), the switching transients between ranges.

### Rigol DS1054Z (oscilloscope)

From Rigol's MSO/DS1000Z specifications and the sigrok device page:

| Property | Value |
|---|---|
| Channels | 4 analog |
| Bandwidth | 50 MHz |
| Sample rate | 1 GS/s (1 channel), 500 MS/s (2), 250 MS/s (3 or 4) |
| Memory depth | 12 Mpts (24 Mpts with the memory option) |
| Vertical | 8 bits, 1 mV/div to 10 V/div |
| Input | 1 MΩ ±1 % ∥ 15 pF ±3 pF |
| Triggers | edge, pulse, runt, window, nth edge, slope, video, pattern, delay, timeout, duration, setup/hold; RS232, I²C, SPI as options |
| Sweep | Auto, Normal, Single |

What is modelled: the four inputs loading the circuit (1 MΩ ∥ 15 pF, and a
10x probe's 10 MΩ), the bandwidth as a first-order limit, sampling at the
rate the enabled channels allow, 8-bit quantisation at the chosen V/div
and offset, the memory depth, the edge trigger with Auto, Normal and
Single, the timebase and trigger position, and live acquisitions as the
circuit runs in real time (ADR-032). The workbench draws its front panel:
the 12-division screen, each channel's V/div and offset, the timebase,
the trigger level and mode, Run/Stop and Single, measurements (frequency,
period, peak-to-peak, mean, RMS) and cursors.

Not at first: the other trigger types, the serial decode options, math and
FFT, the probe's compensation.

## 2. How they fit the engine

- **Leads load the circuit.** Each channel's input is a pin model with the
  instrument's impedance and threshold (the logic analyzer's digital
  inputs: `vil`, `vih`, a 1 MΩ pull-down, `cin`), or an analog input like
  the scope input part's.
- **Sampling is exact, not stepped.** The logic analyzer does not wake at
  each sample: an edge is placed at the first sample instant after it, and
  a change undone before that instant is dropped, which is what a sampler
  sees. The power profiler and the scope sample the analog solution at
  their rate between accepted steps (the solution held, as the USB meter
  integrates it).
- **Captures reach the workbench by a new method**, `instrument.read`
  (part id, since a sequence number), returning the samples or
  transitions added; the observe stream carries each instrument's state
  (armed, triggered, rate, counts). Recordings replay them, since they
  derive from the circuit.

## 3. The workbench to v0.1

The workbench has reached a usable v0.0.1: design, bench, run, debug and
verify perspectives. v0.1 makes it a bench a person works at:

1. The three instruments' front panels, opened from the part on the bench
   or the schematic, docked or floating, following the circuit live.
2. Each perspective's unfinished edges closed (listed in `docs/STATUS.md`
   under the workbench), keyboard and screen-reader paths checked.
3. The version set to 0.1.0 across the workbench, the desktop shell and
   the CLI's reported version, with release notes in the changelog.

## 4. Order and evidence

1. The logic analyzer: the engine model and fixtures (thresholds, a pulse
   shorter than a sample missed, the rate limit, the trigger), a protocol
   test reading a UART frame from an Uno sketch, the panel, the exports
   opened by PulseView's reader in CI where available.
2. The power profiler: fixtures in both modes (a known load's current, the
   range switch, the resolution), a board sleeping and waking seen at
   100 kS/s, the panel.
3. The oscilloscope: fixtures (an RC step's time constant measured, a
   square wave quantised at 8 bits, the trigger modes), the panel live.
4. The workbench v0.1 items, then the release notes.

Each lands with its catalog entry's fidelity, `CHANGELOG.md`, `docs/STATUS.md`
and screenshots in `docs/gallery/`.
