# Schematic Studio

The Design perspective combines the component library, schematic canvas,
inspector and Layout Studio. The schematic ribbon keeps layout, arrangement,
focus, search and display controls together. Tools work in light and dark themes.

## Connect first, then arrange

1. Place parts from the library. Placement chooses nearby free space rather than
   covering an existing symbol. Drag a part to choose its approximate position.
2. Connect pins with the Wire tool, or activate a pin with Enter and then activate
   the destination pin. Obstacle-aware wiring previews a clear orthogonal path.
   A red preview indicates that no clear route was found. Completing that
   connection still joins the pins electrically and leaves a visible airwire
   with a warning, rather than drawing through another component.
3. Open **Auto layout**, choose Whole sheet or Selection, Left to right or Top
   to bottom, and Comfortable or Compact spacing. Choose **Preview layout**.
4. Review the proposed schematic, moved-part and route counts, and any routing
   notes. The preview has not changed the project. **Apply layout** commits the
   geometry as one undoable edit; **Discard preview** restores the current view.
5. Save the project using the workbench's existing Save command.

For automatic arrangement as you build, enable **View → Auto arrange after
connecting**. Each connection arranges the components on the joined net and
reroutes their affected nets. The connection and its placement share one Undo.
This option starts off so existing manually arranged circuits remain predictable.

**Route only** redraws connections without moving parts. Selection scope affects
nets incident to the selected components; the other components remain fixed.
A full-sheet route replaces manual wire geometry, so use the preview to review it.
Existing labels and bus drawings keep their positions.

## Precise arrangement

Select two or more components to enable **Arrange**: align left, right, top,
bottom, horizontal centers or vertical centers. Three or more enable horizontal
and vertical distribution with equal edge gaps. Arrangement is previewed before
application and reroutes affected nets. Alignment can intentionally place symbols
on the same line; inspect the result for overlaps before applying it.

Dragging shows magnetic alignment guides at other parts' edges and centers.
The committed position uses the same snapped delta as the preview. Turn the
magnetic guides off in View when you want only ordinary grid snapping.
Obstacle-aware wires follow moved parts and are regenerated after rotation or
mirroring. If a path cannot be found, the connection remains an electrical net
and its missing drawn path is shown as an airwire when airwires are enabled.

## Navigate and inspect

- **Find** searches component IDs, component names, board IDs, net names and pin
  membership. Enter focuses the first result; every result is keyboard accessible.
- **Focus** frames selected components or the selected net. Press **F** on the
  canvas to focus selected components; **0** fits the full schematic.
- Finding a net spotlights its wires. Click its ribbon badge to clear the spotlight.
- The canvas counts undrawn connections; airwires also reveal separated drawn
  clusters, even when every pin already touches some wire.
- The **Navigator** shows component positions, wires and the current viewport.
  Click the overview to recenter, or use its keyboard-accessible Fit sheet button.
- **View → Focus canvas** hides side panels for more drawing space. Restore them
  from the same menu; the command palette also exposes this action.
- **View** configures the grid and toggles airwires, navigator, magnetic guides,
  obstacle-aware wiring and automatic arrangement after connecting.
- Existing pan, zoom, Shift-drag selection, copy/paste, duplicate, rotate, mirror,
  net labels, buses and hierarchy remain available.

## Backgrounds and grids

Open **View** to customize the drafting surface. Choose Engineering for major/minor
grid lines, Lines for a uniform grid, Dots, Crosses, or Plain. Paper, Warm, Graphite
and Blueprint palettes each adapt to light and dark themes. Change grid spacing,
line/dot size and contrast to suit your display; **Reset appearance** restores the
defaults. Native radio, select and range controls support keyboard navigation.

The background is continuous across the workspace, including past the old sheet
edge. Patterns follow pan and zoom; coarser subdivisions keep zoomed-out grids
legible. These are visual preferences: editing snap remains at 10 units. Settings
are saved on this device, survive reloads, and also apply to inside views and the
Run schematic. They do not change the project or its electrical connections.

The native server supports document/asset requests while its WebSocket is active,
so a browser reload can restore preferences without waiting for the old page to
release its session. It still accepts only one session client at a time.

## What layout guarantees

Preview planning runs in a separate worker. It preserves IDs, component values,
firmware, electrical net membership, rotations and mirrors. Applying a preview
accepts only component/board positions and wire geometry from the planner.
Layout-only transactions preserve the running simulation. If the project changes
while a preview exists, Apply is disabled until a fresh preview is generated.

Placement uses deterministic connectivity groups with fixed obstacles outside the
selection. This is a geometric heuristic, not electrical signal-direction analysis.
Shared named supply rails do not merge existing signal groups; supply-only
symbols are attached to a connected owner where possible. Routing uses exact
transformed pin anchors, five units of body clearance, outward pin leads, a
bend penalty, and a bounded orthogonal visibility-grid search. Every failed or
unknown-anchor route is reported; it is never replaced by a fabricated connection.

The router reserves previously drawn foreign-net tracks against collinear
overlap; perpendicular crossings remain allowed. It does not globally optimize
crossings or label positions. Review complex schematics, especially dense or unusual custom symbols.
Drawn crossings do not change electrical connectivity; the project's named nets
are authoritative. These tools improve authoring but do not qualify circuit models.

## Regression evidence

Algorithm tests cover deterministic placement, fixed parts, transformed and
unsnapped anchors, obstacle avoidance, missing/blocked anchors, arrangement,
free-space placement and unchanged circuit data. The transaction-boundary test
rejects accidental planner changes to firmware, values or nets.

The real-engine browser regression exercises Preview without edits, Apply through
layout-only transactions, whole-operation Undo/Redo, finder and view controls,
connect-then-arrange, both themes and the supported compact viewport:

```sh
pnpm --dir ui build
python3 tests/browser/test_schematic.py --embsim build/dev/bin/embsim \
  --chromium /usr/bin/chromium --screenshots /tmp/embsim-schematic
```

Python Playwright and a browser are required for this optional native-engine check.
Platform CI separately covers the workbench unit and standard smoke tests.


The appearance-specific browser regression additionally requires Pillow and checks
all patterns/palettes, keyboard controls, reload persistence, reset, compact layout,
and pixel uniformity across a viewport panned beyond the old sheet edge:

```sh
python3 tests/browser/test_canvas_appearance.py --embsim build/dev/bin/embsim \
  --chromium /usr/bin/chromium --screenshots /tmp/embsim-canvas
```

## Deliberate wire bends

Start at a pin (or a wire in Wire mode), then click empty sheet space to commit
an orthogonal bend. Continue adding bends and finish at a pin or an existing wire.
The visible route is retained when completed: smart routing and automatic
arrangement do not replace a route with explicitly placed bends.

While drawing, **Space** flips the live elbow, **Backspace/Delete** removes the
last committed bend, and **Escape** cancels without modifying the project.
Arrow keys move the routing cursor by one editing grid step (Shift: five steps);
**Enter** on the canvas commits a bend or connects at the cursor. You can also
Tab to a destination pin and press Enter. Bend markers show committed waypoints.
The completed connection is one undoable edit; unfinished bends create no history.

Manual routes give you geometric control and can cross bodies or other nets;
crossings do not by themselves create electrical connections. This is not a
clearance checker. Later automatic rerouting or layout operations can replace
manual geometry; waypoints are not persistent routing constraints.
