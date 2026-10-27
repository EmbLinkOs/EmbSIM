# SPEC-014 - Catalog Packaging and Libraries

**Status:** Accepted 2026-10-15; §6 records the M3 implementation. **Builds
on:** 02 §6, §7; ADR-006, ADR-017. **Schema:** `schemas/library.schema.json`.
**Requirements:** `docs/requirements/CAT.md`.

---

## 1. Library layout

```
<library>/
  library.json            {schema_version, kind: "library", name, version, description, license, authors, depends: [...]}
  components/<name>/      component.json, symbol.svg, artwork.svg?, model.lua?, tests/
  mcus/<vendor>/<part>/   mcu.json, <package>.json, register file if vendored, LICENSE
  boards/<name>/          board.json, artwork.svg, tests/
  toolchains/<name>.json
  symbols/                shared symbol SVGs
  LICENSES/               third-party licences for vendored material
```

The bundled catalog is the `emblink` library in `catalog/`. User libraries
live in the platform's user data directory (`~/.local/share/embsim/libraries`,
`~/Library/Application Support/EmbSIM/libraries`, `%APPDATA%\EmbSIM\libraries`)
or beside a project (`project.libraries`).

## 2. Resolution

`<library>/<name>` is looked up in the project's libraries in order, then
the user libraries, then the bundled catalog; the first match of a
compatible version wins; a project records the resolved versions. A missing
item is `catalog.not_found` naming where it looked.

## 3. Validation

`embsim catalog validate PATH` validates every description against its
schema and the semantic rules of SPEC-005 and SPEC-006, checks that every
referenced file exists, that vendored material has a licence, that symbols
declare their pins, and runs the fixtures; the report lists each item with
`ok`, `broken` (fixtures fail) or `invalid`.

## 4. Distribution

A library is a directory or a zip of it; `embsim catalog install
PATH_OR_URL` copies it to the user library directory after validation.
There is no central registry in 1.0; the documentation lists known
libraries.

## 5. Authoring tools

`embsim catalog new component|board|mcu NAME` writes a template with the
required fields and a fixture skeleton; `tools/import/` holds the importers
(RTOS board YAML, DeviceTree, KiCad netlist, SVD/ATDF/EDC to MCU drafts).

## 6. As implemented (M3)

- **Resolution.** A session loads the bundled catalog, then every library
  directory (one holding `library.json`) under the user library directory
  (`$EMBSIM_LIBRARIES` overrides it), then the project's `libraries`. An id
  is defined by the first library that provides it; ids are namespaced by
  library (`acme/part`), so a library cannot replace a bundled part. Version
  constraints and `depends` were not checked in M3; §8 records the later
  enforcement milestone.
- **Validation.** `embsim catalog validate [DIR] [--library DEP] [--json]`
  (`es_catalog_validate`) loads DIR ahead of the session's libraries,
  instantiates each component and board alone under each policy whose
  fidelity tier is declared (a board once), and runs every
  `tests/*.json` fixture as a project and a scenario: one probe and one
  assertion per expectation. Items are `ok`, `broken` or `invalid`; schema and
  reference checks remain `tools/validate.py`'s, which indexes the library
  holding the paths it is given.
- **Distribution.** `embsim catalog install DIR|ARCHIVE.zip|URL [--to
  LIBRARIES_DIR] [--force]` validates and copies a directory (files and
  directories beginning with `.` are left out). A zip archive holds the
  library at its top or in its one top directory (where its `library.json`
  is); its stored and deflated entries (`core/util/zip.c`, `inflate.c`;
  CRC-32 checked; ZIP64, encryption and multi-disk archives refused) are
  unpacked into a staging directory under the host's temporary directory,
  validated there, copied, and the staging directory removed. An entry
  outside the library (an absolute path, a drive, `..`) refuses the whole
  archive, and a library whose `name` is not a plain directory name is
  refused. An `http://` or `https://` URL is fetched by the host's `curl`
  (on `PATH`) into the temporary directory first.
- **Importers** (`tools/importers/`, its README): `rtos2board.py`
  (EmbLinkRTOS board YAML) and `dts2board.py` (Zephyr/Linux DeviceTree
  source, read without dtc) write a board draft with a power fixture;
  `kicad2project.py` turns a KiCad netlist into a project; `svd2regs.py`
  makes MCU register descriptions. Unknown devices become `todo`
  components, which validation refuses (SPEC-006 §4).
- **Authoring.** `embsim catalog new component|board NAME [--library DIR]
  [--lua]` writes `library.json` when missing and a part whose fixture
  passes: a two-terminal resistance, a Lua toggle flip-flop, or an
  STM32F103C8 board with a regulator, crystals, a header and a user LED.



## 7. Opt-in content locks

`project.library_lock` pins all loaded non-hidden library trees and the effective
catalog selection. The native loader enforces the baseline before instantiating
models. Exact binary SHA-256 hashes catch Lua/resource drift even without a version
change. GUI review, explicit undoable pin/removal and `embsim lock capture|check`
share the native implementation; no review silently replaces a baseline. This is
not a version-range solver or remote installer. See
[format, acceptance and exclusions](../engineering/library-locks.md), CAT-013,
UI-031, CLI-006 and ADR-043.


## 8. Declared version enforcement

Explicit requests now enforce bounded SemVer compatibility at project parts,
boards, MCU references, shared-block members and catalog composites. Every loaded
library's `depends` declarations must resolve to an installed, unambiguous name
with a compatible manifest revision. This checks the existing first-match
selection; it does not search alternate revisions or download dependencies.
Malformed/unsupported ranges are rejected, and unversioned local block wrappers
do not acquire invented release versions. See [grammar, workflow and evidence](../engineering/library-versions.md),
CAT-014, UI-032 and ADR-044. Content locks and version requests are complementary.
