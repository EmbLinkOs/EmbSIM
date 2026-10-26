# Editable hierarchy and reusable circuit blocks

A project can own reusable `blocks`. An ordinary component instance references
one with `component: "block/divider"`; nested blocks use the same syntax. Each
definition has its own components, named nets, explicit port-to-pin mappings,
optional parameter declarations and drawing. The original document remains the
saved electrical design. The loader elaborates through the existing native
composite engine rather than simulating another client-side circuit.

## Connection rules

- Component identifiers are local to their definition. Runtime identities are
  qualified: `A.D.RT` is distinct from `B.RT`.
- Local net names are local to an instance. `A.D.OUT` and `B.OUT` remain separate
  unless an explicit parent connection joins their ports. There are no implicit
  global nets merely because two sheets use the same text.
- A port maps to an existing local component pin in a named local net, for
  example `"OUT": "RT.2"`. It may map to a nested block's port. Use canonical
  terminal names when authoring drawings. A port is an electrical alias, not an
  extra driver, resistor or fabricated measurement.
- Root boards and their firmware retain their existing interfaces. Root GPIO nets
  can connect to block ports. Nested board/firmware instances are not introduced
  by this milestone; blocks contain catalog components and other project blocks.
- The engine's alias map is authoritative. `project.hierarchy` returns actual
  native names for each instance's local nets, including additional native wire
  and component ties. Tracing compares solved net identities, not name similarity.
- Root net names cannot occupy a block instance namespace such as `A.OUT` when
  `A` is a block instance. Local block net names contain no dots. This prevents
  an accidental alias collision with qualified internal nets.

Definitions use lowercase catalog-style identifiers, ports use named terminal
syntax, and case remains significant. The current bounds are 256 definitions,
eight block levels, 10,000 expanded components and runtime instance paths below
150 characters; existing catalog composite expansion limits also apply. Recursive
references, unknown members/ports and duplicate pin membership fail validation,
including unused definitions. Device parameter operating ranges are checked when
instantiated; an unused definition is not a new device-qualification result.

## Editing workflow

Open **Sheets and reusable blocks** from Design. Projects containing blocks open
in the hierarchy workspace. The left navigator lists concrete instances and shared
definitions; the active-sheet selector also exposes definitions without instances.
Double-click or press Enter on a block to enter its sheet. **Overview** returns to
the established project workspace. Run can open a block into the sheet editor.

Search parts, place components/blocks, connect pins with the existing wiring
gestures, rotate, move, repair routes, edit parameters and reshape wires directly
on the selected sheet. Root board drawing moves remain supported. The inspector
can explicitly reconnect/disconnect a selected part's pins. **Route sheet** repairs
its drawing; labels are re-anchored to the resulting geometry. Port flags on the
sheet identify the exported internal terminals and support keyboard net tracing.

A definition is shared. Editing it through `A.D` also updates `B` when both use
`divider`. The inspector states this explicitly. Choose the parent instance and
edit its declared parameters for per-instance values. Parameter declarations and
`$name` bindings can be authored in project JSON; native parameter metadata supplies
resolved instance values to the drawing. The GUI exposes instance values and now provides a declaration editor; see
[project-wide reliability](schematic-reliability.md) for types, rename propagation
and limits.

**New block** creates an editable empty definition. **Extract selection** moves
selected unlocked components into a reusable definition, creates ports for
boundary connections and enclosing exports, and keeps internal nets local. It
remaps project-owned pin/device/output/serial observations and internal-net probes.
Selection extraction preserves parameter forwarding. Affected parent wires require
repair because the members have become a block; use Route sheet. Parent bench
references to extracted members are removed, with internal bench placements and
jumpers retained in the child drawing data. Nested 3D rendering remains deferred.
External scripts, scenario files and recordings referring to old component paths
are not rewritten; update them to the qualified paths after extraction.

Port names and mappings are editable. Renaming rewrites parent pin references,
enclosing port mappings and project-owned pin probes, retaining declaration order
and existing anchor positions. Block `layout.port_order` stores those positions
independently of JSON key order, including numeric terminal names. Unlisted ports
use deterministic name order. New ports append to stable alternating left/right
positions. Object key reordering alone is not an electrical edit. This is drawing placement, not a claim about electrical input/output
direction: actual internal pin models determine drive and loading.

Export a block project to move definitions between projects. The export contains
only transitive project-owned dependencies and one root instance when required
parameter values/defaults are available; otherwise it exports definitions without
an incomplete root instance. Unrelated definitions are excluded. Ordinary catalog descriptions
must be available in the destination's libraries. Import merges identical
identifiers and rejects different content under an existing name. This is portable
project composition, not managed/versioned library distribution.

## Atomicity, undo and runtime behavior

All editor changes use undoable project transactions. `set_circuit` replaces the
components, nets, blocks and drawing as a coherent staged edit, with project probes
included when provided. A malformed patch cannot partially change even its staged
transaction. Structural commits preflight in an isolated session with private block
descriptors, preserving the live project, time and probes if validation fails.
Descriptors never enter the shared installed catalog.

Electrical changes restart simulation and report it; they do not pretend to be
hot circuit surgery preserving firmware state. Drawing-only changes at every
sheet depth retain engine time, state and probe identity. Classification removes
only known drawing fields, so an actual model parameter named `layout` remains an
electrical change. Undo/redo uses the same validation and restart rules.

Operate and Probe modes target the concrete instance's qualified components and
native nets. An uninstantiated definition has no live inputs, light outputs or
measurements. Tracing an instance highlights every matching local terminal and
lists all related sheets; keyboard links navigate to the corresponding net.

## Acceptance and remaining gates

The [hierarchical divider](../../examples/hierarchical-divider/README.md) composes
the same divider twice, one nested behind a real 2 kΩ load. Independent resistor
calculations predict 2 V and 1.65 V. Changing the shared upper resistor to 2 kΩ
predicts 1.25 V and 1.1 V; undo restores both. Native protocol/browser checks cover
those values, qualified port probes, a qualified on-sheet contact that changes one instance
without disturbing its sibling, trace scope, retained-clock moves, port
renaming, nested extraction, export and save fidelity. Invalid recursion, unknown
ports, duplicate pins, namespace overlap and malformed staged patches preserve the
prior circuit. Address/undefined-behavior sanitizers exercise session ownership.

Remaining work includes sheet-local board firmware, explicit global-net/sheet-port
objects, advanced port-interface migration, managed/versioned library packages,
hierarchy-aware BOM/PCB workflows and
qualified industrial-size editing performance. The flat overview and hierarchy
editor have different command surfaces; block editing does not yet reproduce every
flat-editor annotation or template command. Cross-sheet copying and protected
port removal are described in [project-wide reliability](schematic-reliability.md). No Proteus-parity, market
leadership or full 1.0 completion is claimed.

```sh
embsim run examples/hierarchical-divider/project.json \
  --scenario examples/hierarchical-divider/scenario.json --json
python3 tests/protocol/test_hierarchy.py --embsim build/dev/bin/embsim
# Build the workbench first.
python3 tests/browser/test_hierarchy.py --embsim build/dev/bin/embsim \
  --chromium /usr/bin/chromium
```
