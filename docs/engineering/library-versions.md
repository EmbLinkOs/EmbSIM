# Declared version contracts

An explicitly requested part or library version is a load contract. Previously,
project `version` fields and library `depends` declarations were accepted as data
without checking the loaded revision. The native loader now refuses incompatible
or malformed requests instead of silently running another revision.

This adds **constraint enforcement to the existing first-match catalog**, not a
remote registry or a new multi-version resolver. The bundled catalog loads first,
then user libraries, then project libraries; the first definition of an ID wins.
Relative and absolute spellings of the same normalized root are deduplicated;
already-loaded roots retain one coherent manifest/description revision until a
fresh session. Symbolic-link identity and Windows path case aliases are not
canonicalized. A matching later definition does not silently replace that selection. Use unique
library identities and explicitly install the intended revision. Projects with no
version requests and libraries without dependencies keep their existing behavior.

## Where contracts apply

- Root project components and boards.
- Component members inside project-owned blocks, including unused definitions.
- Catalog IC/composite members and on-board components.
- The board's MCU reference.
- Every loaded library's direct dependencies. Checking every declaring library
  also covers transitive installed requirements, even if no component uses that
  library. Missing, incompatible and ambiguous dependencies are distinct errors.

Dependencies refer to library **manifest versions**, independently of the versions
of individual parts. A dependency without a range accepts any loaded revision.
Already-installed dependency cycles are permitted: all requirements must hold;
there is no install ordering or fetching in this check. Two distinct roots for a
requested dependency name are ambiguous and refused. Missing dependencies are not
downloaded or automatically added to a project's search paths.

Project-owned blocks are local shared definitions without a published SemVer;
a version request on `block/...` is rejected rather than invented. Component
members inside those blocks can request their actual catalog versions.

## Supported range grammar

Version precedence follows [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html):
three numeric components, optional prerelease/build identifiers, numeric
prerelease identifiers before nonnumeric ones, and ignored build metadata for
precedence. Numeric identifiers have no leading zeros except `0`; build metadata
may contain them. Release versions sort after prereleases of the same core.

Supported bounds follow npm's documented
[advanced ranges](https://github.com/npm/node-semver#advanced-range-syntax):

| Request | Meaning |
| --- | --- |
| `1.2.3`, `=1.2.3` | Exact precedence, ignoring build metadata |
| `1`, `1.2` | Matching major, or matching major/minor |
| `^1.2.3` | At least 1.2.3, below 2.0.0 |
| `^0.2.3`, `^0.0.3` | Below 0.3.0, or below 0.0.4 |
| `~1.2.3`, `~1.2`, `~1` | Below 1.3.0, 1.3.0, or 2.0.0 |
| `>=1.2`, `<2.0.0` | Inclusive/exclusive comparison with omitted fields zero-filled |
| `>1.2`, `<=1.2` | At least 1.3.0, or below 1.3.0 |
| `>=1.0 <2.0`, `>=1.0,<2.0`, `>=1.0<2.0` | Conjunction; comma and adjacent comparison forms are explicitly supported separators |

A prerelease is excluded unless a clause explicitly names a prerelease of the
same major/minor/patch core. For example `>=1.2.3-alpha <2` admits 1.2.3-beta but
not 1.3.0-beta. All clauses are validated even if an earlier clause mismatches.

Bounds are deliberate: 255 ASCII characters, up to 16 conjunction clauses,
numeric core components 0..2147483647. An incremented upper bound that exceeds
that numeric domain is rejected. Wildcards (`*`, `x`), OR (`||`), hyphen ranges,
`v` prefixes and partial versions with prerelease/build suffixes are unsupported
and rejected. This is a bounded subset with documented separators, not full npm
range syntax. Native C and offline Python consume the same acceptance vectors.

## Schematic workflow

In **Engineering reports → Assembly**, expand a reference group and the desired
part's **Version request**. The form shows the loaded revision and native
satisfaction evidence. Enter a supported range, or clear it to remove the request.
The change is an undoable validated structural transaction: successful changes
restart simulation and clear acquisition records, with that consequence stated
before Apply. Unchanged requests do nothing. A rejected request preserves the
previous project, clock and observations and shows the native reason.

A nested part's request belongs to its shared sheet definition; editing A.R also
changes B.R when both use that definition. The form states this explicitly.
The GUI does not expose this native validation workflow as a demo success.
New component and board additions retain their requested version fields.

`set_version {id,version:string|null}` edits existing root components/boards;
`set_circuit` carries shared block definition changes. `project.manifest` retains
`requested_version` and additive boolean `version_satisfied` evidence. The UI
accepts a compatible range based on that native evidence; older artifacts without
the field remain unchecked rather than assuming success. Offline manifest checks
verify any declared satisfaction against its loaded description.

## Verification and limits

`tests/fixtures/version-ranges.json` contains explicit boundary, precedence,
prerelease, malformed and bounded-input cases. `test_version` checks them in C;
`tools/test_version_ranges.py` checks Python. The 55 supported cases were also
checked against npm semver 7.8.5 after normalizing only conjunction separators:

```sh
node tools/verify/check_version_ranges.mjs PATH_TO_INSTALLED_NPM_SEMVER_PACKAGE
python tests/protocol/test_version_contracts.py --embsim build/dev/bin/embsim
python tests/browser/test_version_requests.py --embsim build/dev/bin/embsim --chromium /path/to/chromium
```

The protocol acceptance covers project, unused block, composite, board, MCU and
on-board member requests; missing/transitive/incompatible/ambiguous dependencies;
rollback and subsequent successful edits; explicit restart; native manifest facts.
The real-server browser rejects ^2 while retaining 1 µs, applies ^1 to both shared
instances, checks the exported evidence and removes the request with Undo.

Constraints establish version compatibility under the declared policy, not model
accuracy or supplier authenticity. Content locks additionally pin actual library
bytes. They complement each other: a matching version label cannot replace a
content hash, and a matching content hash cannot excuse an incompatible request.
No automatic downgrade, alternate-revision search, remote dependency solver or
installer is added here. Whole-project/library lock and firmware/archive limits
remain as described in [library locks](library-locks.md).
