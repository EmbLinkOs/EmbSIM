# Arduino Uno on a breadboard

[`uno-blink-serial`](../uno-blink-serial/README.md)'s circuit, built as it is
built on a desk: an 830-point breadboard beside the Uno, the parts plugged
into its strips and wired to the Uno's headers with jumpers. The firmware
and the scenario are the same, and the scenario passes the same: the
breadboard's strips and rails (its `ties`) are what join the parts.

On the bench (`layout.bench`):

- the 330 Ω resistor from a20 to a24 and the blue LED's anode in c24 (one
  strip), its cathode in c25, jumpered from e25 to the top ground rail;
- the push button from e30 to e33, a30 jumpered to D2 and a33 to ground;
- the 10 kΩ potentiometer in f40, f42 and f44 across the channel's lower
  half: j40 to the lower ground rail, j44 to the lower 5 V rail, h42 to A0;
- the Uno's 5V and GND to the top rails, the top rails to the lower ones at
  the right end, D9 from e20 to the resistor's strip;
- the 5 V supply (standing for the USB port) on the lower rails, a
  USB-serial adapter on D0 and D1.

```sh
embsim run examples/uno-breadboard/project.json --scenario examples/uno-breadboard/scenarios/blink.json
```

In the workbench, open it and switch the Design canvas to **Bench**: drag a
part off the breadboard and its legs leave their strips (the schematic
loses those connections); drag it back and they plug in again. **Build**
wires whatever the schematic connects that the bench does not.
