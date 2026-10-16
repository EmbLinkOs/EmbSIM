# Getting started

EmbSIM is in its M0 foundation; this page grows with the implementation.

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

## Run firmware

```sh
./build/dev/bin/embsim run examples/bluepill-button-led/project.json \
  --scenario examples/bluepill-button-led/scenarios/press.json
```

A Blue Pill runs the example's firmware with a button, a transistor and LED,
and a serial terminal. [Firmware](firmware.md) covers building and
debugging it.
