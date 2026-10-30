# Building and inspecting the bench

In Design, select **Bench** above the canvas. The bench and schematic edit the
same project connections; changing views does not create a second circuit.
The Run perspective shows the bench with live controls. The schematic opens
without downloading the optional 3D scene. When you open the bench, **Opening 3D
bench…** announces that loading is underway; **Parts & wiring** remains usable,
and its selected part stays selected when the scene appears.

See [Arrange your workspace](workspace.md) for global navigation, panel sizing
and keyboard access to status details.

## Inspect the circuit

Right-drag to orbit, middle-drag to pan and use the wheel to zoom. **F** or
**Frame the bench** fits the assembly. Select a part and choose **Focus part**
to fit that part while retaining a top view if one is active. **Focus canvas**
in Design folds the library and inspector out of the way. In **View**, choose
a paper/graphite/anti-static-mat surface, adaptive/all/selected-only labels, and
whether notes are shown. These are viewing aids, not changes to the physical
circuit. Paper is the default surface.

### Views, measurements and sections

These work as an EDA tool's 3D viewer does
([the plan](../engineering/bench-3d.md)). None of them changes the project.

- **Standard views.** **View** has **Top**, **Bottom**, **Front**, **Back**,
  **Left**, **Right**, **Isometric** and **Desk** (a person's view from a
  chair). Each one frames the bench. The keys are KiCad's, with the bench
  focused: **Z** top (**Shift+Z** bottom), **Y** front (**Shift+Y** back),
  **X** left (**Shift+X** right), **I** isometric, **F** frame.
- **View cube.** The cube in the top-right corner turns with the camera;
  click a face to see the bench from that side.
- **Projection.** Choose **Perspective** or **Orthographic**. An
  orthographic view keeps parallel lines parallel, as a drawing does.
- **Render quality.** **Draft render** is the default. **Quality render**
  adds ambient occlusion, a glow around what emits light (lit LEDs and
  indicators), 4× multisampling and finer shadows. It drops back to draft,
  with a notice, when a frame takes longer than the 60 Hz budget.
- **Measure.** Choose the **Measure** tool and click two points. A click near
  a pin or a hole (within 1.5 mm) snaps to it. The distance shows in mm with
  its Δx, Δy (depth) and Δz (height), and as a count of 2.54 mm pitches when
  it falls on the grid. **Esc** clears it.
- **Section.** In **Inspect**, cut across X, Y (depth) or Z (height) at a
  position in mm. **Keep the other side** flips the cut. The desk stays whole;
  cut bodies show open.
- **Explode.** The slider lifts every part plugged into a breadboard off it,
  so you can see which holes its legs use.
- **Height plane.** A plane at a height above the desk, for an enclosure's
  lid. The parts that rise through it are outlined in red and listed in the
  status line, tallest first.
- **Layers.** Show or hide boards, breadboards, parts, jumpers, the world's
  bodies and labels. A hidden layer is not under the pointer.
- **Export.**
  - **Image 2×** and **Image 4×** save the view at that multiple of its
    resolution, as PNG, without the HTML labels.
  - **GLB model** saves the bench as glTF binary, in metres, each part a node
    named by its id, for Blender or an MCAD tool.

Projection, quality and layers persist on this device. Section, height plane
and measurement are cleared when the page reloads.

**Parts & wiring** folds or reveals the part list and keyboard forms. On a narrow
bench the list moves below the scene; it is not silently removed. Surface, labels,
notes and list visibility persist on this device. Blocked or full browser storage
leaves current-session changes usable. Camera positions are not persisted.

Selecting a part shows the geometry's source in the list: catalog dimensions,
artwork anchors or an approximate package. None is a mechanical qualification
badge. Approximate bodies can differ from the actual manufacturer package; verify
the package, pinout and dimensions before building hardware. Board bodies use the
declared PCB thickness, standoff and header style; absent values use generic
renderer dimensions. Header styles do not validate connector mating or clearance.

## Wire contacts

Choose **Jumper** and a colour. Point at a pin or breadboard hole: the contact ring
and feedback show its exact terminal name. Press and drag to another terminal;
the feedback names both endpoints before release commits the jumper. Picking uses
the projected contacts in the current camera, rather than distances on the desk.
Contacts covered by another component are excluded. If contacts overlap too
closely on screen, zoom in to disambiguate them. The larger pointer hit area does
not make a contact electrically bigger.

Escape, pointer cancellation, losing pointer capture, window focus loss or hiding
the document cancels a pending jumper and rolls a pending part drag back. Releasing
a cancelled gesture cannot create a wire or move. Held buttons are released when
the gesture is interrupted. Changing the tool, edit permission or live mode also
cancels the current gesture.

Alternatively type endpoints such as `BB1.e20` and `B1.D9` in **Parts & wiring**.
Unknown endpoints are rejected without editing the project. Repeating the same
jumper, including its reverse, does not add a second copy. Connections still use
the project's transactions and undo. Physical wire overlap is not a junction.

**Move** drags parts; R turns the selected part, Shift+R turns the other way,
arrows nudge by one pitch and Delete removes the selection. The list also offers
plug/turn/lift actions. Controls remain usable while firmware runs: hold buttons,
flip switches and drag knobs. The engine determines the resulting circuit state.

## Edit and trace existing jumpers

Select a jumper on the canvas or in **Parts & wiring → Jumpers**. Its shared net
is highlighted in the schematic and bench, including contacts on the same
breadboard strip. The trace lists named terminals and the engine's latest voltage
or logic level; it reports no observation when disconnected from the engine.

**Selected jumper** edits insulation color and automatic rise. **Add route point**
adds a numbered handle: drag it across its horizontal plane, or type its X, height
and Y coordinates in millimetres. **Apply appearance** commits the numeric draft
as one undoable edit. Drag release commits once; Escape, blur and pointer
cancellation restore the original path. **Automatic route** clears the draft's
points; apply it to return to automatic routing. Ends stay attached when parts
move, while intermediate points remain in bench coordinates.

Appearance changes preserve nets, time and measurements. **Reconnect jumper**
changes named endpoints and restarts simulation; invalid endpoints leave the old
wire intact. **Remove selected jumper** removes the connection it alone built.
Undo restores both wiring and appearance. Crossings are never junctions.
Native validation also rejects duplicate/reversed imported jumpers, endpoints in
different nets, malformed paths and multiple direct wires in one breadboard hole.
It does not yet infer component-leg occupancy in imported geometry.

Try [RGB indicator bench](../../examples/rgb-indicator-bench/README.md) for three
resistor-fed LED dies controlled by physical buttons. Current limiting, polarity
and an actual return path determine light. RGB color and brightness follow engine
outputs in 2D and 3D; the generic model's color mixing is illustrative, without
spectral calibration, thermal damage or perceived PWM averaging.

## Physical insertion previews

Select a part in **Parts & wiring** and type a hole for its first leg. The
preview lists every pin and its proposed hole, or marks it unseated. Green and
red rings on the 3D bench show available and occupied target holes. The rings
are viewing aids drawn over the circuit, not physical contacts. **Plug in** stays
disabled when another leg or jumper already occupies a target. A different hole
on the same conductive strip remains usable. Pointer dragging uses the same
per-leg proposal, shows the occupied owner and returns a rejected drop to its
original position. Invalid placement cannot partially change the project or nets.

Typed and pointer jumper creation also reject an occupied breadboard hole; use
another hole of its strip. Component-pin endpoints remain abstract lead
connections, not a simulation of a particular clip or connector housing. Existing
imported conflicts are not certified or automatically repaired by these guards.

**Bench placement** edits the selected part's physical X/Y coordinates in
millimetres and its quarter-turn rotation. These are separate from the schematic
placement in the main inspector. An empty/invalid coordinate cannot apply. Moving
or rotating a breadboard carries its inserted parts, retaining their pin-to-hole
assignments; the whole edit has one undo. Breadboard translation previews carry
the inserted bodies too; connected jumper curves update when the edit commits.
Raised development-board header sockets are not automatically plugged into holes
merely because their top-down positions overlap a breadboard.

These are workbench authoring guards derived from the catalog's geometry. They
are not native loader rules, a body/lead collision solver, connector mating
validation or a manufacturer qualification. Partial insertion remains possible and
is shown explicitly. The simulator's net graph remains the source of electrical
truth. For a physical breadboard introduction see
[Adafruit's guide](https://learn.adafruit.com/breadboards-for-beginners).

## Surface-mount packages

BSS138 and BAT54 use SOT-23 bodies; LM1117IMP-3.3 uses SOT-223, including its
output tab. Their canonical circuit terminals retain the existing names and
numbers. The list explains that these parts need a carrier for breadboard use;
placing one above a breadboard does not silently connect it. The SOT-223 tab is
visible but not separately wireable: OUT2 represents its shared conductor.
BAT54 uses a reference pin map and representative family outline; its manufacturer
review remains open. See [package geometry evidence](../engineering/package-geometry.md)
for sources, dimensions and remaining limits.

## Rendering recovery and remaining boundaries

If 3D initialization fails or its WebGL context is lost, the panel switches to 2D
and explains the fallback. The list remains available for editing. Choose **3D**
to retry. 2D is a flat artwork view, not a reproduction of physical bench placement.

This milestone does not qualify every catalog package, import GLB/STEP assets,
validate mechanical collisions or establish 60/120 Hz performance budgets. Labels
have visibility modes, not a complete 3D occlusion/collision solver. Pin targeting
uses the catalog's existing contact positions; a wrong catalog position still
needs correction against manufacturer evidence. Generic lead anchors may sit inside
the owning body; occlusion rejection excludes other components and is not a
mechanical contact-accessibility check. Instrument loading and connector
compatibility follow their separate model fidelity, not the quality of the render.

If the 3D files cannot be loaded, the bench switches to 2D and keeps the keyboard
editor available. **Reload to retry 3D** starts a fresh page request; check the
connection if it fails again. This differs from a graphics-context failure after
loading, where selecting **3D** retries the renderer without reloading the page.

## Source-checked named indicators

WP7113ID and WP154A4SUREQBFZGC have a **Nominal DC envelope** card in the
selected-part inspector and the library's Models tab. It states the 25 °C,
2–20 mA source-curve range, voltage tolerance and separate holdout points; expand
Source snapshot for the reviewed revision and hash. This is bounded model
evidence, not a manufacturer certification or calibrated optical/thermal model.
The RGB package uses R–COM–B–G at 1.27 mm pitch; check its pin table before wiring.
Try the [named RGB driver bench](../../examples/qualified-rgb-driver/README.md)
for actual resistor-limited channel loading and physical switches.
