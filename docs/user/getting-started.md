# Getting started

EmbSIM is under active development. See the [project status](../STATUS.md) for
implemented behavior, evidence and limitations.

## Build the command line

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev --output-on-failure
```

## Run an example headless

```sh
./build/dev/bin/embsim run examples/button-led/project.json --until 50ms --json
./build/dev/bin/embsim run examples/button-led/project.json --scenario examples/button-led/scenarios/press.json
```

## Open the workbench in a browser

```sh
./build/dev/bin/embsim serve examples/button-led/project.json --open
```

The desktop application (`desktop/`) wraps the same workbench; see
`ui/README.md` for development.

## Design a circuit

The [Schematic Studio guide](schematic-studio.md) covers connected auto-layout,
smart routing, preview/apply/undo, alignment, search and navigation.

## Run firmware

```sh
./build/dev/bin/embsim run examples/bluepill-button-led/project.json \
  --scenario examples/bluepill-button-led/scenarios/press.json
```

A Blue Pill runs the example's firmware with a button, a transistor and LED,
and a serial terminal. [Firmware](firmware.md) covers building and
debugging it.
