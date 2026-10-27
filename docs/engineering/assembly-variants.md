# Assembly variants

An assembly variant describes which physical packages are fitted and which values
or compatible catalog alternatives are used. An unfitted resistor must open the
circuit; hiding its artwork or leaving a functional short is incorrect.

## Author, review and activate

Open **Assembly variants** from the schematic tools in a native session. Create a
named variant, select a concrete reference (including `A.R` inside a sheet), and
set population, parameter, catalog or version alternatives. Save the definition,
then preview it. Definition edits are undoable metadata transactions and preserve
the running clock. Preview builds and resets an isolated native circuit; it reports
native validation and resolved assembly values without changing the current
clock, probes or outputs. It is not a transient analysis of that circuit.

Review the derived circuit and BOM before activating. Activation checks the exact
retained base hash again, validates before replacing the session, restarts time at
zero and clears acquisition/history state. Invalid or stale previews cannot
replace the running project. Derived circuits are immutable engineering artifacts:
return to base before editing. Fitted runtime controls and probes remain usable.
The native transaction API enforces the same rule. Activation clears the saved
project path; Save requires a new explicit path to protect the original base.

A DNP symbol is dimmed, crossed and labeled beside its value. Terminal sockets,
nets and numeric live pin evidence remain inspectable. Its compiled placeholder
has passive pins and no drive, pull, input action, internal ties, composite children
or electrical model. Input listing excludes it and immediate/scheduled actions
are rejected. This models an ideal empty footprint, without pad parasitics.

Assembly reports omit DNP by default. Including DNP produces separate zero-quantity
rows labeled DNP; variant exports identify the selected variant. Fixtures are
independently excluded by default. Native preview exposes actual replacement
configuration, not just typed overrides.

## Stored contract

`project.variants` maps up to 32 identifiers to optional descriptions and an
`overrides` map keyed by concrete project-owned catalog references. Each override
may contain `populated`, `params`, `component` and `version` (null removes the
inherited request). Parameter overrides merge into the base. A catalog replacement
starts from its own defaults and requires identical visible symbol terminal names,
positions and dimensions; this does not certify PCB footprint compatibility.

Only affected project block branches are cloned, so repeated instances remain
independent. Ports, reference IDs, connections and wire geometry survive. Limits:
10,000 overrides, depth eight and 256 materialized block definitions. Block wrappers,
board internals and wiring aids are not population targets. No variant definitions
means existing loading behavior is unchanged.

Derived projects remove `variants` and retain `variant_origin` with the name, exact
compact base JSON and its SHA-256. Native code verifies the UTF-8 bytes, rejects
recursive provenance and requires that the named variant exists in the retained
base. The retained base is bounded to 8 MiB. It supports exact return-to-base and
is not a signature or complete firmware/library archive. Graphical export uses
native serialized bytes to preserve integers outside JavaScript's safe range;
unsafe integer definition edits are refused rather than rounded silently.

## CLI and RPC

```sh
embsim variant list examples/assembly-variants/project.json --json
embsim variant render examples/assembly-variants/project.json --name production > /tmp/production.json
```

`list` describes alternatives; `render` emits a project only after native
validation. Neither edits its input. Relative library/firmware paths remain
relative to the saved project context; exporting elsewhere does not bundle them.

RPC `project.variant` accepts `op: base`, `preview` or `activate`. Preview/activate
accept optional `name`; omission selects the retained base. Activation accepts
`expected_sha256` and is rejected during an open project transaction. Preview
returns `base_sha256`, project, validity, diagnostics and a manifest when valid.
Session capability `assembly.variants` distinguishes support from an older server.
The demo transport explicitly refuses native review/activation.

Metadata transaction `set_variants` updates definitions; root-part
`set_population` changes physical population and supports inverse edits. An
explicit true equivalent to the default preserves time; a real population change
restarts compilation. Native manifests add each instance's population evidence.
Offline schema/validator checks verify paths, provenance and manifest consistency;
actual electrical alternatives still require native preview.

## Evidence and remaining limits

`tests/protocol/test_variants.py` checks native CLI/RPC parity, retained hashes,
independent sheet instances, source preservation, atomic failures, recovery,
loading, versions, replacement defaults, DNP internal ties/composite children,
functional LED behavior and forbidden runtime actions. The ten-volt example gives
5 V and 100/11 V in the base; production gives 0 V and 25/3 V.
`tests/browser/test_assembly_variants.py` authors real alternatives, reviews and
exports them at 1 µs, activates, checks DNP sockets/voltages and restores the base.
UI tests cover grouping, inverse edits and feedback. Offline tests use both
validator backends. See the [gallery](../gallery/README.md).

This milestone covers project-owned schematic assemblies. Firmware build
variants, board-internal assembly choices, alternate PCB footprints, pad leakage,
mechanical population, 3D assembly and global device qualification remain future
work. Bench mode is disabled for derived/DNP flat projects instead of showing an
incorrect assembly. Preview validates at reset and cannot qualify every future
runtime state or establish manufacturer conformance.
