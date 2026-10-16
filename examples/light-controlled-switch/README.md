# Light Controlled Switch

A GL5528 light divider drives a 2N3904 LED switch; a two-way terminal block exposes the supply connections.

Adjust LDR1 illuminance. Darkness switches the LED on; bright light switches it off. The GL5528 power-law fit is illustrative and its manufacturer source remains reference-only. It does not simulate spectral response or light-adaptation time.

Run from the repository root:

```sh
build/dev/bin/embsim run examples/light-controlled-switch/project.json --scenario examples/light-controlled-switch/scenarios/interact.json --json
build/dev/bin/embsim serve examples/light-controlled-switch/project.json --ui ui/apps/workbench/dist
```

In the workbench, open Run and adjust the named controls while simulation runs.
The same shared engine and input semantics are exercised by the CLI scenario.
Artwork is illustrative; package footprints and hardware qualification are outside this example.
