# M3 catalog and SDK: what was built and how it was checked

This page records the M3 implementation: parts can be written in Lua, the
engine validates the catalog against its fixtures, contributors have SDK
commands, the catalog holds 101 components and boards come from other
tools' files. It is the source for the M3 rows of [STATUS.md](../STATUS.md);
update both together.

## What exists

| Area | Files | Contract |
|---|---|---|
| Lua tier | `third_party/lua`, `core/ext/lua_model.c` | SPEC-013 §2: one state per instance, text chunks, an instruction budget and a memory cap, `ctx` with pins, timers, inputs, diagnostics; I2C and SPI slaves decoded in C; an error stops the instance until reset |
| Engine fixes the validator found | `core/engine/engine.c`, `core/analog/devices.c`, `core/model/digital.c` | the timed policy forms analog islands; analog pins bind by name; clock and pulse source parameters; probes report text values |
| Validation | `core/session/validate.c`, `cli/commands.c` | SPEC-014 §5: the library first, then each item under each fidelity policy it declares, then its fixtures as a project and scenario; ok, broken or invalid |
| SDK | `cli/sdk.c`, `sdk/README.md` | SPEC-014 §3, §4, §6: `catalog new component\|board [--lua]`, `catalog install`, user libraries from `$EMBSIM_LIBRARIES` or the user data directory |
| Catalog | `catalog/components/` | 101 components in 16 categories, 38 of them Lua models, every one with a fixture |
| Fixture stimulus | `tools/catalog/stimulus.py` | I2C and SPI transactions as pattern-generator words for fixtures and examples |
| Board view states | `ui/packages/scene/src/BoardView.tsx` | SPEC-005 §8: an artwork state maps a value, formats it or sets an element's text |
| Importers | `tools/importers/` | SPEC-014 §7: EmbLinkRTOS board YAML, DeviceTree sources and KiCad netlists to boards with a power fixture, or a laid-out project |
| Examples | `examples/display-showcase`, `examples/kicad-flasher` | the displays and actuators at once; a KiCad netlist imported and run |

## How it was checked

| Check | Where | What it shows |
|---|---|---|
| Lua tier | `test_lua` (also under ASan and UBSan) | a counter; two runs equal; a runtime error, a runaway loop and a syntax error stop the instance with their diagnostic; an EEPROM-like I2C slave and an SPI slave answer a master |
| Catalog | `catalog_fixtures`: `embsim catalog validate catalog` | 102 items ok (101 components and the Blue Pill), 150 fixtures pass |
| SDK | `catalog_sdk`: `tests/protocol/test_sdk.py` | a Lua component and a board from the templates validate as written, install into a user library and are listed from it; a part whose fixture fails is reported broken and refused by install |
| Importers | `importers`: `tools/test_importers.py` | the boards they write from the test data validate and pass their power fixture; the KiCad netlist's parts, values and nets map to catalog parts, the 555 takes its astable R and C, an unknown part is left out with a warning |
| Examples | `examples_headless` | display-showcase 6/6 (LCD and MAX7219 text, OLED pixels, the count, the servo angle, the tone); kicad-flasher blinks at 6.86 Hz |
| Workbench | `pnpm test` (75 tests); `scripts/screenshots.mjs` | artwork value maps, formats and text; the [M3 gallery](../gallery/README.md) shows the showcase's displays in the board view |

## Known gaps

Native models and `ctx.analog` for Lua models; the per-project
`lua_budget`; installation from archives and URLs; the `catalog.validate`
protocol method; importers for Eagle, Altium and EasyEDA. The importers
read only what their test files exercise: a DeviceTree's included `.dtsi`
files are not read, and KiCad symbols outside the importer's table are left out with a warning.
