# TECNOIOT B07N97DD1B: advertised board evidence

**Status: identified listing, provisional hardware inventory, not executable
support.** Inspected 2026-11-04 after the owner provided the product link.
No direct physical inspection, PCB schematic or electrical capture has been
examined. An owner-supplied serial hardware identification is recorded below.

## Sources and confidence

- [Owner's Amazon listing, ASIN B07N97DD1B](https://www.amazon.fr/dp/B07N97DD1B):
  brand TECNOIOT; generic original ESP32 dual-core Wi-Fi/Bluetooth description.
  Product text requests the CP210X driver and links Espressif's ESP32 datasheet,
  TRM and Nicholas3388/LuaNode. That repository link does not prove which PCB
  this seller ships.
- [Listing's main photograph](https://m.media-amazon.com/images/I/61siWrmZEVL._AC_SL1239_.jpg):
  ESP-WROOM-32 module shield, two rows of 15 pads, Micro-USB connector,
  EN and BOOT switches. Header labels are transcribed below. The regulator
  appears marked AMS1117; its full suffix/rating and the circuit remain unverified.
- [Espressif ESP32 Series Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
  supplies SoC restrictions. It is not a schematic for this development board.

Do not call this an Espressif DevKitC V4 or claim an authenticated DOIT PCB.
Its 30-pin layout resembles boards commonly sold as ESP32 DevKit V1, but that
name alone does not establish manufacturer, revision or electrical equivalence.
Marketplace images and shipped units can differ.

## Hardware verification update (2026-11-05)

Owner-supplied esptool v5.6.0 output reports **ESP32-D0WD-V3 revision v3.1**,
a **40 MHz crystal**, **JEDEC C4 60 16** and **4 MB flash**. Esptool reports a
3.3 V flash setting from a strapping pin; this is not a measured rail voltage.
Giantec GT25Q32A-U V2.5 section 9.2 lists that exact JEDEC response, supporting
a Giantec GT25Q32-class match. Exact package/ordering suffix and die revision
remain unconfirmed. The output and primary manufacturer reference are in the
[hardware and boot investigation](esp32-boot-verification.md).

The listing itself still does not identify a complete flash part or authenticate
the photo/PCB revision. The Winbond W25Q32JV-IQ EF 40 16 fixture does not match
the physical readout and remains a separate test profile. Serial download/stub
success is not full ROM/SDK application boot evidence. No runtime part was
switched or relabeled from this report alone.

## Header transcription

Orientation: component side facing the viewer, antenna at top, USB connector
at bottom. Position 1 is nearest the antenna in each row. These are local
reference positions, not vendor-assigned physical pin numbers.

| Position | Left silkscreen | Signal | Right silkscreen | Signal |
|---|---|---|---|---|
| 1 | EN | Chip enable/reset | D23 | GPIO23 |
| 2 | VP | GPIO36 / SENSOR_VP | D22 | GPIO22 |
| 3 | VN | GPIO39 / SENSOR_VN | TX0 | GPIO1 / UART0 TX |
| 4 | D34 | GPIO34 | RX0 | GPIO3 / UART0 RX |
| 5 | D35 | GPIO35 | D21 | GPIO21 |
| 6 | D32 | GPIO32 | D19 | GPIO19 |
| 7 | D33 | GPIO33 | D18 | GPIO18 |
| 8 | D25 | GPIO25 / DAC1 | D5 | GPIO5 |
| 9 | D26 | GPIO26 / DAC2 | TX2 | GPIO17 / default UART2 TX |
| 10 | D27 | GPIO27 | RX2 | GPIO16 / default UART2 RX |
| 11 | D14 | GPIO14 | D4 | GPIO4 |
| 12 | D12 | GPIO12 | D2 | GPIO2 |
| 13 | D13 | GPIO13 | D15 | GPIO15 |
| 14 | GND | Ground | GND | Ground |
| 15 | VIN | Board input rail, routing unverified | 3V3 | Board 3.3 V rail |

UART labels describe conventional routing; firmware can reconfigure the GPIO
matrix. GPIO34/35/36/39 are input-only and do not provide software-controlled
internal pull-up/pull-down resistors. They must not acquire output drivers in
the simulator. The module's flash connections must not become additional
user headers. Strapping includes GPIO0/2/5/12/15; GPIO0 is not on these headers
but BOOT interaction and reset-time sampling still matter. GPIO12 can affect
flash supply configuration; reproduce behavior for the verified module/eFuses,
not a universal boot-success assumption. GPIO1/3 share the console bridge.

The photo is insufficient to determine a user LED's GPIO or polarity. Do not
invent a GPIO2 LED because other boards have one. VIN is not evidence of a
safe 5 V output, reverse-power protection, regulator headroom or allowable input
range. GPIOs are 3.3 V signals, not assumed 5 V tolerant.

## Unresolved hardware evidence

Before publishing an electrically qualified board model, establish:

- Actual PCB/module revision and full eFuse configuration. Silicon/revision is
  now hardware-reported as ESP32-D0WD-V3 v3.1.
- Exact flash ordering/package/revision and whether any PSRAM is fitted. JEDEC
  C4 60 16 / 4 MB and the Giantec datasheet match are now recorded; the photo
  does not establish PSRAM. Do not silently select a WROVER configuration.
- USB bridge exact device and wiring; CP210X in the product text proves neither
  a specific CP2102 variant nor its oscillator, power or flow-control connections.
- Regulator suffix, power routing, protection, capacitor values and tolerances.
- EN/BOOT pulls, RC timing and auto-reset DTR/RTS transistor network.
- LED count, nets, series resistors, polarity and button contact behavior.

Continuity measurements, clear PCB photos and the correct schematic can resolve
these. Firmware chip/flash identification can supplement them. These unknowns
do not block the shared Xtensa CPU implementation in the
[implementation gates](esp32-dual-core.md), but they do block claiming every
board detail matches the shipped unit.
