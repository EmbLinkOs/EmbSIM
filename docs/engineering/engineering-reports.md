# Assembly inventory and model/source reports

Engineering reports use `project.manifest` to capture the canonical project,
project-package instances, compiled model instances and attached loaded catalog
descriptions in one read-only native request. Parameters come from the native
engine after defaults, presets and block forwarding, not from a second frontend
parameter resolver. The request preserves the clock, probes, aliases and circuit.

## Assembly and model identity

Project blocks expand into concrete paths (`A.R`, `B.R`). Catalog composites such
as LM358P remain one configured package; their internal simulation primitives are
separate **compiled model records**, not extra physical packages. Root boards are
whole assemblies, without treating the board's internal devices as extra purchased
boards. Unused project definitions are not assembly instances.

Groups require the same catalog reference, loaded description identity, resolution
and resolved model configuration. Different parameter overrides remain separate.
References are fully qualified and sorted; quantities count concrete instances.
A source, ground symbol or generic bench instrument is a simulation fixture and is
excluded from the default assembly CSV, with an explicit option to include it.
Generic physical parts without manufacturer/orderable metadata remain **MPN
unassigned**. A model name is not a procurement choice or device qualification.

The model ledger retains loaded descriptions attached to all native compiled
components, including package implementation primitives and generated project
blocks. Colliding generated description identities remain separate by content
hash. The compiled records retain their actual primitive name and resolved
parameters. This provides a model audit without inflating the assembly inventory.

## Exact content identities

A manifest is a version-1 `engineering_manifest` envelope with `json` and
`sha256`. The JSON string contains the captured data. Its SHA-256 covers those
exact UTF-8 bytes, including parameters, policy, clock and library metadata.
The project and each distinct description also carry their own exact JSON string
and digest. Clients do not need another serializer to reproduce native number
spelling or member order. The GUI verifies all of these identities before showing
a native report; demo data is explicitly unverified.

Digests identify content and detect accidental changes. They are **not signatures,
authentication, external-datasheet verification or evidence of device accuracy**.
The raw manifest preserves native JSON text, including integer digits beyond
JavaScript's exact integer range. Such configurations receive no exact browser/CSV
numeric preview and are not merged based on a rounded JavaScript value.

## Workflow and exports

Open Engineering reports from either schematic toolbar. Assembly search filters
visible configuration groups; exports include all assembly groups, independently
of the search. Expanding references and selecting Locate opens the corresponding
sheet, selects the local component and includes its labels in the focused view.
Source view shows model scope/omissions, loaded versions, declared source URLs and
hashes, compiled instances and loaded library names/versions/paths.

BOM CSV uses RFC 4180 quoting and includes quantity, references, catalog/version,
manufacturer/MPN/package, resolved configuration, classification, resolution and
notes. Spreadsheet-formula-leading text is made literal; the native manifest
retains verbatim identifiers. Reports state when a declared project version text
differs from the loaded version; this is not dependency-range resolution or a
library lock. The table bounds on-screen rows; exports retain every instance.

Reports are snapshots. A project/policy change disables exporting/locating a stale
snapshot until Refresh. Export also rechecks the exact native project text and
policy, covering changes beyond a browser numeric preview. Simulation time advancing alone does not invalidate static
assembly data. Reopening captures a fresh native snapshot. Reports never change
connectivity, physical loading or runtime inputs.

```sh
build/ui-layout-native/bin/embsim manifest examples/hierarchical-instruments/project.json
build/ui-layout-native/bin/embsim manifest examples/hierarchical-instruments/project.json \
  --json > /tmp/measurements.manifest.json
python tools/validate.py --strict /tmp/measurements.manifest.json
```

The default CLI output is a human summary. `--json` exports the same format as the
GUI and RPC. Offline verification checks envelope/payload shape, exact digests,
unique qualified paths and instance/description identities without installing the
original libraries. Demo exports produce an unverified-evidence warning.

## Acceptance and limits

Native tests compare CLI/RPC source facts, actual per-instance meter loading
parameters, package-versus-implementation counts, inherited defaults, board
assembly counts and retained native time/probes/aliases. Browser acceptance checks
quantity-two grouping, exact selected-part navigation, native metadata export,
offline validation and GUI/CLI identity agreement. UI tests check stale snapshot
rejection; schema backends reject malformed or corrupted artifacts.

This is an audit report, **not** an assembly-variant system, a dependency installer
or lock, a PCB export or a complete reproducible execution package. It does not
embed firmware images, Lua/C implementations, register files, package-pin files or
other external resources merely referenced by attached descriptions. Native engine
version/ABI is recorded, not a complete binary build identity. Interactive inputs,
MCU register state and waveform history are outside this static inventory. Runtime
component/board internals are model records rather than a manufacturing BOM for
those assemblies. Large-manifest transport/memory qualification remains open.

Use [the manifest schema](../../schemas/engineering-manifest.schema.json) and
[SPEC-008](../specs/SPEC-008-session-protocol.md) for the format; the existing
complete-schematic release gates remain in [live schematic](live-schematic.md).
