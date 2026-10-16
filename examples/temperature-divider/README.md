# Temperature Divider

A 10 kohm Vishay NTC divider feeds LM393P; a 3296W trimmer sets the alarm threshold and the comparator sinks an LED.

Drag NTC1 temperature between 25 and 85 C and adjust P1 position. The LED lights when the divider falls below the selected reference. The beta-law response is nominal; thermal inertia and self-heating are omitted.

Run from the repository root:

```sh
build/dev/bin/embsim run examples/temperature-divider/project.json --scenario examples/temperature-divider/scenarios/interact.json --json
build/dev/bin/embsim serve examples/temperature-divider/project.json --ui ui/apps/workbench/dist
```

In the workbench, open Run and adjust the named controls while simulation runs.
The same shared engine and input semantics are exercised by the CLI scenario.
Artwork is illustrative; package footprints and hardware qualification are outside this example.
