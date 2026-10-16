# EmbSIM workbench

The graphical client of the session protocol (SPEC-008, SPEC-011). It holds
no simulation: every value it shows comes from the engine through the
protocol, and every action it takes is a protocol request.

## Layout

| Package | Content |
|---|---|
| `packages/protocol` | typed client for every SPEC-008 method, JSON-RPC framing, time as `bigint` picoseconds with unit formatting, the WebSocket transport and an in-memory mock session for development and tests |
| `packages/design` | design tokens (light and dark themes, signal palette, motion with reduced-motion), base components on Radix primitives: buttons, toggles, segmented controls, unit inputs, selects, sliders, tabs, panels, tooltips, dialogs, toasts, command palette, dock layout, status bar |
| `packages/scene` | the schematic canvas (pan, zoom, grid snap, selection, move, rotate, delete, undo and redo, orthogonal wiring, net colouring by live state) and the board view (artwork with hold, toggle and drag hotspots, keyboard equivalents, states bound to outputs) |
| `packages/instruments` | oscilloscope and logic analyzer drawn on canvas with cursors, zoom, decimation and measurements; serial terminal; meters; CSV and VCD export |
| `apps/workbench` | the application: toolbar, command palette (Ctrl/Cmd-K), status bar, the Design, Run, Debug and Verify perspectives, stores mirroring `observe.update`, the platform adapter (browser and Tauri), a component gallery at `#/gallery` |
| `tools/eslint-plugin-embsim` | lint rules keeping simulation out of the client and network access inside `packages/protocol` |

## Scripts

```sh
pnpm install
pnpm dev          # Vite dev server
pnpm typecheck    # every package
pnpm lint
pnpm test         # vitest, including axe checks on the shell
pnpm build        # apps/workbench/dist, served by `embsim serve`
pnpm e2e          # Playwright against a running server (Chromium at /opt/pw-browsers in CI images)
```

## Running against the engine

`embsim serve PROJECT` serves `ui/apps/workbench/dist` and the protocol on
the same loopback origin, so after `pnpm build`:

```sh
./build/dev/bin/embsim serve examples/button-transistor-led/project.json --port 8765
# open http://127.0.0.1:8765/
```

For UI development with hot reload, run the server as above and start
`pnpm dev` with `VITE_EMBSIM_WS=ws://127.0.0.1:8765/ws`. Without a server
the workbench falls back to the mock session and shows a "Demo session"
badge; the mock exists for development and tests, never for results.

## SPEC-011 checklist (M0 state)

| Item | State |
|---|---|
| Keyboard operation, labelled controls | toolbar, palette, library, inspector, board hotspots and instruments are operable and labelled; a full keyboard-only acceptance run is pending |
| Themes and contrast | light and dark; axe runs in `pnpm test` on the Design and Run perspectives |
| 1024×700 to 4K layouts | dock layout adapts; not yet verified at every size |
| Reduced motion | honoured by the tokens |
| 500-component pan at 60 Hz | not yet benchmarked |
| Playwright on three platforms | configured for Chromium; macOS and Windows webviews pending CI |
| No simulation in `ui/` | enforced by the lint plugin |

Debug and Verify are laid out but report "not available" until the MCU
runtime and replay exist; they never show fabricated data.
