# Bench instruments

Three instruments sit on the bench as parts, wired into the circuit by their
leads, the way they would be on a real desk. Each one loads the circuit the
way the real input does and samples the way the real device does. Each has a
front panel modelled on the maker's own software or front panel. Their
figures come from the makers' documents
([the plan](../engineering/bench-instruments.md) quotes them). Each part's
fidelity in the library lists what is not modelled.

| Part | Real device | What it does |
|---|---|---|
| `emblink/saleae_logic8` | Saleae Logic 8 | 8-channel logic analyzer, 1 to 100 MS/s |
| `emblink/nordic_ppk2` | Nordic Power Profiler Kit II | Supplies or measures a board's current at 100 kS/s, with an 8-channel logic port |
| `emblink/rigol_ds1054z` | Rigol DS1054Z | 4-channel oscilloscope, 1 GSa/s, 8 bits |

## Place and wire one

Place an instrument from the library (category **Instruments**) and wire its
leads like any part:

- **Logic 8:** D0–D7 to the signals and GND to the circuit's ground. Each lead
  is 1 MΩ ∥ 10 pF. It reads high above 1.2 V and low below 0.6 V.
- **PPK2:** VOUT to the board's supply and GND to its ground; the board then
  runs from the PPK2 in source mode. In ampere mode the PPK2 goes in series:
  VIN from the supply, VOUT to the board. D0–D7 are its logic port.
- **DS1054Z:** CH1–CH4 to the nets to watch and GND to ground. Each input is
  1 MΩ ∥ 15 pF behind its probe; with the 10x probe it ships with, the circuit
  sees 10 MΩ.

An instrument loads the circuit as the real one does. A 1x probe on a
high-impedance node pulls it down; the 10x probe pulls it down much less.

## Open its front panel

Any of these opens an instrument's panel:

- **Open front panel** in the inspector, with the part selected;
- right-click the part on the schematic, then choose **Open front panel**;
- select the part in the bench's **On the bench** list, then choose **Open
  *part*'s front panel**;
- the command palette (Ctrl/Cmd-K): **Open *part* · *instrument***.

The panel opens in the Run perspective's dock, unfolded and focused. To keep
it in view while you edit the circuit, choose **Float** in the inspector,
the panel's header or the palette. It then floats in a window over every
perspective:

- drag its title bar to move it, or focus the title bar and use the arrow
  keys;
- drag the corner to resize it, or Shift with the arrow keys;
- **Dock** puts it back in Run.

Each window's place and size are kept for the next session.

## The logic analyzer

Laid out like Saleae's Logic 2. Choose the sample rate, the capture's
duration and the trigger (a channel's rising, falling or either edge), then
**Start**.

- The rate follows the channels in use, as on the real device: 100 MS/s
  with up to 3 channels, 50 with up to 6, 40 with 7 and 25 with all 8.
- A pulse shorter than the sample period can be missed, as it can on the
  real device.
- While the analyzer waits for its trigger, the rows stay empty, as in
  Logic 2.
- The **Async Serial** analyzer decodes a channel at a baud rate.
- Export the capture as Logic 2's digital CSV or as a VCD.

## The power profiler

Laid out like Nordic's Power Profiler app. Choose **Source meter** (it
supplies 0.8–5 V, limited at 600 mA) or **Ampere meter** (in series, up to
1 A), then **Start**.

- The current is the average over each 10 µs, quantised to the resolution of
  the range it falls in (0.2, 0.5, 5, 50 or 1000 µA).
- The readouts give the window's average, maximum and charge. Set the A and
  B cursors to measure a selection instead. **Average since Start** covers
  everything sampled since you pressed Start.
- The logic port's wired channels are shown under the current, on the same
  time axis, so a pin toggled by firmware lines up with the current it
  causes.
- **CSV** saves the current samples.

## The oscilloscope

The screen is 12 divisions by 8, with the channels in Rigol's colours (CH1
yellow, CH2 cyan, CH3 magenta, CH4 blue). The front panel has:

- **Run/Stop** and **Single**; the sweep **Auto** (refreshes even without a
  trigger), **Normal** (waits for one) or **Single** (stops after one
  acquisition; Single or Run re-arms it);
- the timebase and each channel's volts a division in 1-2-5 steps, its offset,
  probe (1x or 10x) and coupling (DC, AC, GND);
- the edge trigger's source, level and slope.

The top line shows the sample rate and memory depth, which follow the channels
on: 1 GSa/s with one channel, 500 MSa/s with two, 250 MSa/s with three or
four, lowered when the memory (12, 6 or 3 Mpts) can't hold the screen. The
status badge reads T'D, WAIT, AUTO or STOP.

The measurements (frequency, period, Vpp, Vmax, Vmin, Vavg, Vrms, duty, rise
time) are taken on one channel's 1200 screen points. An edge faster than
their spacing reads as **< *spacing*** rather than a made-up figure.

## From a script

`instrument.read({part, since})` returns what an instrument captured since a
sequence number: transitions (`kind: "logic"`), current samples
(`kind: "power"`) or the last acquisition (`kind: "scope"`). See SPEC-008.
The instruments' state (armed, sampling, rate, status) is in the observe
stream, so scenarios can assert on it.
