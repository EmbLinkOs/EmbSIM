# Circuit construction and live indication

This milestone builds on the workspace pass. The project net graph is the
canonical electrical connectivity; a wire path or crossing is presentation.
The same project must survive browser/desktop editing, native validation,
save/reload, undo and CLI scenarios.

## Connection rules

- A jumper names two distinct declared terminals in one project net. A rendered
  wire never invents connectivity. Duplicate/reversed jumpers are rejected by
  import validation; interactive repeat creation remains an idempotent no-op.
- Breadboard holes are individual contacts connected by catalog strip ties.
  Two direct jumper ends cannot occupy one hole. A different free hole on the
  same strip remains legal. UI insertion guards also reserve component legs.
- Generic bent-lead geometry and connector/body collisions are not manufacturing
  qualification. Native validation checks declared terminals, net agreement,
  direct-hole reservations and route bounds. Full pose-dependent component-leg
  occupancy remains a separate mechanical qualification task; it is not claimed
  from a schematic net or a component's package name.
- Color, rise and intermediate points are drawing data. Changing them preserves
  nets, measurements and simulation time. Reconnecting/removing is an electrical
  transaction with exact undo; the editor distinguishes its consequences.
- Intermediate points use [X, height, Y] in bench millimetres. Their height is
  nonnegative, coordinates finite/bounded and their number limited. Route edits
  have keyboard equivalents; invalid proposals leave the project unchanged.

## Wire interactions and tracing

A selected jumper exposes its named ends, color, automatic rise and editable
intermediate points. Pointer handles and numeric controls edit the same path.
Escape/pointer cancellation restores the previous path. The end contacts remain
anchored to their owning parts. Editing insulation never changes the net.
Selecting a jumper or choosing its net highlights the same net in the schematic,
bench and nets list, including the breadboard strip's contacts. Live values come
from engine state; unbuilt schematic connections stay explicit.

## Acceptance circuits

1. Uno/breadboard: existing blink/serial firmware, button, potentiometer and LED.
   Edit a jumper's path/color while observing the same net; save/reopen and undo;
   reject an occupied direct hole or invalid endpoint through UI and native API.
2. [RGB indicator bench](../../examples/rgb-indicator-bench/README.md): three
   physical switches and separate current-limiting resistors exercise off and all
   seven mixes under electrical policy. Its 28 CLI assertions check channel state
   and solver health. Isolated fixtures check missing common and opposite supply
   polarity. No firmware or named manufacturer qualification is claimed for this
   generic reference circuit. An ESP32-C3 GPIO/PWM version remains a subsequent
   firmware acceptance target; its existing addressable onboard RGB is a separate
   device/model from this four-lead discrete LED.
3. Isolated indicator fixtures: common cathode/anode, all seven RGB combinations,
   disabled/unpowered inputs, reversed bias and changing resistor/current. Under
   electrical policy three diode junctions must load the supply; functional
   policy retains explicitly ideal logic behavior with floating/unknown inputs
   off. Rendering consumes current/luminance/color outputs in both policies.

## Qualification and instruments follow this gate

Expand named parts in reviewed batches, each with manufacturer/package/pin maps,
operating limits, electrical fixtures and documented omissions. Priority families:
LEDs/RGB and current-limiting parts; 74HC logic and shift registers; timers,
op-amps/comparators; transistor/MOSFET drivers and optocouplers; regulators; sensor
interfaces. A generic illustrative fit is not a qualified manufacturer model.

After that, instruments use connected terminals and stated loading: voltmeter
input resistance, oscilloscope input resistance/capacitance and probe attenuation,
ammeter burden and fuse behavior. Datasheet/manual evidence precedes range,
accuracy, bandwidth or protection claims. Ideal engine probes remain identified
as ideal inspection tools.

## Implementation and evidence

`BenchWire` carries drawing overrides in the shared project schema; bench model
functions separate dressing from reconnecting. Reconnection first validates a
copy, so failed proposals cannot delete a wire. Removal also handles wires between
two loose components, without retaining a stale connection. The shared selection
net drives tracing. The 3D renderer uses anchored polyline points and cancellable
numbered handles; numeric drafts commit through the same project transaction.

Native load and commit check bench invariants independently of UI guards. Python
schema/semantic validation applies the same direct-wire rules; the built-in
schema backend supports tuple `prefixItems` for nonnegative point heights.
`tests/protocol/test_bench.py` verifies persistence, rejected transactions and
unchanged divider voltage/time/probes. UI model/browser workflows verify atomic
reconnection, strip tracing, appearance undo and invalid typed endpoints.

Electrical RGB reuses the native LED junction stamp three times, with shared
common and independently conserved channel currents. Functional/timed RGB uses a
native ideal indicator that explicitly watches all four passive terminals. The
3D dome, halo and point light consume `luminance` and `color`; rendering neither
solves a circuit nor generates autonomous blinking. Generic RGB fixtures cover
both polarities, every binary mix, floating common, nominal resistor-fed current,
increased limiting resistance, zero supply and reverse bias. Optical normalization
is display data, not lumens or manufacturer-qualified colorimetry.

## Next delivery gates, in order

A catalog entry is not qualified because its name or symbol exists. Each batch
must identify an exact manufacturer/orderable package, source revision, supported
supply/load/temperature envelope, observable electrical contract and independent
acceptance bounds. A typical-value fit must retain that label; full datasheet
corner coverage is a separate claim. Existing entries are audited before adding
look-alike aliases.

| Batch | Parts to qualify or expand | Required circuit evidence |
|---|---|---|
| Indicators and switches | named red/green/blue LEDs, common-anode/cathode RGB, tactile switches | measured/datasheet I–V points, resistor/current sweep, floating/reverse/off, GPIO drive/loading, PWM waveform and stated averaging limits |
| Logic | SN74HC00N/04N/14N, 74HC165/595 and tri-state drivers | powered/unpowered truth tables, thresholds/hysteresis, output loading, OE/high impedance, setup/hold and propagation limits |
| Analog and drive | NE555P, LM358P, LM393P, 2N2222-family exact variants, BSS138, flyback/optocoupler circuits | supply dependence, clipping/common-mode, open-collector pull-up, transient response, transistor load line and inductive switching |
| Supply and sensors | exact LDO variants, temperature/light/position parts and bus sensors | dropout/current limit/quiescent draw, transfer endpoints, supply failures, bus transactions and documented physical response |

Only after those component gates, qualify connected instruments. A voltmeter's
input resistance must visibly change a high-resistance divider; an oscilloscope's
parallel input resistance/capacitance and selected probe must change RC amplitude
and phase; an ammeter's series burden must change load current. Source/manual
specifications determine defaults, ranges, accuracy and bandwidth. Tests compare
loaded and unloaded circuits, include connection/polarity errors, and label ideal
engine probes separately. No instrument loading or manufacturer accuracy claim
is introduced by the current wire-editor milestone.
