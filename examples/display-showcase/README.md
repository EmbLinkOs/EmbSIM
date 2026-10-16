# Display showcase

The displays and actuators of the M3 catalog in one functional-policy
project, driven without firmware:

- a 16x2 LCD and an SSD1306 OLED written by pattern generators (8-bit
  parallel, and I2C with the I2C decoder on its probes);
- a MAX7219 8-digit module over SPI;
- a 74HC161 counting at 2 Hz into a CD4511 and a 7-segment digit, and into
  an RGB LED;
- a CD4017 at 8 Hz chasing a bar graph;
- a servo on 1.25 ms pulses (45°) and a 555 sounding a buzzer at 1.9 kHz.

The board view (Run, Ctrl+2) shows them all. The pattern words were made
with `tools/catalog/stimulus.py`.

```sh
embsim run examples/display-showcase/project.json --scenario examples/display-showcase/scenarios/show.json
```
