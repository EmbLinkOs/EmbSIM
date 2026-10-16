# Importers

One-shot drafts from other descriptions (SPEC-014 §5). A draft is a starting
point: check it against the schematic, then run
`embsim catalog validate LIBRARY` and `python3 tools/validate.py LIBRARY`.

| Tool | From | To |
|---|---|---|
| `rtos2board.py` | an EmbLinkRTOS board description (YAML, the RTOS's SPEC-014 §4) | `boards/<name>/board.json` with a power fixture |
| `dts2board.py` | a Zephyr or Linux board `.dts` | `boards/<name>/board.json` with a power fixture |
| `kicad2project.py` | a KiCad netlist (KiCad 5 to 8, S-expression) | a `project.json` |
| `svd2regs.py` | a CMSIS-SVD file | a register description for an MCU (SPEC-009 §5) |

The board importers share `boardgen.py`: the MCU is found in the catalog
from the SoC name (`--mcu` overrides), and the draft gets what the board
needs to run: crystals for the oscillators, a resistor per LED, a pull
resistor per button, the NRST pull-up and BOOT0 pull-down, catalog parts
for the bus devices it knows (TMP102, 24C02, SSD1306, PCF8574, DS1307,
MCP3008, MCP4921, 25LC040, MAX7219, DHT22) with I2C pull-ups, and a header
exposing every used signal with the supply. A device without a catalog
part becomes a component marked `todo`, which both validators refuse until
it is replaced.

`kicad2project.py` maps KiCad's standard symbols (`Device:R`, `Device:LED`,
`Timer:NE555P`, `74xx:74HC595`...) to catalog parts, takes values as their
main parameter (`4k7`, `100n`), keeps net names, adds supply symbols for
power nets nothing on the sheet drives, and reads a 555's astable timing
from the resistors and capacitor around it. MCU symbols are left out: an
MCU belongs in a board.

The RTOS importer needs PyYAML (`pip install pyyaml`). Test data is in
`testdata/`; `tools/test_importers.py` and `tests/protocol/test_importers.py`
run the three on it, the second through the engine.
