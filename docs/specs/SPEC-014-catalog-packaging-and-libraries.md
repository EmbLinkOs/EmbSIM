# SPEC-014 - Catalog Packaging and Libraries

**Status:** Accepted in outline 2026-10-15; completed before M3. **Builds
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
