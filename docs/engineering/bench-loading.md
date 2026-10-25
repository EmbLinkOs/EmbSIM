# Deferred bench renderer

The workbench uses `@embsim/bench/model` for geometry descriptions, placement,
contacts and connectivity. This entry exports renderer types without loading
Three.js or constructing GPU materials. `@embsim/bench/renderer` loads only when
a 3D bench is requested. The existing root entry remains available for consumers
that intentionally need the full package; it retains its runtime exports.

During loading the part list and project editor remain mounted. Leaving the view
or choosing 2D ignores a late import result. A failed asset request selects the
flat view and explains recovery. The reload action creates a fresh document,
because browsers can cache failed module fetches within an existing document.
After a successful download, the existing graphics initialization/context-loss
fallback still supports in-place retry. These are client viewing states; neither
loading nor recovery changes engine connectivity, project edits or virtual time.

## Build measurements

Measured 2026-10-25 on Linux, Node 24.19.0, pinned pnpm 10.28.0 and Vite 7.3.7,
with production builds of main `896e4f0` and the workspace-layout change. Sizes use Vite's
printed decimal kB. Gzip figures are build estimates; actual transfer compression
is the host's responsibility. Source maps are separate from these JavaScript sizes.

| JavaScript artifact | Before | After |
|---|---:|---:|
| Initial application entry | 1,550.42 kB | 909.17 kB |
| Initial entry, estimated gzip | 466.36 kB | 300.17 kB |
| Deferred 3D chunk | bundled in entry | 640.97 kB |
| Deferred 3D chunk, estimated gzip | bundled in entry | 166.35 kB |

The initial entry is about 41% smaller (about 36% in estimated gzip). Opening 3D
still downloads the geometry code. This does not establish a wall-clock startup
speedup, a memory budget or ADR-028's 60/120 Hz frame-time targets; that reference
hardware harness remains outstanding. No dependency, renderer quality or engine
fidelity changes are made to achieve this split.

## Regression evidence

`e2e/bench-loading.spec.ts` intercepts the built renderer chunk. It checks that
ordinary schematic use does not request it; delays its response while selecting
a part through the list, preserves a later choice of 2D, and subsequently opens
3D; and rejects the request, exercises the surviving 2D
editor, then reloads and opens a working 3D scene. Existing bench workflows check
context-loss recovery, wiring, insertion, controls and undo after the split.
