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
| `watchdog` | avr-libc's watchdog: the WDT interrupt waking the part from power-down, then `wdt_enable` kicked by `wdt_reset` and left to time out twice; a boot count in `.noinit` survives each reset, MCUSR reads PORF then WDRF |
| `comparator` | The analog comparator's interrupt timing an RC network (10 kOhm, 1 uF) against 2.5 V on AIN1, rising then falling: 6960 and 6956 us against R * C * ln 2 = 6949 us; this sketch has its own `project.json` |
| `input_capture` | Timer1's input capture (noise canceler on, ICES1 alternated in TIMER1_CAPT) measuring `analogWrite(3, 64)` wired from D3 to ICP1 (D8): a period of exactly 32640 cycles (490.196 Hz) and 8192 high |

## Libraries and their own examples

The libraries parts are used through are listed in `libraries.json`, each
at a pinned commit with its licence; none is vendored. `--libs DIR --fetch`
clones them into `DIR`. A library's own example runs as its authors wrote
it when `examples/<library>/<example>/` holds its `scenario.json` (and a
`project.json` wiring the part as the example's comments say):

| Library example | Part | What it checks |
| --- | --- | --- |
| Adafruit_BME280_Library `bme280test` | BME280 on A4/A5 from the Uno's 3.3 V, SDO pulled up (0x77) | The chip found, reset and set to normal mode at x16; 21.37 C, 1009.82 hPa (28.60 m) and 43.60 %RH as the library compensates them, then 30.05 C, 955.40 hPa (493.17 m), 71.20 %RH after the weather turns |
| Adafruit_MPU6050 `basic_readings` | MPU-6050 on A4/A5 from the Uno's 3.3 V, AD0 pulled down (0x68), as on a GY-521 | Found, reset and set to +-8 g, +-500 deg/s and 21 Hz; a 30 degree tilt turning at 45 deg/s as 4.90 and 8.49 m/s^2 and 0.79 rad/s, then -9.81 m/s^2 on its side |
| Adafruit_NeoPixel `simple` | 16-LED WS2812B ring, DIN on D6 through 470 Ohm | The library's bit-banged 800 kHz frames decoded by the ring: one more LED green (0, 150, 0) every 500 ms |
| DallasTemperature `Simple` | DS18B20 on D2 with a 4.7 kOhm pull-up, OneWire's bit-banged slots | The sensor found by the search algorithm, a 12-bit conversion polled to its end, the scratchpad read by MATCH ROM with its CRC: 21.37 C as 21.375 (printed 21.37), then -10.06 C |
| LiquidCrystal `HelloWorld` | 16x2 HD44780 LCD in 4-bit mode, contrast pot on V0 | Nothing shown while the contrast pot sits at half (2.5 V of contrast), then 'hello, world!' and the seconds once it is turned, at the controller's real execution times |
| LiquidCrystal_I2C `HelloWorld` | 20x4 HD44780 LCD behind a PCF8574 backpack (A0..A2 pulled up, P3 switching the backlight transistor) on A4/A5 | The sketch's four rows, rows 3 and 4 at the controller's 0x14 and 0x54, written nibble by nibble through the expander at 0x27; the backlight off until `backlight()` |
| NewPing `NewPingExample` | HC-SR04, TRIG on D12, ECHO on D11 | The echo the sound's round trip at the air's temperature (100 cm: 5824 us at 20 C, 6037 us at 0 C) as NewPing prints it (102 and 105 cm), out-of-range targets as 0 |
| Servo `Sweep` | SG90-class servo on D9 | The horn holding 90 deg until the library's first pulse (32.8 ms: a full Timer1 wrap), turning at the rated 625 deg/s at 5 V, then following the sweep 1 deg (10.3 us, past the 10 us dead band) every 15 ms |
| Servo `Knob` | Potentiometer on A0, servo on D9 | The knob read and mapped to the horn (half: 2.500 V on the 511/512 boundary, 89 deg), the horn turning at its rated speed when the knob moves |
| DHT-sensor-library `DHTtester` | DHT22 on D2 with a 10 kOhm pull-up | Every read decoded, temperature and humidity as set; the sensor sending its previous measurement and its element lagging a step from 23.4 to -5.3 C with its 10 s response time (23.4, 23.4, 20.6, 15.9 C) |

The core is LGPL-2.1 and is not vendored here; `run.py` takes a checkout of
it:

```sh
git clone https://github.com/arduino/ArduinoCore-avr /tmp/ArduinoCore-avr
python3 tests/vendor/arduino/run.py --core /tmp/ArduinoCore-avr [--libs /tmp/arduino-libs --fetch] [--embsim build/dev/bin/embsim]
```

It compiles every file of `cores/arduino` into `core.a` with avr-gcc and
the flags of the core's `platform.txt` (`-Os -flto`, `--gc-sections`) and
the Uno's `boards.txt` entry; turns each `sketches/<name>/<name>.ino` into
C++ as the IDE's builder does (`Arduino.h` included, a `#line` back to the
sketch); compiles the core's libraries the sketch includes (`libraries/<name>/src`,
SPI and Wire here) as the builder finds them; links it; and runs `project.json` (the Uno, a potentiometer on A0,
a button on D2, a terminal on D0/D1, a 25LC040 on the SPI pins with its CS
pulled up, a 24C02 on A4/A5 with 4.7 kOhm pull-ups, D3 wired to D8) under the sketch's `scenario.json`.
Without avr-gcc or a core it skips.

CI's linux-gcc job runs it against commit 11b9130 of the core with Ubuntu
24.04's avr-gcc 7.3.0. Blink links to 924 bytes of flash, the size the IDE
reports for it on an Uno.
