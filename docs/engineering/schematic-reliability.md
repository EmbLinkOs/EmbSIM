# Project-wide schematic reliability

The hierarchy editor now supplies parameter declaration authoring, safe unused
port removal, cross-sheet copying, ordered buses, project search and a combined
project electrical/drawing review. These reuse the canonical project document,
existing native staged transactions, existing bus renderer and native alias map.
They do not implement another circuit solver.

## Editing contract

- Copy selects components, their shared connections and wholly internal wires.
  Pasting creates unique local references and fresh nets, including when copying
  a net that also reaches outside the original selection. It never reconnects a
  copied circuit to the source boundary by matching names.
- Copy includes only the nested block dependency closure and referenced parameter
  declarations. Identical definitions compare independently of JSON object key
  order. Conflicting definitions/declarations and recursive insertion fail.
- Parameterized leaf components paste into another block, carrying declarations.
  Pasting unresolved `$name` bindings into the root is rejected; replace bindings
  with concrete values first. Clipboard scope is the open workspace, not the OS
  clipboard. Boards and external firmware/scenario files are not copied.
- Parameter declarations support number, integer, boolean, string, enum and table
  defaults, units, documentation, required state and numeric bounds. Defaults must
  match the type/range. Internal scalar editors accept `$name` bindings; native
  preflight remains responsible for target compatibility and parameter resolution.
  Table defaults are authored as JSON pairs; direct instance table editing is not
  added by this change.
- Renaming a declaration updates its internal bindings and every parent override,
  including overrides inside another reusable definition. Removing a declaration
  with bindings/overrides fails until those uses are removed.
- Port removal requires no connected parent nets, enclosing exports or project
  pin probes. Rejection lists uses to reconnect first. Renaming/remapping retain
  the previous milestone's propagation behavior; removal never silently drops a
  connected circuit. External scenario/recording/script references are not scanned.
- Bus editing supports 2–32 distinct existing nets with explicit least-significant
  first ordering. Rename/reorder preserves its geometry. A bus is a drawing/value
  group, never an electrical short or implicit port bundle. Member buttons trace
  individual nets. Electrical bus ports/harness connectors remain separate work.
- Copy/paste and undo/redo shortcuts operate on the sheet without intercepting
  text fields. Copy may inspect read-only projects; mutations respect native
  transaction availability and busy state. Undo includes dependent declarations.

## Search and navigation

Find in project matches all entered words against reference, component identifier,
explicit parameter values, net members, port target and bus members. Results
identify concrete instance paths and reusable definitions separately. Selecting a
result changes sheets and locates its component/net. The first 100 matches are
shown with the total; refine the query for larger result sets. Installed component
internals and runtime-inherited parameter values are not indexed as project text.

## Electrical and drawing review

Project checks combines three different kinds of evidence without conflating them:

1. Drawing checks run once per shared definition and root sheet, using existing
   scene checks. Exported single-pin nets are valid boundary connections. Bus
   membership checks detect absent/duplicate members.
2. Advisory electrical checks traverse concrete leaf instances through native net
   aliases. They flag explicitly required terminals without external connections,
   unconnected digital inputs without pulls, missing power connections, input-only
   nets without a declared pull, and multiple declared push-pull outputs. The last
   is a topology warning, not evidence of active contention. Optional power rails
   remain information because some models intentionally use implicit rails.
3. Live checks use current native `contention`, logic and receiver observations.
   Only native contention is reported as confirmed opposing drives. X is unknown;
   Z at receivers is floating. Zero analog voltage alone proves neither.

Board-connected nets count as external connections; ERC does not guess MCU pin
configuration from header artwork. Missing component descriptions produce coverage
warnings. Installed composite internals, undocumented analog paths, absolute
maximum ratings, voltage-domain compatibility, physical clearances, supply corner
qualification and firmware-dependent pin modes are not established by these checks.
A no-error report is not proof of an electrically correct design.

Filter by source, severity or text and select a result to locate it. Reports export
project name, current picosecond time and structured issues with paths, rule codes,
severity, target and evidence source. Reports are snapshots, not replay sessions.
Geometry checks are memoized separately from live observations so every incoming
sample does not trigger another geometric review. Larger designs still require
measured performance qualification.

## Acceptance

Pure tests check isolated copying, dependency conflicts/cycles, declarations and
renames, protected removal, bus ordering, search locations, native-alias ERC,
optional power/pulls, board boundaries and X/Z/contention distinctions. Browser
workflows author/bind/rename parameters, reject connected port removal, edit a
bus, copy a repeated block, search and inspect checks. Native browser acceptance
also verifies solved divider voltages after GUI declaration/binding/override/undo,
and preserved simulation time after a bus edit. A second native browser workflow
checks confirmed contention at a solved 1.65 V, floating-input evidence, structured
report export and conflict clearing. These are linear network and
workflow checks; they do not qualify arbitrary component behavior.

```sh
python tests/browser/test_hierarchy.py --embsim build/ui-layout-native/bin/embsim \
  --chromium /usr/bin/chromium --screenshots /tmp/embsim-reliability
```

The broader complete-schematic gates remain in issue #64 and
[live-schematic engineering](live-schematic.md): firmware-board hierarchy,
managed libraries, full analysis/instrument qualification and engineering output
integration are not completed by this milestone.
