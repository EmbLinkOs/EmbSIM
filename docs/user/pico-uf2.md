# Programming a Pico with BOOTSEL and a UF2 file

A simulated Pico is programmed the way a real one is (ADR-031): plug it into
a computer with BOOTSEL held, a drive named RPI-RP2 appears, and a `.uf2`
file copied onto it is written to flash; the Pico then reboots and runs it.
In EmbSIM the computer is an `emblink/usb_host` on the Pico's micro-USB
(its `VBUS`, `USB_DP` and `USB_DM` ports), and the copy is its `copy` input.

## 1. The circuit

```json
"components": [
  {"id": "PC", "component": "emblink/usb_host", "params": {"copy_file": "blink.uf2"}}
],
"nets": {
  "VBUS": ["PC.VBUS", "B1.VBUS"],
  "DP": ["PC.DP", "B1.USB_DP"],
  "DM": ["PC.DM", "B1.USB_DM"],
  "GND": ["PC.GND", "B1.GND"]
}
```

`copy_file` is relative to the project. The Pico SDK writes a `.uf2` next to
each ELF it builds (picotool or elf2uf2); any UF2 file of the RP2040's
family (0xe48bff56) for flash will do.

## 2. Hold BOOTSEL as it powers up

Press the board's BOOTSEL button (`B1.SW_BOOTSEL`, input `pressed`) before
the session starts, or hold it and reset the board. The bootrom sees the
flash's chip select held low and goes to its USB boot (`mcu.usb_boot`); a
Pico whose flash holds no valid boot2 does the same on its own
(`mcu.no_boot2`). The port enumerates it: VID 0x2E8A, PID 0x0003,
"RP2 Boot", a mass storage interface (the host's `log` output shows it).
Let BOOTSEL go once the drive is there.

## 3. Copy the file

Set the host's `copy` input (`PC.HOST`, `copy`, true), in a scenario, the
workbench or a script. The host writes the file as a file manager does (the
FAT and the directory, then the data); each 512-byte UF2 block lands in
flash, its sector erased first, at the flash's own times. With the last
block in, the Pico reboots (`mcu.uf2_written`), the drive goes away, and the
new program starts. The host's `copy_result` says `copied`.

What is not there yet: picotool's PICOBOOT interface, UF2 files for RAM,
and a blank board (one with no firmware entry at all) in its USB boot.
