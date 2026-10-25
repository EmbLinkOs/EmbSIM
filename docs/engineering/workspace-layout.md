# Workspace layout

The 2026-10-25 pass starts from main `896e4f0`, including the contextual Inspector,
qualified Uno geometry, power and radio work. It keeps the existing perspectives,
project actions, transport commands and project transactions.

## Global controls

The upper row holds the project menu, Design/Run/Debug/Verify navigation and
utility actions. Simulation controls have their own row, with the clock, speed,
fidelity and history controls. The project name truncates inside its menu's
allocated column; its full value remains in the accessible menu label. Popups
can extend below the header without being clipped by it. At very narrow widths
navigation uses another row rather than disappearing. The duplicate demo badge
is suppressed below 1100 px; the session still identifies itself in the status bar.

This replaces a one-row toolbar that put Perspective beyond x=1089 even in a
1024 px window. At 900 px its whole navigation group now fits within the viewport.
The schematic action ribbon is 44 px high rather than 54 px, recovering some of
the height used by the explicit transport row. In Bench view it is absent:
the bench has its own physical-editing controls, and schematic auto-layout
would otherwise act on a different set of coordinates than the one being viewed. Pane-width container queries suppress the redundant
canvas title when necessary and allow its own view/tool controls to wrap.

## Panel constraints and preferences

`DockLayout` measures its available extent with ResizeObserver, with a resize
listener fallback. Requested panel minima remain in force whenever they fit.
When their sum, gutters and folded rails exceed the available space, open-panel
minima scale proportionally for that viewport. This temporary scale does not
replace saved weights, order or folded choices. Expanding the window restores
the preferred proportions. Panels retain their own scrolling for dense content.

Resizing starts with the actual visible pane sizes, including any minimum-size
clamping, rather than inferring edges from their preferred weights. Keyboard
arrows and pointer gutters resize the same adjacent pair. The status bar is a
focusable horizontal scroll area so technical details remain reachable when
they do not all fit.

The browser regression checks open Library/Inspector/canvas bounds at 900 and
768 px, unchanged stored weights on viewport resize, restoration at 1440 px,
16 px keyboard resizing from a constrained edge, and persisted folding after
reload. A separate workflow creates a long-named project and uses every
perspective and utility control across these widths, including keyboard access
to status details. The exercised desktop envelope starts at 768 × 600; this is
not a claim that every specialized editor is optimized for phone-sized screens.

## Optional rendering

The [deferred renderer](bench-loading.md) avoids fetching Three.js with the
schematic entry. Keyboard editing stays mounted during loading or failed asset
requests. Network failure and graphics-context failure have separate recovery
paths. No renderer asset, vendor library or electrical-model fidelity is removed
to obtain the bundle reduction. Reference-machine frame-time budgets remain open.
