# Visual Benchmark v0.1

The M6 redesign of the workbench: a professional electronics and embedded
simulation tool first, the simulation at the centre of every perspective,
with settings and an interface in English and French. This page is the
account: what was wrong, what changed, the design system, the default
workspaces, how it was verified, and what is not done. The screenshots of
the same twelve workflows before and after are in
[the gallery](../gallery/README.md) (`visual-benchmark-v0.1/before`,
`after`, `after-fr`).

## What was wrong (the review, before any change)

From the twelve `before` screenshots and the source:

- **State by colour alone.** Running or paused was a green or amber tint on
  the clock; stopped, failed, loading and a lost engine had no name at all.
- **Simulated and host time mixed.** One clock read `1.546 s 0.94×` with no
  word saying which number was the simulation's and which the host's.
- **A lost engine looked like an empty workbench.** The perspective stayed,
  greyed, with no explanation and no way back.
- **The status bar led with transport details** (address, protocol, frames)
  and buried the simulation's state and the diagnostics.
- **Inconsistent chrome.** Five header heights, uppercase labels in some
  panels and sentence case in others, three radii, two title styles,
  rounded "cards" on some panels only.
- **Repeated titles.** Verify's panes repeated their panel's title as a
  heading; the Design ribbon repeated "Schematic" above a canvas titled
  "Schematic".
- **Wasted space.** The register panel spread name, hex and decimal across
  its full width; the library's rows were 56 px tall.
- **Overflow at small sizes.** At 768 px the Design ribbon's minimum width
  pushed the page wider than the window; at 900 px the fidelity select fell
  off the simulation bar.
- **No settings, one language.** The theme toggle was the only preference,
  every string was written into the components in English.

## What changed

### The shell

- **Header in two rows.** The app bar (40 px): project menu, the four
  perspectives as a radio group with their shortcut in the tooltip,
  diagnostics, Settings, the command palette. The simulation bar (38 px):
  run/pause, step, advance by an interval, reset; the readout; speed and
  fidelity; undo/redo in Design. Under 1000 px the simulation bar wraps to
  a second line rather than hide a control; under 1180 px the perspective
  labels give way to their icons (their accessible names stay).
- **The readout.** A state chip that names the state in words and an icon
  (running ▷, paused ‖, stopped □, loading ◌ spinning, failed ⊗,
  disconnected ⏻), with a tooltip saying what it means; then **Simulated**
  and the simulated time (the exact picoseconds in the tooltip); then
  **Host** and the measured rate of simulated to host time, or "—" when it
  is not measured (stopped, paused). Nothing is estimated: the rate comes
  from the engine's own clock reports over the last two seconds.
- **The state is derived in one place** (`shell/simState.ts`): the
  connection's status, whether the engine runs, its clock, and the reason
  its last run ended (a reason outside the normal ones, such as a fault or
  a watchdog, is *failed*).
- **The status bar** leads with the state chip and simulated time, then the
  project and each board's timing tier, then the error and warning counts
  (a button that opens the diagnostics), the engine connection last with
  its details in a tooltip. On a narrow window it scrolls (focus it, then
  the arrow keys) rather than clip.
- **The connection screen** replaces the perspective while connecting, when
  the engine is gone (what happened, the address and the simulated time
  reached, Reconnect, how to start it again) and when the connection could
  not be made (the error in a disclosure).
- **Settings** (Ctrl+, or the gear): see below.

### The perspectives

- Every panel title in sentence case at one size; panels at one radius and
  border; the Design ribbon flattened into a 32 px tool strip whose tools
  scroll and whose identity (parts and nets count) folds away on a narrow
  canvas; the library's rows compact (30 px symbols, the Place button
  quiet until hovered); controls as rows; meters with their identifier in
  the mono face and the measured value in the measured colour.
- Debug's registers read as one row each: name, hex (editable, its own
  background on hover), decimal right-aligned; a changed register is bold
  as well as coloured.
- Verify's panes no longer repeat their panel's title.

### Values and states

| Kind | How it shows |
| --- | --- |
| Editable value | an input or select on `--value-editable-bg`, with a border |
| Measured value | mono face, `--value-measured` |
| Unavailable | `--value-unavailable`, "—", or the control disabled with its reason in its tooltip |
| Warning | amber with the warning triangle and a word |
| Error | red with the ⊗ icon and a word |

Colour never carries a meaning alone: each state has an icon and a word,
each changed register is bold, each warning and error count has its icon.

## The design system

Tokens are in `ui/packages/design/src/tokens.css`, defined for the dark
theme, the light theme and the system's light scheme:

- **Type.** `--font-ui` (the system UI face) for the interface,
  `--font-mono` for every identifier and measured value; `--text-xs` to
  `--text-lg` scale with `--ui-scale`.
- **Scale and density.** `--ui-scale` is 0.92, 1 or 1.12 (Settings → text
  size); it drives the text sizes, the control heights, the panel header
  (30 px at 1) and the status bar. Compact density shortens rows
  (`--row-height`, `--row-pad-y`) and gutters (`--gutter`).
- **Space.** `--space-1` to `--space-6` on a 4 px base.
- **Radii.** `--radius-sm` for panels, controls and chips;
  `--radius-md` for popovers and dialogs.
- **Colour.** Surfaces (`--surface`, `-raised`, `-sunken`, `-overlay`),
  lines (`--line`, `-strong`, `-focus`), text (`--text`, `-muted`), the
  accent, and the semantic sets: `--state-running|paused|stopped|loading|
  failed|disconnected` with their `-bg`, and `--value-measured`,
  `--value-editable-bg`, `--value-unavailable`.
- **Selection.** The accent at 18 % behind a selected row; the focus ring
  (`--focus-ring`) on every focusable element.
- **Icons.** Lucide at 14–16 px, `aria-hidden` beside a word.
- **Motion.** The running chip pulses, loading spins; both stop under
  `prefers-reduced-motion`.

The consolidation layer at the end of `apps/workbench/src/styles/app.css`
brings the older panels onto these tokens.

## The default workspaces

| Perspective | Left | Centre | Right / bottom |
| --- | --- | --- | --- |
| Design | Library (collapsible) | Schematic or Bench, the ribbon above | Inspector (a rail until something is selected); Objects, Checks, Nets, World, Layout Studio below |
| Run | Controls and instruments (wired instruments, voltage comparison, controls, world) | Live schematic, oscilloscope, logic analyzer | Meters, Serial, Air, Diagnostics below; front panels docked or floating |
| Debug | Source, disassembly, console | | Registers, breakpoints, memory, peripherals |
| Verify | Scenario editor, report | | Recordings, coverage |

Each perspective's panel sizes, order and folds are saved per project;
Settings → Reset layout returns every perspective to these.

## Settings

`stores/settings.ts`, kept in `localStorage` (`embsim.settings`) and
applied to the document at once:

| Category | Setting | Effect |
| --- | --- | --- |
| General | Interface language | English or Français; at once |
| Appearance | Theme | System, light, dark |
| | Text size | Small, medium, large (`data-text-size`) |
| | Density | Comfortable, compact (`data-density`) |
| Workspace | Restore panel layout | Off: the next start opens the default layout (the page says it takes effect at the next start) |
| | Reset layout | Clears every saved arrangement now |
| Simulation | Time unit | Automatic (the largest that fits), s, ms, µs, ns |
| | Time precision | 0 to 6 decimals; a clock truncates, never shows a time not reached |
| | Host rate | As a factor (0.94×) or a percentage (94 %) |

A preview shows the simulated time and host rate as set. Only settings
that act are offered.

## Languages

- **Catalogs.** `apps/workbench/src/i18n/en.ts` is the source: every
  message and its key, typed (`MessageKey`). `fr.ts` is a complete
  `Record<MessageKey, string>`; the compiler refuses a missing key and
  `i18n.test.ts` checks parity, placeholders, emptiness and plural pairs.
- **Use.** `useT()` in components, `t()` outside them; `{name}`
  parameters; `.one` / `.other` by `Intl.PluralRules`; numbers by
  `Intl.NumberFormat` (French decimal commas and a space before %).
- **Shared packages** (canvas, instruments, dock, command palette,
  dialogs) take their words through `uiText(key, english)` from
  `@embsim/design`; the application hands them its catalog's `pkg.*`
  messages.
- **Fallback.** A key a catalog lacks shows the English.
- **Remembered** in the settings; the first start follows the browser's
  language.
- **Not translated, on purpose:** register, peripheral, pin, net and
  signal names, part numbers, units, protocol names, code, the engine's
  diagnostic messages (they come from the engine), catalog descriptions and
  fidelity notes (catalog data), and the instruments' front-panel legends
  (as printed on the real DS1054Z, PPK2 and Logic 8).
- **Longer words.** French runs about 20 % longer; labels wrap or give
  way to icons rather than clip. `settings-language.spec.ts` checks every
  perspective in French at 1280 px for horizontal overflow, and the
  `after-fr` screenshots show 1600 px and 1280 px.
- **More languages** are not offered: a language is added when it can be
  delivered complete and reviewed, as English and French are.

## How it was verified

- `pnpm typecheck`, `lint` and `test` (370 unit tests, including axe on
  the Design and Run perspectives and the catalog checks) pass.
- The Playwright suite passes in Chromium, with the specs updated for the
  new header (state by `data-state`, Settings instead of the theme toggle,
  capitalised landmark names) and a new `e2e/settings-language.spec.ts`:
  text size, density, time unit and precision applied at once and kept
  over a reload, Reset layout, Restore defaults; the workbench switched to
  French, its perspectives, transport, state and status read in French, a
  run started and paused, no horizontal overflow in any perspective, the
  language kept over a reload, and back to English.
- `workspace-layout.spec.ts` checks every control reachable at 1440, 900
  and 768 px and the side panels at 900 and 768 px.
- The `before`, `after` and `after-fr` screenshots, the same workflows on
  the same examples.
- A real bug found on the way: a pointer going down on a pin's target near
  its edge fell through to the sheet (the pin was looked up by distance,
  not by the element); macOS's and Windows's fonts moved the pin label's
  box enough to show it. The canvas now takes the pin under the pointer.

## Limitations

- The 3D bench's own controls are translated; its renderer's error texts
  from three.js are not.
- Catalog descriptions, fidelity notes and the engine's diagnostics are in
  English (they are data from the catalog and the engine, not the
  interface).
- Compact density changes rows and gutters; it does not yet change the
  canvas or the instruments' plots.
- Floating front panels keep their own sizes when the text size changes;
  resize them by hand.
- Panels are resizable, collapsible and reorderable within their
  perspective and the front panels float; panels do not yet move between
  perspectives.
- The `bench-tools` measurement spec is timing-sensitive under a loaded
  machine running several workers; it passes alone and on CI's runners.
