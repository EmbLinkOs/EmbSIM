# Electrical feedback: deliberate circuit faults

This example deliberately connects opposing push-pull outputs to demonstrate
warnings. It is a simulator fault demonstration, not a recommended hardware
connection. The generic drivers do not simulate heating or destruction.

```sh
build/dev/bin/embsim run examples/electrical-feedback/project.json \
  --scenario examples/electrical-feedback/scenario.json --json
build/dev/bin/embsim serve examples/electrical-feedback/project.json \
  --ui ui/apps/workbench/dist --port 8080
```

Advance **100us**. HIGH drives 1 while CLOCK drives 0 during its initial 1 ms
delay. The BUS voltage is approximately **1.65 V**, from two finite 25 Ω drivers.
It stays a real solved voltage while the native state reports contention. The
scope input draws about 1.65 µA; current circulating between the outputs is much
larger. This model does not provide output-overcurrent or damage qualification.

Open **electrical warnings → Review** in the schematic. The conflicting-driver
entry locates BUS; the floating-input entry locates FLOAT, whose receiver has no
driver. Selecting a warning highlights terminal rings and labels, even when its
net has no drawn wire. The isolated receiver reports Z; the analog BUS receiver
can retain a sampled level through the undefined-voltage region due to its
hysteresis. The conflict warning is based on the opposing known drive intents,
not an assumption that every X or intermediate voltage proves a conflict.

Advance a further **1ms** to 1.1 ms: CLOCK now drives high, BUS rises to nearly
3.3 V and the current contention warning clears. FLOAT remains high impedance.
The acceptance scenario checks both BUS voltages and solver health; native
protocol tests separately check the contention and receiver fields.

See [the contract and limitations](../../docs/engineering/instrument-loading.md).
