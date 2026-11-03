# Uploading from the Arduino IDE

A simulated Uno is programmed the way a real one is (ADR-031, ADR-032): its
ATmega328P carries the Uno's fuses and Optiboot in its boot section, its
ATmega16U2 bridge is a serial port on your computer, and the IDE's Upload
button runs the same avrdude command it runs for a board on a USB cable.
Linux and macOS (Windows has no pseudo-terminals yet, Q16).

## 1. An Uno with its boot loader

A real Uno ships with Optiboot. EmbSIM does not ship it (it is GPL-2.0,
ADR-029): give the board an image that holds it, either

- the boot loader alone, from your Arduino installation:
  `~/.arduino15/packages/arduino/hardware/avr/<version>/bootloaders/optiboot/optiboot_atmega328.hex`
  (macOS: `~/Library/Arduino15/...`), or
- a sketch with it: the IDE's **Sketch > Export Compiled Binary** writes
  `<sketch>.ino.with_bootloader.hex`.

```json
"boards": [{"id": "B1", "board": "emblink/arduino_uno",
            "firmware": {"image": "optiboot_atmega328.hex", "format": "hex"}}]
```

Power it as the USB cable does (a 5 V source on `B1.5V`, as in
`examples/uno-blink-serial`). The board's fuses are the Uno's (low 0xFF,
high 0xDE, extended 0xFD, lock 0xCF); a project can change them under the
board's `settings.fuses`.

## 2. Its serial port on your computer

```sh
embsim serve project.json --serial B1 --run
# embsim: B1's serial port is /dev/pts/3
```

`--run` runs the session at real time with nothing attached; in the
workbench, the Serial panel's **Open on this computer** and Run do the same.

## 3. The IDE finds it

Copy `tools/arduino/hardware/embsim` into your sketchbook's `hardware`
folder (`~/Arduino/hardware/embsim/avr/...`) and restart the IDE. It adds
no boards, only a port discovery: **Tools > Port** then lists
`/dev/pts/3 (EmbSIM B1: Arduino Uno R3 ...)`, identified as an Arduino Uno
by its bridge's USB identity (2341:0043). Choose **Arduino Uno** as the
board and that port, and press **Upload**.

The upload is the real one: avrdude opens the port (the auto-reset),
Optiboot answers at 115200 baud, each page is erased and written by SPM in
4.1 ms and read back to verify, and the sketch starts when avrdude leaves.
The Serial Monitor resets the board as it does a real Uno; Optiboot waits
its second for a programmer, then the sketch runs. What was uploaded stays
in flash through a power cycle (a reset); `control.reset` with
`reload_images` loads the project's image again.

## From the command line

```sh
python3 tools/arduino/hardware/embsim/avr/embsim-discovery.py --list
avrdude -c arduino -p m328p -P /dev/pts/3 -b 115200 -U flash:w:sketch.hex:i
arduino-cli upload -p /dev/pts/3 --fqbn arduino:avr:uno sketch/
```

avrdude reports `ioctl("TIOCMGET")` warnings: a pseudo-terminal has no
modem lines. The reset comes from the port being opened instead, so the
upload works the same.

`tests/vendor/arduino/optiboot.py` runs this upload in CI, against the
unmodified Optiboot of ArduinoCore-avr.
