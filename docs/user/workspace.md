# Arrange your workspace

The top row contains the project menu, **Design**, **Run**, **Debug**, **Verify**,
diagnostics, theme and command-palette controls. Simulation controls live in the
row below: run/pause, event stepping, advance duration, reset, speed, clock and
fidelity policy. Changing perspectives keeps those controls available. Long
project names shorten visually inside their menu; the project name is unchanged.
The advance field, its button, F7 and the command palette use the same interval.

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
