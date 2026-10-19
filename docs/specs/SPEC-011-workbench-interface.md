# SPEC-011 - Workbench Interface Specification

**Status:** Accepted 2026-10-15 for the perspectives, design system and
checklist; panel details may be refined before M2. **Builds on:** 05 §3;
ADR-008. **Requirements:** `docs/requirements/UI.md`.

---

## 1. Application structure

- `ui/` is a pnpm workspace: `packages/protocol` (typed client for SPEC-008,
  generated from a method table), `packages/design` (tokens, CSS, base
  components), `packages/scene` (schematic and board rendering, hit-testing,
  wiring), `packages/instruments` (oscilloscope, logic analyzer, terminal,
  decoders), `apps/workbench` (the application: shell, perspectives, state).
- State: one store per domain (session, project, selection, layout,
  instruments); the simulation state in the store is a mirror of
  `observe.update` notifications, never computed locally.
- Platform adapter: `platform.ts` with `open/save file`, `menus`,
  `start sidecar`, `clipboard`; implementations for Tauri and browser.

## 2. Perspectives and panels

| Perspective | Panels (dockable) | Required actions |
|---|---|---|
| Design | Schematic canvas; Library (search, categories, fidelity badges, drag to place); Inspector (parameters with units, pin models, fidelity, provenance); Board view; Nets list | place, move, rotate, mirror, delete, undo/redo, wire by drag from pin to pin or net label, rename net, set parameter, choose board, assign firmware, validate |
| Run | Board view with hotspots; Controls (inputs list: buttons, switches, pots, serial); Oscilloscope; Logic analyzer; Serial terminal; Meters; Diagnostics | run, pause, step, advance, reset, speed; press, hold, toggle, drag; add probe by clicking a wire or pin; cursors and measurements; export CSV/VCD |
| Debug | Source; Disassembly; Registers; Memory; Peripherals (register and field decode, writable); Breakpoints; Call stack; Variables; Console | step into/over/out, continue, break, set breakpoints in source, watch variable, edit register |
| Verify | Scenario editor (inputs timeline, assertions); Recordings; Reports; Coverage | record, stop, replay, convert to scenario, run scenario, view report |

Global: toolbar with transport controls, virtual clock (ps with unit
formatting and a raw picosecond tooltip), policy selector, diagnostics badge,
theme toggle; command palette; status bar with engine version, timing tier
of each board, protocol latency.

### Component discovery

The Library groups categories with counts and keeps unknown/custom categories
browsable. Multiword search intersects with manufacturer, package, fidelity-tier
and named-part filters. Clear filters recovers empty results. Selecting Preview
must not mutate the project: Overview, Pins and Models expose identity, controls,
numbered terminals and model omissions before placement. Source review is shown
separately from electrical qualification. Place and drag require a model for the
current project policy and a non-broken entry. All controls support keyboard use.

After a circuit transaction reports `restarted: true`, the client reloads runtime
inputs/probes, clears obsolete records and restores waveform subscriptions. A
layout-only transaction must preserve the run and its accumulated measurements.

### Scenario draft validation

Run and Save are disabled when the visible draft contains invalid expectation
JSON, duplicate or invalid assertion IDs, invalid timing, reversed windows,
missing input/probe identifiers or invalid expectation fields. Times are
non-negative and cannot exceed the scenario duration. A visible error summary
explains corrections. Editing clears assertion badges from the previous run.
Invalid JSON remains visible for correction and must never silently execute the
previous valid expectation. The engine remains authoritative for whether target,
input and probe identifiers resolve in the project and for model-specific values.

### Schematic Studio

The schematic ribbon provides connectivity-driven placement, route-only previews,
alignment/distribution, component/net search, selection focus and canvas display
options. Placement supports both axes, compact/comfortable spacing, whole-sheet
and selected-component scopes. Unselected components are fixed obstacles. Existing
rotations and mirrors are preserved. Preview must not send edits; applying a plan
uses one undoable transaction containing presentation fields only. A stale preview
cannot be applied. Route failures remain visible as unresolved geometry; electrical
nets remain authoritative. Planning runs in a bounded worker task.

An opt-in automatic arrangement mode combines a new connection with placement of
its joined-net components and routing of affected nets in one undoable edit.
Obstacle-aware wiring previews paths, including a blocked-path indication. If no
clear path is found, completing a connection joins its pins with an undrawn
airwire and an explicit warning. Smart wiring
reroutes incident nets after movement. Rotation/mirroring reroute incident nets.
Magnetic drag guides use the same grid-compatible delta for preview and commit.

Find searches parts, boards, nets and pin membership. A selected net spotlights its
wires. F focuses selected parts; 0 fits the sheet. A navigator depicts the viewport
and supports recentering plus a keyboard-accessible fit action. Grid, airwires,
navigator, side-panel focus mode, magnetic guides and smart-wiring preferences are controllable in View.
The [user guide](../user/schematic-studio.md) records the heuristic boundaries.

### Canvas appearance

The background covers the entire viewport, including negative world coordinates,
without exposing the finite sheet bounds. Patterns remain anchored to world zero
while panning and zooming. Engineering major/minor lines, simple lines, dots,
crosses and a plain background are selectable. Paper, warm, graphite and blueprint
surfaces adapt to light/dark themes. Visual spacing (10/20/40 units), line/dot
size and grid contrast are adjustable; zoom-out uses coarser subdivisions to
avoid subpixel noise. These appearance settings do not alter editing snap.

Appearance preferences are validated and persisted locally, shared by Design,
inside views and the Run schematic, with a reset action. Storage failure must
not prevent changes in the current session. Controls have native keyboard
semantics and visible focus indicators. The style uses restrained system fonts,
neutral canvas surfaces, rounded chrome and subtle shadows.

## 3. Scene rendering

- SVG scene graph: a viewport group with pan and zoom (wheel, pinch, keys);
  layers: sheet grid, wires, components (symbols from the catalog), labels,
  selection, probes. Virtualisation: components outside the viewport are not
  mounted; 500 components at 60 Hz pan is the budget.
- Wires are orthogonal polylines snapped to the 10-unit grid; junction dots
  at T-connections; net colour by state in Run (low, high, floating,
  contention, analog gradient by voltage with a legend); labels for named
  nets; hovering a wire shows net name, island kind, state and voltage.
- The bench (ADR-027): the Design perspective's canvas shows the schematic
  or the bench (a switch over the canvas, kept per user), and the Run
  perspective's Board panel the bench, live. The bench is a 3D scene
  (Three.js, `packages/bench`) in millimetres: a desk (an anti-static mat
  with a 10 mm grid), lit from above with soft shadows; each board, part
  and breadboard as its body: the board's PCB with its artwork as its face,
  its `bench.features` (SPEC-006 §3) and its headers; a part built from its
  package (SPEC-005 §8.1): a resistor's colour bands read from its
  resistance, an LED's dome with its flat, a TO-92, a TO-220, a DIP with
  its notch, a tactile button, a potentiometer's knob, modules as their
  PCB on their header with their artwork as their face (live: an LCD's
  characters), sources and instruments as boxes with binding posts. The
  camera orbits (right drag), pans (middle drag) and zooms (wheel); F
  frames the bench. The Move tool drags a part on the desk; a leg within
  half a pitch of a hole snaps the part so that leg is in it, the holes
  the legs would plug into are marked, and the release plugs it in
  (SPEC-007 §2); R turns the selected part, the arrows nudge it a pitch,
  Delete removes it; a breadboard carries what is plugged into it. The
  Jumper tool presses on a hole or a pin and releases on another: a
  jumper in the chosen colour, flat between two holes of one breadboard
  (a pre-cut jumper) and an arcing Dupont wire elsewhere. Connections the
  nets ask for that nothing on the bench makes yet are dashed links, and
  Build turns them into jumpers. While running, a part's controls act
  under the pointer (a button's cap held, a switch flipped, a knob turned
  by dragging, an artwork's hotspots), LEDs light from their luminance
  (brightness as the eye sees it, the cube root of the duty, with a glow),
  and frames are drawn only when something changes. Notes are yellow
  callouts placed on the bench, pointing at a part. Every bench action has
  a list equivalent beside the scene (each part with the holes its legs
  are in; plugging the selected part's first leg into a named hole,
  turning it, lifting it off; a jumper between two named terminals), and
  without WebGL the bench is the flat view below.
- Bench inspection and wiring: projected terminal positions define an 11 CSS-pixel
  hit radius, with clipped/covered contacts excluded and near-equal neighboring
  targets rejected with a zoom-in hint. A screen-space contact ring and named
  source/destination feedback identify the proposed connection. Plugged legs share
  their breadboard hole's picking target. Unknown typed endpoints are rejected
  before mutation; duplicate/reversed jumpers do not create another wire.
  Escape, pointer cancellation/capture loss, focus loss, hidden documents and
  tool/edit/live-mode changes cancel pending wiring and roll back uncommitted
  dragging; interrupted holds send a release. A top camera, selection focus,
  paper/graphite/mat surfaces, label visibility and notes are viewing aids;
  appearance/list preferences persist locally without changing electrical data.
  The keyboard part/wiring list can fold explicitly and stacks below narrow
  scenes. Geometry source labels distinguish catalog/artwork/approximate bodies
  without implying qualification. Board geometry and contact heights share declared
  thickness, standoff and header style. Renderer initialization/context failure
  selects the 2D fallback with an explanation and an explicit 3D retry.
- The Design perspective's Inspector is contextual. It is a rail while
  nothing is selected, so the canvas takes its width, and it opens on a
  selection. Folding or unfolding it by hand overrides this, and the choice
  is kept. With the library open and the Inspector and parts list folded,
  the bench takes about 73 % of a 1440-pixel-wide window; the visual
  benchmark's target is 60 % (`docs/research/visual-workbench-benchmark.md`).

- Physical assembly authoring: a pure per-leg preview identifies available,
  occupied and unseated positions. Pointer and keyboard construction use the same
  occupancy model: a component leg or direct-hole jumper reserves a hole, while
  another hole on the same strip remains available. Invalid insertion is rejected
  before project mutation; imported old conflicts are not certified or silently
  repaired. Preview rings and text show the owner of occupied contacts. Numeric
  bench placement edits millimetres and quarter turns, independently of schematic
  coordinates; empty/non-finite coordinates cannot apply. Breadboard translation
  and rotation carry inserted parts by one rigid transform, preserving hole IDs
  and nets in one transaction. Raised board headers are not inferred to be
  inserted into a breadboard. These workbench guards are not native-loader rules
  or a complete mechanical/connector solver; partial insertion remains explicit.
- The World panel (Design, under the canvas; Run, beside the controls)
  edits the project's world (ADR-026): each field with whether the room
  declares it, a slider in its unit (light on a logarithmic scale) and the
  sensors that read it, the value now when a scenario has changed it; and
  the bodies with their place in millimetres, added as a box, a wall or a
  ball, removed, or dragged on the bench in the Move tool. A sensor's beam
  is drawn on the bench to what it meets, with the distance. The Controls
  panel marks an input that reads the world and one set on the part.

- The flat bench (the 2D view). The parts with artwork sit in rows above the
  boards, each labelled with its id and name; the project's nets are wires
  between their pins (each artwork's `pin-<name>` elements, SPEC-005 §8):
  ground black, supplies red, signals in a palette, each new end joined to
  the nearest end already wired, square runs that go round a part they
  would cross; hotspots stay live. Header pins are wirable from the
  schematic through the board's symbol (a generated rectangle with ports
  on its sides). The toolbar's clock shows, while running, virtual time
  against wall time over the last two seconds (a percentage of real time,
  or a multiple above 10x).
- Buses: a project's `layout.buses` drawn as thick lines with their names
  and, while running, their values (SPEC-007).
- Hierarchy: double-clicking a board or a composite part (or "Open inside"
  in the inspector) shows its inside on a read-only sheet: its parts on a
  grid, a board's MCU as a block of the pins its nets use, each connected
  pin with a stub and its net's label, live while running (a net joined to
  the project through a port shows the project net's state). "Back to the
  project" returns.
- Waveforms on canvas (2D with WebGL acceleration where available): time
  axis shared across instruments, cursors, zoom with the wheel, auto-range,
  min/max decimation for long records, decoded protocol annotations.

## 4. Design system

Tokens in `packages/design/tokens.css`: colour (surface, surface-raised,
line, text, text-muted, accent, signal-low, signal-high, signal-z,
signal-x, analog), typography (UI sans, mono for identifiers, values,
time), spacing (4-unit scale), radius, elevation, motion (durations and an
`prefers-reduced-motion` override). Themes: dark (navy surfaces, mint accent,
from the foundation) and light; both WCAG AA for text and UI components.
Components: button, icon button, toggle, segmented control, input with unit,
select, slider, tabs, tree, table, panel with header, toast, dialog,
tooltip, command palette, dock layout. All built on accessible primitives
with keyboard and ARIA behaviour.

## 5. Interaction rules

- Every action is reachable by keyboard; focus order is logical; focus does
  not move when observations update.
- Pointer gestures on hotspots: `hold` (press on down, release on up, also
  Space when focused), `toggle` (click), `drag` (an axis with a value
  readout; arrow keys step).
- Simulation observations update at a capped rate (default 30 Hz for state,
  instruments stream faster); heavy updates never block control requests.
- Errors and diagnostics show at the affected element (a wire, a pin, a
  register) with an explanation and a link to the docs code page.
- The virtual clock is displayed with units and never as a JavaScript
  number beyond 2^53: time is handled as `bigint`.

## 6. Checklist (the M2 gate; used from M0)

1. Keyboard-only completion of: place and wire the acceptance circuit; run;
   press the button; add a probe; set a breakpoint.
2. Screen-reader labels on every control and state (buttons announce
   pressed state, LEDs announce on/off, nets announce value).
3. Light and dark themes pass contrast checks (automated axe run in CI).
4. Layout usable at 1024×700 and at 4K with 200% scaling.
5. Reduced-motion honoured; no animation is required to understand state.
6. 500-component schematic pans at 60 Hz on the CI reference machine.
7. Playwright smoke on Linux, macOS and Windows webviews: open project,
   run, press, observe, export.
8. No simulation code in `ui/` (lint rule forbids importing anything but
   the protocol client for state).

### Manual wire authoring

During an unfinished connection, empty-sheet clicks commit orthogonal legs.
Space changes only the live elbow, Backspace/Delete removes the last leg, and
Escape cancels without a project mutation. Arrow keys move the cursor by 10
units (Shift: 50); Enter on the canvas commits a bend or connects at a pin/wire.
Finishing on a pin or wire commits the complete polyline in one transaction.
Explicit bends take precedence over smart routing and auto-arrange at completion;
subsequent layout operations may replace the geometry. Crossings alone do not
create net membership. This workflow does not promise clearance validation.

### Schematic authoring and repair

A selected orthogonal track shall support perpendicular segment reshaping with a
live preview, fixed original endpoints, and preserved same-net pin/branch contacts
using connecting spurs where needed. Pointer release commits one layout-only
transaction; Escape and pointer cancellation discard the gesture. Segment handles
and numeric segment/offset controls support keyboard operation. Reshaping never
changes net membership. Stale source revisions cannot commit.

The inspector shall support snapped coordinates, quarter-turn rotation and mirror
for parts and boards, relative group movement, and one-transaction batch parameter
editing. Batch fields require matching type/unit/range/enum contracts across the
selection; mixed values require deliberate inclusion and a valid new value.
Unchanged placement shall not regenerate manual tracks. Layout edits preserve
firmware and electrical parameters; parameter edits retain native restart behavior.

Contextual actions shall be available by pointer and Shift+F10, keyboard navigable,
viewport bounded, focus restoring and disabled for unavailable mutations. Clearing
drawn geometry shall be separate from disconnecting terminals. Both use ordinary
transactional history. Overlapping mutations shall reject rather than apply a diff
computed from an obsolete snapshot; failed undo/redo shall retain the history item.

A worker-backed structural/drawing report shall identify invalid terminal/net
references, repeated memberships, missing descriptions, small nets, unused pins,
off-grid origins, undrawn connectivity, overlapping symbol bounds, nonorthogonal
geometry, wires through bounds and ambiguous foreign-net contacts. Each result
shall identify its target, severity and scope, support navigation/filtering and
provide net-routing repair where relevant. Reports shall apply only to their
source revision, use a bounded cancellable worker, and surface failures. These
checks shall not be represented as electrical-rule or physical-clearance validation.

## Circuit organization

Design details provides a searchable Objects table for top-level boards and
components, group selection/navigation, flat named-group creation/rename/removal
and per-instance/group placement locks. Canvas outlines derive bounds from member
symbols; keyboard-accessible headers select all members. Locked placement cannot
be directly dragged, rotated, mirrored or numerically repositioned. Arrangement
rejects a selected locked member; automatic placement treats locks as fixed
obstacles. Group/lock changes use normal transactional history, save and reload.
Complete copied component groups remap IDs; new copies are unlocked.

Export parts list uses the platform file adapter for CSV of top-level models,
explicit versions and exact explicit parameter maps, grouped with quantity and
references. It excludes firmware contents and board internals. This inventory is
not a default-resolved or procurement-qualified BOM. CSV quoting and leading
formula escaping apply to all cells. Organization controls respect read-only,
busy, inside-view and layout-preview boundaries.

## Schematic visual language

Idle wires use a configurable theme-aware color distinct from component strokes.
Selected tracks have a separate selection stroke/halo; live state coloring must
not obscure selection. Exposed symbol pins render hollow circular terminals with
larger pointer targets, keyboard activation and hover/focus feedback. Wiring uses
a pencil cursor; editable component bodies use grab/grabbing, respecting locks
and read-only views. Navigation shall not hide terminals or annotations.

Reference/name/primary-value annotations use catalog defaults when no explicit
parameter overrides them, with SI formatting and units. Deterministic candidate
positions minimize intersections with bodies, terminal neighborhoods, prior labels
and track bounding boxes; they do not alter geometry or guarantee an overlap-free
dense sheet. Compact reference/value labels and idle-wire color persist as device
appearance preferences, independent of project and electrical data.
