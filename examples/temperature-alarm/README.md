# Named-part temperature alarm

An LM35DZ at 5 V produces 10 mV/°C. Channel 1 of an LM393P compares it to
0.7 V, giving a nominal 70 °C threshold. The comparator's open collector needs
its 10 kΩ pull-up. Above threshold, the pulled-up output drives a 2N3904 base
through another 10 kΩ resistor; the transistor lights a red LED through 330 Ω.
Both channels and supply pins of the LM393P are wired; the unused channel is
held in a defined state.

```sh
./build/dev/bin/embsim run examples/temperature-alarm/project.json \
  --scenario examples/temperature-alarm/scenarios/temperature.json --json
```

The six assertions check cold/hot sensor voltage, cold/hot/cooled LED current,
and solver health. The scenario changes ambient temperature from 25 to 80 and
back to 25 °C. These instantaneous changes exercise the transfer function, not
physical heating/cooling times.

With the workbench built, `embsim serve examples/temperature-alarm/project.json`
opens the same circuit through the real engine. In Run, change `S1 temperature`
to 80 °C, then back to 25 °C. Inspect the manufacturer/package details in Design.
Labels connect the schematic terminals; there is no firmware in this example.

The model omits sensor accuracy/noise/thermal response, comparator delay and
common-mode faults, transistor temperature and device tolerances. The original
artwork is illustrative; consult the physical package datasheet before wiring
hardware. See [catalog limits and source status](../../catalog/README.md).
