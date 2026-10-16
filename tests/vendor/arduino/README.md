# Arduino sketches on the unmodified core

Evidence for 01 §8.2 (a board runs its vendor's examples): sketches written
for the Arduino framework, built with the unmodified
[ArduinoCore-avr](https://github.com/arduino/ArduinoCore-avr) as the IDE
builds them, run on `emblink/arduino_uno` and pass their scenarios.

| Sketch | What it checks |
| --- | --- |
| `blink` | The IDE's Blink: the L LED at 0.5 Hz from `delay(1000)` |
| `core_tour` | `delay` and `millis` (the L LED at 5 Hz, the printed times), `analogRead` (a potentiometer at half and a quarter), `analogWrite` (490.2 Hz on D9 from Timer1, 976.6 Hz on D6 from Timer0), `attachInterrupt` (a button on D2), `Serial` at 115200 baud both ways (a terminal's character echoed) |
| `spi_wire` | The core's SPI library (4 MHz, mode 0) against a 25LC040 on D10 to D13: WREN, WRITE, RDSR polled through the write cycle, READ; its Wire library (100 and 400 kHz) against a 24C02 on A4/A5: a page write, NACKed polls, `endTransmission(false)` and `requestFrom` across a repeated START, an absent address |

The core is LGPL-2.1 and is not vendored here; `run.py` takes a checkout of
it:

```sh
git clone https://github.com/arduino/ArduinoCore-avr /tmp/ArduinoCore-avr
python3 tests/vendor/arduino/run.py --core /tmp/ArduinoCore-avr [--embsim build/dev/bin/embsim]
```

It compiles every file of `cores/arduino` into `core.a` with avr-gcc and
the flags of the core's `platform.txt` (`-Os -flto`, `--gc-sections`) and
the Uno's `boards.txt` entry; turns each `sketches/<name>/<name>.ino` into
C++ as the IDE's builder does (`Arduino.h` included, a `#line` back to the
sketch); compiles the core's libraries the sketch includes (`libraries/<name>/src`,
SPI and Wire here) as the builder finds them; links it; and runs `project.json` (the Uno, a potentiometer on A0,
a button on D2, a terminal on D0/D1, a 25LC040 on the SPI pins with its CS
pulled up, a 24C02 on A4/A5 with 4.7 kOhm pull-ups) under the sketch's `scenario.json`.
Without avr-gcc or a core it skips.

CI's linux-gcc job runs it against commit 11b9130 of the core with Ubuntu
24.04's avr-gcc 7.3.0. Blink links to 924 bytes of flash, the size the IDE
reports for it on an Uno.
