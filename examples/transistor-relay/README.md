# Transistor Relay

A 2N3904 drives a G5LE-1 5 V relay coil; a 1N4007 freewheel diode clamps inductive turn-off. Contacts switch a separate 12 V load.

Hold SW1 for longer than 10 ms to pick up the relay, then release it. The RL coil and diode model the electrical transient, while the contacts use sampled hysteresis and persistent timing. The 100 mH coil inductance is illustrative, not measured. Contact bounce, arcing and magnetic saturation are omitted.

Run from the repository root:

```sh
build/dev/bin/embsim run examples/transistor-relay/project.json --scenario examples/transistor-relay/scenarios/interact.json --json
build/dev/bin/embsim serve examples/transistor-relay/project.json --ui ui/apps/workbench/dist
```

In the workbench, open Run and adjust the named controls while simulation runs.
The same shared engine and input semantics are exercised by the CLI scenario.
Artwork is illustrative; package footprints and hardware qualification are outside this example.
