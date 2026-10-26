# Source-checked RGB driver bench

WP154A4SUREQBFZGC uses the source-reviewed R–COM–B–G pin order and 1.27 mm lead
pitch. It is a common-cathode package. Three physical switches, 25 Ω representative
output resistances and separate 220 Ω limiting resistors feed its channels from
5 V. These resistors model a finite driver; this is not qualification of any
particular MCU GPIO. Change V1 to 3.3 V to see current fall differently by channel.

```sh
embsim run examples/qualified-rgb-driver/project.json --scenario examples/qualified-rgb-driver/colors.json --json
```

The scenario checks every mix and returns to off. Native fixtures separately
check source-curve holdouts and loaded 3.3/5 V circuits. The library Models tab and
selected-part inspector show the checked **25 °C, 2–20 mA nominal DC envelope**.
This does not establish manufacturer corner, temperature, damage, transient or
photometric qualification. Body dimensions/pitch are source-backed; curved lens,
3 mm clearance, lead placement and connection mechanics are representative. The
four leads do not fit four consecutive 2.54 mm breadboard holes without forming.
