# Arrange your workspace

The top row contains the project menu, **Design**, **Run**, **Debug**, **Verify**,
diagnostics, **Settings** (the gear, or Ctrl+,) and the command palette. Simulation
controls live in the row below: run/pause, event stepping, advance duration, reset,
the readout, speed and fidelity policy. Changing perspectives keeps those controls
available. Long project names shorten visually inside their menu; the project name
is unchanged. The advance field, its button, F7 and the command palette use the
same interval. In a small window the simulation row wraps onto a second line rather
than hide a control.

## Read the simulation

The readout names the simulation's state in words, with an icon: **Running**,
**Paused**, **Stopped** (at time zero), **Loading**, **Failed** (the last run ended
on a fault or an error) or **Disconnected**. Hover it for what the state means.
**Simulated** is the simulated time (hover for the exact picoseconds). **Host** is
how fast simulated time passes against your computer's clock, measured over the
last two seconds while running (1.00× is real time); it reads "—" while nothing is
measured. The status bar repeats the state and simulated time on the left, and
shows the errors and warnings (click to open the diagnostics) and the engine
connection (hover for its address, protocol, round trip and frames) on the right.

If the engine stops, a screen says so, shows where it was and the simulated time
reached, and offers **Reconnect**.

## Settings and language

**Settings** applies each change at once and keeps it between sessions:

- **Interface language**: English or Français. Register, pin, net and part names,
  units and protocol names stay as written; a message not yet translated appears
  in English.
- **Theme**, **text size** (small, medium, large) and **density** (comfortable,
  compact).
- **Restore panel layout**: off, the next start opens the default layout.
  **Reset layout** returns every perspective's panels to their defaults now.
- **Time unit** (automatic, s, ms, µs, ns), **time precision** (0 to 6 decimals;
  the time is truncated, never rounded up past the simulation) and the **host
  rate** as a factor or a percentage, with a preview.
- **Restore defaults** under About.

## Arrange the panels

In Design, choose **Schematic** or **Bench** above the canvas. The schematic's
auto-layout, arrangement, checks and finder act on its drawing. The bench shows
its own physical editing controls instead. In a narrow canvas the repeated panel
title gives way to the view/tool controls, which can wrap onto another line.

Fold Library or Inspector with the arrow in its header and reopen it from its
labelled rail. **Focus canvas** provides more space for the scene. Drag a panel's
grip to reorder it; the grip also accepts Alt+Arrow keys. Drag a divider to resize
adjacent panels, or focus the divider and use the arrow keys.

If a window becomes too small for the panels' usual minimum sizes, they share
the available space temporarily. Expanding the window restores their preferred
proportions. The temporary fit does not erase saved panel sizes or fold choices.
Dense panel contents scroll within their own area. To read status details that
do not fit along the bottom, focus the status bar and use Left/Right, or scroll
it horizontally.

The 3D view loads when opened. You can still select and edit through **Parts &
wiring** while it loads, or choose **2D**. A failed 3D download leaves the flat
view and keyboard editor available; **Reload to retry 3D** starts a fresh page
request. A graphics-context failure after loading offers an in-place **3D** retry.

These layout and viewing choices are separate from electrical connections,
firmware and the simulation clock. Appearance and workspace preferences are
saved on this device; they are not electrical project data.

In **Run**, the oscilloscope's mean and RMS and the logic analyzer's duty cycle
are weighted by elapsed time within the displayed window. **Coverage** reports
how much of that window has known captured data. A missing start, stream gap,
unknown digital interval or future time leaves the average blank; pan to a fully
captured window. These inspection probes do not model physical instrument loading.
Coverage follows each waveform's delivery watermark, so a clock update arriving
before the final data chunk cannot turn a missing tail into a valid average.
The scope plots one unit per vertical axis; select a voltage or current trace
to switch groups. Current output probes retain amperes from the part description.

In **Verify**, a `between` assertion can check **Every sample**, **Time mean**,
**Time RMS** or **Duty cycle**. Measured assertions use numeric bounds or equals
with an absolute tolerance. Duty is a fraction from 0 to 1 and requires a digital
probe. [Try the firmware-driven RGB example](../../examples/esp32c3-qualified-rgb/README.md).


To include measurement loading, place **Voltmeter (loaded input)**,
**Ammeter (shunt burden)**, **Oscilloscope input (R ∥ C)** or
**Function generator (50 Ω output)** from the Instruments category in Design.
Use electrical policy and wire both leads. Run's **Wired instruments** panel
shows their configured impedance, connected nets and engine readings; **View
leads** selects the part, and **Add trace** plots its output. The ammeter belongs
in series. Generator waveform values specify open-circuit voltage; a matched
50 Ω load receives half that voltage. The negative lead is an explicit reference,
not an automatic earth connection.

Selecting a schematic wire highlights the whole net and its terminal rings.
Selected parts, highlighted terminals and focused/hovered pins reveal names and
observed logic states. Paused Design views retain observations. The **electrical
warnings** control reveals current floating/unknown inputs and conflicting drivers;
choose a warning to locate its net. An X level alone is not called a conflict.
Try the [instrument loading bench](../../examples/instrument-loading/README.md).
