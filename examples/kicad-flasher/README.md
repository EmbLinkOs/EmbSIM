# KiCad flasher

A 555 LED flasher fed from a 9 V battery through a 7805, as a KiCad
netlist (`flasher.net`) turned into an EmbSIM project with

```sh
python3 tools/importers/kicad2project.py examples/kicad-flasher/flasher.net -o examples/kicad-flasher/project.json
```

The importer maps the KiCad symbols to catalog parts with their values,
keeps the net names, adds a ground symbol, and reads the 555's astable
timing from R1, R2 and C1. The STM32 on the sheet is left out (an MCU
belongs in a board) and reported. The probes were added by hand.

```sh
embsim run examples/kicad-flasher/project.json --scenario examples/kicad-flasher/scenarios/blink.json
```

checks the flash rate (6.86 Hz) and the 5 V rail.
