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

## 3. Scene rendering

- SVG scene graph: a viewport group with pan and zoom (wheel, pinch, keys);
  layers: sheet grid, wires, components (symbols from the catalog), labels,
  selection, probes. Virtualisation: components outside the viewport are not
  mounted; 500 components at 60 Hz pan is the budget.
- Wires are orthogonal polylines snapped to the 10-unit grid; junction dots
  at T-connections; net colour by state in Run (low, high, floating,
  contention, analog gradient by voltage with a legend); labels for named
  nets; hovering a wire shows net name, island kind, state and voltage.
- Board view: the board artwork with hotspots; header pins are wirable from
  the schematic through the board's symbol (a generated rectangle with
  ports on its sides).
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
