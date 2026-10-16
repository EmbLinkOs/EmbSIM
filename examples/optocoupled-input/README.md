# Optocoupled Input

A D2F-01L lever switches the LED side of a VO617A-3; the output is pulled up to 3.3 V on a separate reference.

Hold SW1 in the Run controls or press the lever artwork. The output is active low. The fixture uses a 2 V input-ground offset to check that optical control does not create a galvanic connection. Nominal CTR and DC behavior are modeled; certified isolation, switching delay and temperature-dependent CTR are not.

Run from the repository root:

```sh
build/dev/bin/embsim run examples/optocoupled-input/project.json --scenario examples/optocoupled-input/scenarios/interact.json --json
build/dev/bin/embsim serve examples/optocoupled-input/project.json --ui ui/apps/workbench/dist
```

In the workbench, open Run and adjust the named controls while simulation runs.
The same shared engine and input semantics are exercised by the CLI scenario.
Artwork is illustrative; package footprints and hardware qualification are outside this example.
