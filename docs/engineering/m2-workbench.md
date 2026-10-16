# M2 workbench: what was built and how it was checked

This page records the M2 implementation: the workbench edits real projects,
shows boards, decodes buses and ships as a desktop application. It is the
source for the M2 rows of [STATUS.md](../STATUS.md); update both together.

## What exists

| Area | Files | Contract |
|---|---|---|
| Project transactions | `core/session/edit.c`, `rpc.c` | SPEC-008 §5: edits on a copy of the project; layout-only commits keep the run, circuit changes are checked in a scratch session and rebuild it; `project.save` |
| Editing | `ui/apps/workbench/src/stores/project.ts`, `projectDiff.ts` | place, wire, move, rotate, mirror, delete, copy, paste, duplicate, label, rename, undo, redo, save; each a pure function on the project, sent as its diff |
| Boards on the schematic | `ui/packages/scene/src/boardSymbol.ts` | a block of the board's ports, one connector per side, in pin order |
| Board view | `ui/apps/workbench/src/perspectives/BoardPanel.tsx`, `ui/packages/scene/src/BoardView.tsx` | each board's artwork with its LEDs, buttons and jumpers; a hotspot names the part it acts on |
| Decoders | `ui/packages/instruments/src/decoders.ts` | SPEC-011 §4: UART, SPI, I2C annotations from digital records; a probe's `decode` field |
| Performance | `ui/packages/scene/src/SchematicCanvas.tsx`, `tools/perf/large_project.py`, `ui/apps/workbench/scripts/perf.mjs` | SPEC-011 §6.6: memoised parts and wires, viewport committed when it settles, detail left out while moving |
| Desktop | `desktop/` | ADR-008: the Tauri shell with the engine as sidecar, `--allow-origin` for its webview, its icon |
| CI | `.github/workflows/ci.yml` | the smoke on three platforms, the installers on three platforms, sanitizers |
| Screenshots | `ui/apps/workbench/scripts/screenshots.mjs`, `docs/gallery/` | the same views at every milestone |

## How it was checked

| Check | Where | What it shows |
|---|---|---|
| Transactions | `test_session`: `test_edit_transactions` (also under ASan and UBSan) | layout commits keep time; added, wired, renamed, removed parts rebuild; a failing commit changes nothing; save writes the file |
| Editing against the engine | Chromium against `embsim serve`, by hand | place, wire, undo, redo, duplicate, paste, save reach the project file |
| Decoders | `decoders.test.ts`; the Blue Pill by hand | UART (parity, framing, mid-stream), SPI mode 0, I2C start, address, data, acknowledge, stop; the boot banner frame by frame |
| Performance | `scripts/perf.mjs` on a 500-inverter project | pan and drag 16.8 ms at the 95th percentile, zoom 33 ms, in headless software-rendered Chromium |
| Desktop | Xvfb by hand; CI | the window, the sidecar, the project; installers built per platform |
| Smoke | `e2e/smoke.spec.ts` in CI on Linux, macOS, Windows | open, run, press, observe, export; keyboard place and wire; theme |

## Known gaps

Buses and hierarchical sheets; the Verify perspective; signed installers;
the platform webviews other than WebKitGTK are exercised only by building
the installers, the smoke runs in Chromium.
