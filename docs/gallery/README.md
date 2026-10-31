# Gallery

Screenshots of the workbench at each milestone, to show how EmbSIM
evolves. Newer milestones come first. The milestone sets from M1 on are captured by
`ui/apps/workbench/scripts/screenshots.mjs` (the same views each time:
`pnpm build`, then `node scripts/screenshots.mjs --out ../../../docs/gallery/<milestone>`).

## Parts from KiCad's 3D models (2026-10-31)

Stage 3 of [the 3D plan](../engineering/bench-3d.md): a part may be drawn
from a 3D model file. The Bourns 3296W trimmer and the buzzer are KiCad's
own models, standing on their pins (quality render):

| The 3296W trimmer (P1) | The 12 × 9.5 mm buzzer (BZ) |
|---|---|
| ![KiCad's 3296W model, blue with a slotted brass adjuster](bench-3d-models/1-trimmer-kicad.png) | ![KiCad's buzzer model with its sound port](bench-3d-models/2-buzzer-kicad.png) |

## Boards as boards: outline, holes, edge, mask (2026-10-30)

Stage 4's first step: each board's PCB is cut from its artwork's own
outline, its mounting holes are drilled through with plated barrels and
rings, its edge is the pale glass-epoxy core, and its top is the artwork
under a glossy soldermask.

| The Uno before (a box with a picture) | After: the outline's chamfers and step, holes through to the desk |
|---|---|
| ![The Uno as a rectangle](bench-3d-pcb/1-uno-before.png) | ![The Uno's real outline and holes](bench-3d-pcb/2-uno-after.png) |

| From the top | The ESP32-C3 DevKit's rounded corners |
|---|---|
| ![The Uno from above](bench-3d-pcb/3-uno-top.png) | ![The ESP32-C3 DevKit](bench-3d-pcb/4-esp32c3.png) |

A long jumper (the supply's leads across the desk) used to arch over the
bench; now it falls from its socket, lies on the desk, and climbs to the
breadboard, resting on whatever is under it:

| Before | After |
|---|---|
| ![Leads arched high above the bench](bench-3d-pcb/0-arched-leads-before.png) | ![Leads draped onto the desk](bench-3d-pcb/5-draped-leads.png) |

The selected part (and the part under the pointer) is outlined as CAD tools
draw it, and a view cube in the corner turns with the camera:

| The Uno selected | The view cube, isometric |
|---|---|
| ![The Uno outlined](bench-3d-pcb/6-selected-outline.png) | ![The cube showing Top, Front and Right](bench-3d-pcb/7-view-cube.png) |

The exploded view lifts the parts plugged into the breadboard off it (the
breadboard, under the pointer, is outlined):

![The resistor, LED, button and pot above their holes](bench-3d-pcb/8-exploded.png)

## Packages at their standard dimensions (2026-10-30)

Stage 2 of [the 3D plan](../engineering/bench-3d.md): parts drawn from the
JEDEC and IPC dimension tables, as KiCad's generator draws them.

| The Uno before | After: socket strips with openings and contacts, the DIP-28 in its socket, the USB-B mouth, the barrel jack's bore, the tactile switch |
|---|---|
| ![Headers as solid blocks, boxes for the connectors](bench-3d-packages/1-uno-before.png) | ![The same view with the parts as made](bench-3d-packages/2-uno-after.png) |

| DIP-16, electrolytic (stripe, vent), HC-49, TO-220, TO-92, 6 mm switch, LED | ESP32-C3-MINI-1 (shield can, antenna), micro-USB | NUCLEO's LQFP on four sides |
|---|---|---|
| ![A row of through-hole packages](bench-3d-packages/3-packages.png) | ![The ESP32-C3 DevKit](bench-3d-packages/4-esp32c3.png) | ![The ST-Link's QFP](bench-3d-packages/5-nucleo-qfp.png) |

## The 3D bench as an inspection viewer (2026-10-30)

`examples/uno-breadboard` at 1440 × 900, the circuit running. These are
stage 1 of [the 3D plan](../engineering/bench-3d.md): EDA-style views, an
orthographic camera, sections, a height plane, measurement and a quality
render.

| Draft render | Quality render (occlusion, bloom on the lit LED) |
|---|---|
| ![The bench in the draft render](bench-3d-inspect/1-draft.png) | ![The same view in quality: the blue LED glows](bench-3d-inspect/2-quality.png) |

| Top, orthographic (Z) | Section across Y at 0 mm | Height plane at 12 mm |
|---|---|---|
| ![Top orthographic view](bench-3d-inspect/3-top-orthographic.png) | ![The front half of the bench cut away, the desk whole](bench-3d-inspect/4-section.png) | ![The amber plane, with the parts through it boxed in red](bench-3d-inspect/5-height-plane.png) |

## Schematic labels and empty views (2026-10-28)

The demo session at 1440 × 900, dark theme, before and after. Part type names
were drawn at the browser's default 16 px: their rule named an undefined font
variable, which voids the whole declaration. Each net's live reading sat under
its name, where the wire runs. Now the reading sits on the name's line,
and part labels keep clear of net labels.

| Before | After |
|---|---|
| ![Oversized type names and readings crossed by the wires](ui-polish/before-design.png) | ![Reference, type and value at their sizes, readings beside the net names](ui-polish/after-design.png) |

A view that cannot work yet (Debug without a board running firmware, a report
before any scenario has run) says what it does and what it needs, each
requirement met or missing, instead of a line of text in the corner.

| Before | After |
|---|---|
| ![Debug's requirements as plain text at the top left](ui-polish/before-debug.png) | ![Debug's requirements as a checklist in a centred card](ui-polish/after-debug.png) |

[Verify in the light theme](ui-polish/after-verify-light.png).

## Live schematic operation and observation (2026-10-26)

Actual native sessions, captured by `tests/browser/test_live_schematic.py`.
The live circuit shows five LED colors, independent RGB currents, an antiparallel
bicolor die and one selected matrix pixel. HOLD/LATCH and the loaded potentiometer
operate through native inputs. The A/B readout uses ideal zero-loading probes;
the wired voltmeter contributes 10 MΩ. The sheet has no undrawn connections.

![Native schematic lights, controls, matrix and loaded meter](live-schematic/live-schematic.png)

The display showcase's functional models write LCD text, SSD1306 pixels and
MAX7219 digits directly on the sheet. This capture does not qualify individual
segment currents, physical display optics or missing controller modes.

![Native functional LCD, OLED, segment, bargraph and serial display states](live-schematic/live-displays.png)

The ESP32-C3 firmware example has all 14 schematic connections routed. Its MODE
contact is held on the schematic while firmware executes. Electrical LEDC outputs
select mixed PWM, off and full-on; the native 39-assertion scenario still passes.
Light uses accepted-time interval emission and illustrative 4× display gain;
measurements remain raw electrical values, not calibrated photometry.

![Firmware PWM and native mixed RGB light on a wired schematic](live-schematic/firmware-mixed-schematic.png)

| Off | Full-on |
|---|---|
| ![Native firmware contact selects off](live-schematic/firmware-off-schematic.png) | ![Native firmware contact selects full-on](live-schematic/firmware-on-schematic.png) |

No page errors or optional 3D asset requests occurred. The browser also checks
keyboard controls, interrupted holds, moved voltage leads, exposure selection,
and schematic bounds at 900×760 and 1280×800. See the
[contract, compatibility limits and remaining completion gates](../engineering/live-schematic.md).

## Firmware-driven RGB and measured waveforms (2026-10-26)

Actual native `embsim serve` on `examples/esp32c3-qualified-rgb`. The ESP32-C3
boots its flash image through the ROM and programs LEDC, driving three named
RGB junctions through finite GPIO drivers and separate 100 Ω resistors. The
mixed capture shows 1 kHz, R/G/B duties of 25/50/75%, red mean 2.996 mA and
RMS 5.991 mA, with 100% delivered capture coverage. These match CLI electrical
measurements; the scene's instantaneous LED glow is not optical/PWM qualification.

![Native PWM current traces and digital duty measurements](indicator-pwm/mixed-measurements.png)

Holding the physical MODE input while advancing firmware selects off and then
full-on. At full-on the red mean/RMS is 11.98 mA; selecting its GPIO voltage
trace switches the scope's unit group and shows approximately 3.060 V under load.
All captures have no browser page errors and no engine error/fatal diagnostics.
The boot ROM retains the existing warning for unmodeled SENSITIVE behavior.

| Off | Full-on |
|---|---|
| ![Firmware mode input turns the RGB currents off](indicator-pwm/off-measurements.png) | ![Firmware mode input turns all three RGB channels fully on](indicator-pwm/full-measurements.png) |

![Loaded GPIO voltage on a separate voltage axis](indicator-pwm/loaded-gpio.png)

![Authoring a time-mean current assertion in the native scenario editor](indicator-pwm/measured-scenario.png)

[Measurement contract, delivery bug and limitations](../engineering/indicator-pwm.md).
[Circuit, firmware and rebuild instructions](../../examples/esp32c3-qualified-rgb/README.md).

## Named RGB indicator and source evidence (2026-10-26)

The native `examples/qualified-rgb-driver` circuit uses Kingbright's
WP154A4SUREQBFZGC, its R/COM/B/G terminal order, independent typical DC curves,
220 Ω limiting resistors and representative 25 Ω drivers. Pressing red and blue
produces approximately 12.788 mA red, 8.404 mA blue and 3.80 nA green at 5 V.
The observed illustrative color is `#ff00a8`; unequal electrical currents do not
become equal merely because two switches are pressed. Both captures use the
actual native host, with no engine diagnostics or browser page errors.

| Switches released | Red and blue powered |
|---|---|
| ![Named RGB circuit with all switches released](indicator-qualification/rgb-off.png) | ![Named RGB circuit with independently solved red and blue currents](indicator-qualification/rgb-red-blue.png) |

![Source revision, hash and bounded DC evidence beside the named RGB package](indicator-qualification/rgb-evidence.png)

The inspector exposes the 25 °C, 2–20 mA curve envelope, tolerance, independent
holdouts and exact source snapshot. Body envelope and lead pitch follow the
source drawing; formed leads, lens curvature and displayed light remain
representative. These images establish integration, not full optical, thermal,
damage or mechanical qualification. [Evidence and reproduction](../engineering/indicator-qualification.md).

## Circuit construction and electrically driven RGB (2026-10-25)

Actual native `embsim serve` on `examples/rgb-indicator-bench`, in electrical
policy. Physical switch inputs, separate current-limiting resistors and three
LED junctions determine the scene; the browser does not animate colors itself.
The off capture precedes pressing red and blue. The magenta capture is paused
with engine observations of approximately 19.994 mA red, 19.991 mA blue and
3.70 nA green, color `#ff00ff`, and no engine diagnostics or browser page errors.
The wire editor/tracer operates on that same project. These captures establish
integration and inspection, not manufacturer photometric or mechanical fidelity.

| Off | Red and blue powered |
|---|---|
| ![Native RGB circuit with all switches released](circuit-construction/rgb-off.png) | ![Native RGB circuit emitting magenta from measured channel currents](circuit-construction/rgb-magenta.png) |

## Responsive workspaces (2026-10-25)

The same 1024 × 900 demo project before and after: the Perspective selector and
utilities had been beyond the right edge. Global navigation now has its own row,
with simulation controls below. These are actual built workbench captures.

| Before, 1024 px | After, 1024 px |
|---|---|
| ![The former toolbar clips navigation beyond the window](workspace-layout/before-1024.png) | ![Perspective navigation and utilities remain visible at 1024 px](workspace-layout/after-1024.png) |

The current native `embsim serve` host, paused on `examples/uno-breadboard`, at
1440 × 900. The schematic-only ribbon is absent from the physical bench. Parts &
wiring is folded for inspection. The initial schematic made no renderer request;
opening Bench fetched its 3D chunk once and rendered without page errors. This
is a host/layout inspection, not a new electronics or frame-rate qualification.

![The native Uno and breadboard with the revised workspace](workspace-layout/native-bench.png)

The demo session also exercises a 900 × 760 window with Library and Inspector
open, plus the settled dark theme at 1440 × 900. The compact canvas keeps its own
view/tool controls available; panel constraints do not replace stored proportions.

| Compact panels | Dark theme |
|---|---|
| ![The compact workspace with editable Inspector and canvas controls](workspace-layout/compact.png) | ![The same workbench after the dark-theme transition settles](workspace-layout/dark.png) |

[Light schematic](workspace-layout/schematic.png).
[Workspace behavior and limits](../engineering/workspace-layout.md).

## The air, as phones and sniffers see it (2026-10-22)

The ESP32-C3 radios plan, step 7, and the network beyond the router. On
the left, the DevKitM-1 runs ESP-IDF's `protocols/http_request` as it
ships: a phone's Wi-Fi analyzer beside the bench lists the home router
and the neighbour's, and a sniffer on channel 6 captures the board's
probe, authentication, association, the WPA2 4-way handshake (EAPOL's
messages 1 to 4) and then protected data, as Wireshark would without the
key. On the right, two DevKitM-1s run NimBLE's `bleprph` and Bluedroid's
`ble_ibeacon`: the phone's scanner lists both (the iBeacon's UUID, major
and minor), and the sniffer on channel 37 captures their advertising, the
phone's SCAN_REQ and the SCAN_RSP 150 us later. Each capture is saved as
a pcap file Wireshark opens.

| Wi-Fi | Bluetooth LE |
|---|---|
| ![http_request associating and keying, seen by a phone and a sniffer](radio/air-wifi.png) | ![bleprph and an iBeacon advertising, a phone scanning them](radio/air-ble.png) |

![http_request fetching example.com through the router: the DNS lookup, the socket and the page](radio/http-request-serial.png)

## The ESP32-C3-DevKitM-1 qualified (2026-10-22)

The DevKitM-1's artwork checked against Espressif's V1.0 dimension
drawing (`catalog/boards/esp32c3_devkitm1/reference.json`), read from the
PDF's vector paths: the outline, the 30 header pads and the micro-USB's
body, and every other part placed from its pads. The pads had been
0.39 mm high, the RGB LED 1.1 mm and the micro-USB 0.64 mm out; the header
labels are now the board's narrow vertical silkscreen, beside the pads.

| Before | Qualified |
|---|---|
| ![The earlier artwork](qualified-c3/artwork-before.png) | ![The artwork as the drawing places it](qualified-c3/artwork.png) |

## The ESP32-C3-DevKitM-1 (2026-10-22)

The RISC-V board of the RTOS boards plan, step 6 of its part 2:
Espressif's ESP32-C3-DevKitM-1, from its V1.0 schematic, with the
ESP32-C3-MINI-1 module, the SGM2212 regulator, the RST and BOOT buttons and
the SK6812MINI-HS RGB LED on GPIO8. It runs ESP-IDF v5.4's hello_world from
its flash image: the ROM, the second-stage bootloader and the application,
built unmodified, counting down to its restart one second at a time on the
CP2102N's serial port. The artwork is drawn for EmbSIM to the board's
25.4 x 38.91 mm outline.

| Design | Run |
|---|---|
| ![The DevKitM-1 project in the Design workspace](esp32c3-devkitm1/design-c3.png) | ![hello_world's countdown on the serial terminal, the board on the 3D bench plugged into USB](esp32c3-devkitm1/run-c3.png) |

![The DevKitM-1's artwork: the module, the regulator, the RGB LED, BOOT and RST](esp32c3-devkitm1/board-c3.png)

## Two NUCLEO-F446REs over I2C (2026-10-19)

Step 10 of the NUCLEO-F446RE plan: STM32CubeF4's own examples, built
unmodified. Here two boards run I2C_TwoBoards_ComIT, wired as its readme
draws them: I2C1's SCL (D10) and SDA (D14) joined, with the grounds, from
one USB supply. B1 runs the example as it is, the master; B2 runs it with
`MASTER_BOARD` commented out, the slave. The project is the one
`tests/vendor/stm32cubef4/run.py` generates.

![Two NUCLEO-F446REs on the bench, SCL and SDA wired between their Arduino headers](nucleo-f446re/two-nucleos-i2c.png)

## The NUCLEO-F446RE (2026-10-19)

The board EmbLinkRTOS is developed against, step 9 of its plan: ST's
NUCLEO-F446RE with its STM32F446RE at 180 MHz from the ST-LINK's MCO,
running `examples/nucleo-f446re-blink-serial`. The board's jumpers are
controls: JP5 on U5V or E5V, JP6 (IDD) fitted or removed for an ammeter.
The artwork is drawn for EmbSIM to the 70 x 82.5 mm outline. Its header
positions are approximate until a dimensioned drawing gives
`reference.json`.

| Design | Run |
|---|---|
| ![The NUCLEO-F446RE example in the Design workspace](nucleo-f446re/design-nucleo.png) | ![The example running: VDD on the oscilloscope, LD2 and B1 on the logic analyzer, the jumpers among the controls](nucleo-f446re/run-nucleo.png) |

![The NUCLEO-F446RE's artwork while it runs: LD3 and LD2 lit](nucleo-f446re/board-nucleo.png)

![Stopped in SysTick_Handler at 180 MHz in the Debug workspace](nucleo-f446re/debug-nucleo.png)

## The Uno R3 qualified (2026-10-18)

The first step of the visual benchmark's V1. The Uno's artwork, before and
after being redrawn from reference data: Arduino's A000066 datasheet for
the outline and mounting holes, KiCad's `Arduino_UNO_R3` footprint for
every header pad. The power and analog headers had been about 4 mm to the
left, and D8 to SCL had lacked the 160 mil offset from D7; the right-hand
holes, the outline's steps and the USB-B socket had been placed by eye. The
validator now holds the artwork to the reference within 0.05 mm, and the
3D bench's header follows it.

| Before | After |
|---|---|
| ![The earlier Uno artwork: headers out of place](qualified-uno/artwork-before.png) | ![The Uno artwork from reference data](qualified-uno/artwork.png) |

![The qualified Uno on the 3D bench](qualified-uno/bench.png)

The Design workspace at 1440 × 900, before and after: the Inspector is a
rail while nothing is selected, so the bench grows from 44 % to 59 % of the
width, and to about 73 % with its parts list folded from the toolbar.

| Before | After |
|---|---|
| ![The workspace before: an empty Inspector beside the bench](qualified-uno/workspace-before.png) | ![The workspace after: the Inspector folded while nothing is selected](qualified-uno/workspace-after.png) |
## Named surface-mount bodies (2026-10-18)

Native `embsim serve` captures of isolated BSS138 and LM1117IMP-3.3 parts at
1440×900 in Chromium: SOT-23 with two front leads and one rear lead, and
SOT-223 with three front leads and its rear output tab. The package inspector
explains carrier-board use. These are geometry inspections without a powered
circuit. See the [source and geometry review](../engineering/package-geometry.md)
for dimensions, terminal identities and representation limits.

| BSS138, SOT-23 | LM1117IMP-3.3, SOT-223 |
|---|---|
| ![BSS138 with its SOT-23 body and lead arrangement](surface-packages/bss138.png) | ![LM1117 with its SOT-223 leads and output tab](surface-packages/lm1117.png) |

## Physical assembly previews (2026-10-18)

The native Uno/breadboard session, paused, in top inspection. The selected R1's
first leg is proposed at c24, already occupied by D1.A: the preview names the
conflict and disables insertion. Changing it to b20 uses free holes on the same
strips. Coloured rings are viewing aids, not physical parts. The coordinate
controls edit the bench in millimetres independently of schematic placement.

| Occupied hole, rejected | Free holes on the same strips |
|---|---|
| ![Native insertion preview identifies D1.A and blocks the conflicting placement](bench-assembly/occupied.png) | ![Native insertion preview accepts free breadboard holes](bench-assembly/free-strip.png) |

## The Uno's serial port on the host (2026-10-18)

The Serial panel's "Open on this computer" puts the board's ATmega16U2
bridge on the host as a pseudo-terminal (ADR-031). Here `uno-blink-serial`
runs at real time and a Python program on the host opens `/dev/pts/0` at
9600 baud: the Uno resets as its DTR capacitor makes it, and the program
reads the sketch's banner, then its A0 readings, while the panel shows the
port open.

| | |
|---|---|
| ![The port on the host, no program on it yet](host-serial/serial-port.png) | ![A program on the host has it open at 9600 baud](host-serial/serial-port-open.png) |

## Bench inspection and wiring (2026-10-18)

The native `embsim serve` workbench with `examples/uno-breadboard`: paper and
graphite surfaces, a top camera, optional notes, and the expanded canvas after
folding the keyboard list and side panels. These are captures of the paused native
session in Chromium at 1440×900, not mock-ups or proof of mechanical qualification.
Named contact feedback, cancellation and live button release have separate browser
regressions. See the [bench guide](../user/bench.md) for controls and limits.

| Paper with notes and a focused canvas | Top inspection with notes hidden |
|---|---|
| ![Native Uno and breadboard on the paper bench](bench-inspection/paper.png) | ![Top-down inspection of the same native circuit](bench-inspection/top.png) |

[Graphite surface, perspective inspection](bench-inspection/graphite.png).

## The world (2026-10-18)

The room the circuit sits in (ADR-026). `NewPing/NewPingExample` declares a
20 °C room and a wall a metre behind the HC-SR04; the World tab sets each
field the room declares and places the bodies distance sensors see, and the
bench draws the sensor's beam to what it hits. Running, the controls show
U1's distance and temperature read from the world; a scenario moves the wall
and the sketch's `Ping:` line follows it.

| | |
|---|---|
| ![The World tab under the bench: the room's temperature, the wall's place](world/world-design.png) | ![Running: U1's distance and temperature from the world's](world/world-run.png) |


## The 3D bench and the breadboard (2026-10-18)

The bench becomes a second editor of the project's nets (ADR-027), in 3D.
`examples/uno-breadboard` is the uno-blink-serial circuit built on an
830-point breadboard: the resistor, the blue LED, the button and the
potentiometer plugged into its strips, the rails fed from the Uno's 5V and
GND, jumpers to D2, D9 and A0, a USB-serial adapter on D0 and D1. The same
firmware and scenario pass on it. Each part is built from its package (the
resistor's orange, orange, brown bands read from its 330 ohms, the LED's
dome, the pot's knob), the Uno from its description's features (USB-B
socket, barrel jack, the ATmega328P in its socket, the reset button, the L
and ON LEDs) with its artwork as the PCB's face. Running, the LEDs light
from their luminance. The list beside the scene shows where each leg is and
plugs parts and adds jumpers from the keyboard; the schematic hides the
breadboard and shows the connections it makes.

| | |
|---|---|
| ![The breadboard build in the Design perspective's Bench view](bench-3d/design-bench.png) | ![The Uno up close: its USB socket, jack, reset button and DIP-28, the L and ON LEDs lit, Dupont jumpers in its headers](bench-3d/uno-closeup.png) |
| ![The breadboard up close: the lit blue LED, the resistor, the button and the pot in their strips, flat jumpers to the rails](bench-3d/breadboard-closeup.png) | ![The same project's schematic: the breadboard is not drawn, its connections are](bench-3d/design-schematic.png) |

[The Run perspective, the bench live at 100 % of real time](bench-3d/run-bench.png).
[The parts as the bench builds them](bench-3d/parts.png): the HC-SR04's
transducers, the DHT22's grille, breakouts printed with their pin names,
the LCD's live face, a TO-220, a TO-92, resistors banded by their value,
an LED, diodes, capacitors, a buzzer, a tactile switch and a pot.

## The bench, first step (2026-10-18)

The Board view becomes a bench: the parts labelled above the board, the
project's nets drawn as wires from each artwork's own pins (ground black,
supplies red, signals in colours), routed square and round the board
rather than across it. The clock shows how fast the run goes against real
time (here 24 %: the stepper's project runs under the electrical policy).

| | |
|---|---|
| ![TM1637Test on the bench: CLK and DIO to D2 and D3, VCC and GND round the Uno to its power header](bench/bench-tm1637.png) | ![The stepper's bench: the ULN2003 board's IN pins to D8 to D11, its connector to the motor's five leads](bench/bench-stepper.png) |

[The stepper in the Run perspective, the clock at 24 % of real time](bench/run-stepper.png).

## M6 parts on the bench (2026-10-18)

Parts run by their libraries' own examples, unmodified, on the Uno. The
TM1637 module shows TM1637Test's first step (every segment and the colon);
the 28BYJ-48 has turned its shaft through the Stepper library's 200 steps
while the ULN2003 board's LEDs A and B show the two coils the sketch left
on.

| | |
|---|---|
| ![The TM1637 display at 88:88](m6-parts/board-tm1637.png) | ![The 28BYJ-48 through its ULN2003 board: LEDs A and B lit, the shaft turned](m6-parts/board-stepper.png) |

[TM1637Test in the Design perspective](m6-parts/design-tm1637.png), [stepper_oneRevolution in the Run perspective](m6-parts/run-stepper.png).

## Workbench layout pass (2026-10-17)

Panels fold to a titled rail and unfold from it, the choice kept with the
layout. The Run perspective folds the instrument with nothing to show (the
oscilloscope of a project with only digital probes) and gives its actions to
the other. Meters read in SI prefixes down to femto (residue below that
shows as about zero) and leave growing texts to the Serial tab. A compact
window starts with the Inspector folded, and the sheet navigator steps aside
on a narrow canvas. The Debug perspective demangles C++ names.

| | |
|---|---|
| ![Run: oscilloscope folded, logic analyzer and SI meters](workbench-layout/wide-run.png) | ![Design at 1024 pixels: the Inspector folded to a rail](workbench-layout/compact-design.png) |

[Debug with demangled C++ names](workbench-layout/wide-debug.png).

The EmbSIM mark is now the project menu, with the project's name beside it:
New project (a blank sheet, or a board from the catalog to start from), Open,
Save and Save a copy, the same commands as the palette.

| | |
|---|---|
| ![New project: start from a catalog board](workbench-layout/new-project.png) | ![A new project on the Raspberry Pi Pico](workbench-layout/new-project-created.png) |

[The project menu](workbench-layout/project-menu.png).

## Arduino SPI and Wire on the Uno (2026-10-16)

A sketch built with the unmodified ArduinoCore-avr (`tests/vendor/arduino/sketches/spi_wire`)
drives the ATmega328P's new SPI and TWI models through the core's own SPI and
Wire libraries. The 25LC040's RDSR after WREN reads back status 0x02, clocked at
4 MHz through D13, whose L LED puts SCK on an analog net (the edge that once
answered a bit late). Wire's first START addresses the 24C02 at 99.9 kHz, and
the Serial terminal now joins the text the engine streams into whole lines,
one port at a time.

| | |
|---|---|
| ![Uno SPI decode: RDSR 0x05, then status 0x02](arduino-spi-wire/decode-uno-spi.png) | ![Uno TWI decode: START, 0x50 W ACK at 100 kHz](arduino-spi-wire/decode-uno-i2c.png) |

[The sketch's Wire results in the Serial terminal](arduino-spi-wire/serial-uno-spi-wire.png).

## Serial and analog peripherals on the Pico and the nRF52840 DK (2026-10-16)

Vendor firmware, unmodified drivers, against parts from the catalog: the Pico
SDK's `hardware_spi` and `hardware_i2c` on the RP2040's PL022 and DW_apb_i2c,
and nrfx's SPIM, TWIM, PWM and SAADC drivers on the nRF52840, each talking to
a 25LC040 SPI EEPROM and a 24C02 I2C EEPROM (`tests/vendor/pico-sdk/apps/spi_i2c`,
`tests/vendor/nrfx/apps/nrfx_periph`). The logic analyzer decodes the traffic
the cores drive on the pins: the SPI WREN and RDSR commands (the nRF52840's
MISO shown undriven, orange, while the EEPROM's SO is high-impedance), and an
I2C START with the 24C02's address acknowledged.

| | |
|---|---|
| ![Pico SPI decode: WREN and RDSR to a 25LC040](serial-analog-peripherals/decode-pico-spi.png) | ![Pico I2C decode: START, 0x50 W ACK](serial-analog-peripherals/decode-pico-i2c.png) |
| ![nRF52840 DK SPIM decode with MISO high-impedance](serial-analog-peripherals/decode-nrf52840dk-spi.png) | ![nRF52840 DK TWIM decode: START, 0x50 W ACK](serial-analog-peripherals/decode-nrf52840dk-i2c.png) |

The Serial terminal shows what the firmware prints: these shots are the first
since the engine streams `serial.data`, which it had never sent, leaving the
terminal empty.

| | |
|---|---|
| ![Pico SDK SPI and I2C results in the Serial terminal](serial-analog-peripherals/serial-pico-spi-i2c.png) | ![nrfx TWIM, PWM and SAADC results in the Serial terminal](serial-analog-peripherals/serial-nrf52840dk-periph.png) |
## Schematic graphics and interaction (2026-10-16)

Captured from the native engine with `tests/browser/test_schematic.py`: separate
wire/symbol colors, hollow pin sockets and nearby reference/name/effective-value
annotations. No model values or electrical connections are changed for styling.
The browser regression also checks direct wiring, exact undo and group operations.

| | |
|---|---|
| ![Colored wires and component labels on paper](schematic-graphics/light.png) | ![Theme-aware wires and labels in dark mode](schematic-graphics/dark.png) |

[Terminal and annotation detail](schematic-graphics/terminal-detail.png) ·
[Compact workspace](schematic-graphics/compact.png).

## Circuit organization (2026-10-16)

Captured against the native engine by `tests/browser/test_schematic.py`. The
Objects panel selects an input-stage group; both member resistors have locked
placement while their parameters remain editable. The regression verifies native
metadata, blocked dragging and undo before capturing light/dark/compact views.

| | |
|---|---|
| ![Named group and object browser in light theme](circuit-organization/light.png) | ![Named group and object browser in dark theme](circuit-organization/dark.png) |

[Compact 1024-pixel workspace](circuit-organization/compact.png).

## Schematic authoring and repair (2026-10-16)

Captured from the native engine by `tests/browser/test_schematic.py` after track
repair, shared-parameter editing and exact undo checks. The selected net remains
electrically connected while its drawing is cleared; the Checks report locates
the missing geometry. Bounds and connectivity checks are not electrical-rule or
physical-clearance validation.

| | |
|---|---|
| ![Native schematic checks in light theme](schematic-authoring/checks-light.png) | ![Native schematic checks in dark theme](schematic-authoring/checks-dark.png) |

[Compact 1024-pixel workspace](schematic-authoring/compact.png).

## Canvas appearance (2026-10-16)

Captured against the native engine with `tests/browser/test_canvas_appearance.py`.
The continuous background covers negative coordinates as well as the original
sheet area. Appearance preferences survive three consecutive native-server reloads.

![Continuous paper workspace in focus mode](canvas-appearance/paper-workspace.png)

| | |
|---|---|
| ![Paper engineering grid and controls](canvas-appearance/engineering-paper.png) | ![Blueprint surface in dark theme](canvas-appearance/blueprint-dark.png) |
| ![Warm surface with dots](canvas-appearance/warm-dots.png) | ![Graphite surface with crosses](canvas-appearance/graphite-crosses.png) |

## Schematic Studio (2026-10-16)

Captured against the native engine with `tests/browser/test_schematic.py`.
The [user guide](../user/schematic-studio.md) explains the controls and routing limits.

| | |
|---|---|
| ![Connectivity-driven layout preview](schematic-studio/preview-light.png) | **Preview before applying.** Connected groups placed and routed, with moved/route counts, navigation and one-step Apply/Undo. |
| ![Schematic Studio in dark theme](schematic-studio/dark.png) | **Net spotlight.** Find and focus a net while retaining circuit context; the same tools in the dark theme. |

## M5 stacks and faults (2026-10-16)

The firmware's stacks and faults measured from its core: how deep each
stack went, by function, and each fault decoded as the core took it.

| | |
|---|---|
| ![The Blue Pill's stack depths](m5-2/verify-stacks.png) | **How deep the stacks went.** After a run with the button pressed: the main stack's 40 of 20,472 bytes (down to `_ebss`), and by function the deepest stack pointer, its own frame and its depth with its callees; the interrupt handler is deepest, on the frame its entry stacked. |
| ![A HardFault decoded](m5-2/verify-faults.png) | **A fault, decoded.** `tests/firmware/fault` loads from 0x30000000, where nothing answers: the HardFault it escalates to with HFSR and CFSR in words, BFAR, the faulting `ldr`, the stacked frame, and the backtrace `peek`, `read_sensor`, `main`, `Reset_Handler` from `.debug_frame`; its handler is `Default_Handler`. |

## M5 verification (2026-10-16)

A run becomes a repeatable, measured check: scenarios edited and run in
place, recordings replayed identically or turned into scenarios, and the
firmware's coverage and profile from its core.

| | |
|---|---|
| ![The scenario editor after a run](m5/verify-scenario.png) | **Edit and run a scenario.** `scenarios/press.json` of the Blue Pill example opened in the editor: its inputs timeline and its sixteen assertions, each row with its result after the run, the report below. |
| ![Coverage of the Blue Pill's firmware](m5/verify-coverage.png) | **Coverage and profile.** After a run with the button pressed: each source file's lines and functions hit, `main.c` with each line's count, and where the cycles went by function. |
| ![Coverage, dark](m5/verify-coverage-dark.png) | **The same, dark.** The source view starts at the first line with code; lines that never ran are marked. |
| ![A recording replayed and made a scenario](m5/verify-recording.png) | **Record, replay, convert.** A session recorded while the button was held, replayed identically (291 observations), and turned into a scenario now open in the editor with its inputs and assertions. |

## Interactive component library (2026-10-16)

Captured by `tests/browser/test_library.py` against the native engine, including
real placement and live control/waveform checks. Original component artwork and
schematic symbols are rendered by the shared workbench.

| | |
|---|---|
| ![Library explorer, light](component-library/library-light.png) | **Discover and inspect.** Manufacturer/package filters, multiword search, source reference status and a preview before placement, alongside the wired optocoupler circuit. |
| ![Library explorer, dark](component-library/library-dark.png) | **Place a real package.** B3F-1000's numbered pads and model limits in dark mode. The explorer is also checked at 1024 × 700. |

## M4 core families and boards, part 5 (2026-10-16)

The Pico runs both of its RP2040's cores: core 0 launches core 1 through
its bootrom's FIFO handshake, and the two share a counter, the FIFOs and the
LED.

| | |
|---|---|
| ![The Pico's two cores running](m4-5/run-pico-dual-core.png) | **Two cores running.** `examples/pico-dual-core`: core 1 blinking the LED from its timer alarm and taking the button, core 0 printing on the terminal what core 1 answers through the FIFO. |
| ![Debugging core 1](m4-5/debug-pico-core1.png) | **Debugging core 1.** Stopped in `sio_irq_proc1` on core 1, which the toolbar names: its registers, its source line, the disassembly from flash, while core 0's banner is half printed. |

## M4 core families and boards, part 4 (2026-10-16)

The same Explorer 16/32 with its PIC24 plug-in module: an XC16 image on the
PIC24FJ1024GB610, the 8 MHz crystal through the 96 MHz PLL to a 16 MHz FCY,
UART2 on the USB-UART bridge by the Peripheral Pin Select.

| | |
|---|---|
| ![The Explorer 16/32 with the PIC24 running](m4-4/run-explorer16-pic24.png) | **The PIC24 running.** `examples/explorer16-pic24`: D3 blinking from Timer1's interrupt, the banner with FOSC, FCY and the FOSCSEL word read back by a table read on the terminal. |
| ![The PIC24 PIM up close](m4-4/board-explorer16-pic24.png) | **The PIC24 PIM.** The Explorer's artwork with the PIC24FJ1024GB610 plug-in module (MA240023), live. |
| ![Debugging the PIC24](m4-4/debug-explorer16-pic24.png) | **Debugging the PIC24.** Stopped at `_T1Interrupt`'s first instruction: XC16's line table on the source, the PIC24 disassembly at program addresses, W0 to W15, RAM at data address 0x0800, and the device pack's peripherals with their models. |
| ![The PIC24 Explorer's schematic](m4-4/design-explorer16-pic24.png) | **Design, Explorer 16/32 with the PIC24.** 5 V on VIN and the terminal on RF5 and RF4. |

## M4 core families and boards, part 3 (2026-10-16)

An Explorer 16/32 runs its PIC32MX795F512L's MIPS32 M4K: the 8 MHz crystal
through the PLL to 80 MHz from the configuration words, with firmware built
by clang and by EmbCC.

| | |
|---|---|
| ![The Explorer 16/32 running](m4-3/run-explorer16.png) | **The Explorer 16/32 running.** `examples/explorer16-pic32mx`: D3 blinking from Timer1's interrupt, S3, S6, S4 and MCLR as controls, the banner with SYSCLK and PBCLK on the terminal through UART2. |
| ![The Explorer up close](m4-3/board-explorer16.png) | **The Explorer's artwork.** The PIC32MX795F512L PIM, D3 to D10, the buttons, the crystals, the USB-UART bridge and the PICtail edge, live. |
| ![Debugging the PIC32MX](m4-3/debug-explorer16.png) | **Debugging the PIC32MX.** Stopped in `isr` at Timer1's first interrupt: the source, the MIPS32 disassembly from program flash at 0x9D000000, the MIPS registers, the RAM, and the device pack's peripherals with their models. |
| ![The Explorer's schematic](m4-3/design-explorer16.png) | **Design, Explorer 16/32.** 5 V on VIN and the terminal on the USB-UART bridge's RF5 and RF4. |

## M4 core families and boards, part 2 (2026-10-16)

A Raspberry Pi Pico boots its RP2040 through boot2 and an nRF52840 DK runs
its Cortex-M4F, each with firmware built by clang and by EmbCC.

| | |
|---|---|
| ![The Pico running](m4-2/run-pico.png) | **The Pico running.** `examples/pico-blink-serial` on the RP2040's Cortex-M0+ at 125 MHz from PLL_SYS: the LED on GPIO25 lit by the timer's alarm, the banner on the terminal. |
| ![The Pico up close](m4-2/board-pico.png) | **The Pico's artwork.** Its 40-pin edge, the RP2040, the W25Q16 flash, BOOTSEL and the user LED, live. |
| ![Debugging the RP2040](m4-2/debug-pico.png) | **Debugging the RP2040.** Stopped at the timer's alarm interrupt: the source, the Armv6-M disassembly, the registers, and RP2040.svd's peripherals with the ones RESETS holds marked "in reset". |
| ![The Pico's schematic](m4-2/design-pico.png) | **Design, Pico.** The button and the terminal on GP15, GP0 and GP1, VBUS from a 5 V source. |
| ![The nRF52840 DK running](m4-2/run-nrf52840dk.png) | **The nRF52840 DK running.** `examples/nrf52840dk-blink`: LED1 toggled by RTC1 on the 32.768 kHz crystal, the banner with FICR's part number and the FPU's sqrt(2). |
| ![The DK up close](m4-2/board-nrf52840dk.png) | **The DK's artwork.** Its four LEDs and buttons, the Arduino headers and the virtual COM port's pins. |
| ![Debugging the nRF52840](m4-2/debug-nrf52840dk.png) | **Debugging the nRF52840.** Stopped in RTC1's interrupt: the source, the Thumb-2 disassembly, the registers and nrf52840.svd's peripherals. |
| ![The DK's schematic](m4-2/design-nrf52840dk.png) | **Design, DK.** The button on D2 and the terminal on P0.06 and P0.08. |
| ![Two boards on one line](m4-2/run-two-boards.png) | **Two boards.** As in part 1. |
| ![The Uno running](m4-2/run-uno.png) | **The Uno running.** As in part 1. |
| ![The HiFive1 running](m4-2/run-hifive1.png) | **The HiFive1 running.** As in part 1. |
| ![Display showcase](m4-2/board-showcase.png) | **Displays.** |
| ![Design, Blue Pill](m4-2/design-bluepill.png) | **Design, Blue Pill.** |
| ![Debug, Blue Pill](m4-2/debug-bluepill.png) | **Debug, Blue Pill.** |
| ![Dark theme](m4-2/design-bluepill-dark.png) | **Dark theme.** |

## M4 core families and boards, part 1 (2026-10-16)

An Arduino Uno's AVR and a HiFive1's RISC-V core run their firmware, and
boards of different families share one circuit.

| | |
|---|---|
| ![Two boards on one line](m4/run-two-boards.png) | **Two boards.** `examples/two-boards-uart`: an Uno (AVR, 5 V) and a Blue Pill (Cortex-M3, 3.3 V) exchanging `ping` and `PING` over one serial line, each at its own baud rate; the Uno's L LED lit after the third round trip. |
| ![Two boards on the schematic](m4/design-two-boards.png) | **Design, two boards.** Each board a block of its ports, the link and the power by net labels. |
| ![The Uno running](m4/run-uno.png) | **The Uno running.** `examples/uno-blink-serial`: Timer1's PWM on D9 on the logic analyzer, the potentiometer's 2.5 V on A0, the board's artwork live. |
| ![The Uno up close](m4/board-uno.png) | **The Uno's artwork.** Its L and ON LEDs lit, the blue LED on D9 dimmed by the PWM. |
| ![The Uno's schematic](m4/design-uno.png) | **Design, Uno.** The potentiometer, button, LED and terminal wired to the Uno's pins by labels. |
| ![Debugging the AVR](m4/debug-uno.png) | **Debugging the AVR.** Stopped at the Timer0 interrupt (`__vector_14`): the source line, the AVR disassembly, r0-r31 and the ATmega328P's peripherals. |
| ![The HiFive1 running](m4/run-hifive1.png) | **The HiFive1 running.** `examples/hifive1-blink` on the FE310's RISC-V core. |
| ![The HiFive1 up close](m4/board-hifive1.png) | **The HiFive1's artwork.** The RGB LED's green, toggled by the CLINT's timer interrupt. |
| ![Debugging the RISC-V core](m4/debug-hifive1.png) | **Debugging RISC-V.** Stopped in the machine trap handler: the source, the compressed instructions, the registers by their ABI names. |
| ![Display showcase](m4/board-showcase.png) | **Displays.** As in M3, the LED colours now from their parameters. |
| ![Design, Blue Pill](m4/design-bluepill.png) | **Design, Blue Pill.** |
| ![Decoded serial](m4/decode-bluepill.png) | **Decoding.** |
| ![Debug, Blue Pill](m4/debug-bluepill.png) | **Debug, Blue Pill.** |
| ![Dark theme](m4/design-bluepill-dark.png) | **Dark theme.** |

## M3 catalog and SDK (2026-10-16)

The catalog grows to 101 components, each validated by the engine against
its fixtures; parts can be written in Lua; boards and circuits come from
EmbLinkRTOS board files, DeviceTree and KiCad netlists.

| | |
|---|---|
| ![The display showcase's board view](m3/board-showcase.png) | **Displays and actuators.** `examples/display-showcase` in the board view: a 16x2 LCD and an SSD1306 OLED written over their buses by pattern generators, a MAX7219 module over SPI, a 74HC161 counting into a CD4511 and a 7-segment digit and an RGB LED, a CD4017 chasing a bar graph, a servo at 45 degrees and a 555 sounding a buzzer. |
| ![The showcase running](m3/run-showcase.png) | **The showcase running.** The parts' outputs as meters and traces: the counter's count, the servo's angle, the buzzer's tone near 1.9 kHz, the bar graph's lit segment. |
| ![The showcase's schematic](m3/design-showcase.png) | **Design, showcase.** Twenty-two catalog parts on one sheet, connected by net labels. |
| ![A KiCad netlist imported](m3/design-flasher.png) | **Imported from KiCad.** `examples/kicad-flasher`, drawn by `tools/importers/kicad2project.py` from a KiCad netlist: a 9 V battery, a 7805, a 555 astable with its timing taken from R1, R2 and C1, and the LED it flashes at 6.9 Hz. |
| ![Design, Blue Pill](m3/design-bluepill.png) | **Design, Blue Pill.** The library's 101 components by category. |
| ![Decoded serial](m3/decode-bluepill.png) | **Decoding.** As in M2. |
| ![Inside the Blue Pill](m3/inside-bluepill.png) | **Hierarchy.** As in M2. |
| ![A bus counting](m3/design-counter.png) | **Buses.** As in M2. |
| ![The counter on the logic analyzer](m3/run-counter.png) | **The counter running.** |
| ![Run, Blue Pill](m3/run-bluepill.png) | **Run, Blue Pill.** |
| ![Dark theme](m3/design-bluepill-dark.png) | **Dark theme.** |
| ![Debug](m3/debug-bluepill.png) | **Debug.** |
| ![Design, transistor circuit](m3/design-transistor.png) | **Design, transistor circuit.** |
| ![Run, transistor circuit](m3/run-transistor.png) | **Run, transistor circuit.** |

## M2 workbench (2026-10-16)

The workbench edits the circuit against the engine, draws boards, decodes
serial lines and runs as a desktop application.

| | |
|---|---|
| ![Desktop application](m2/desktop-linux.png) | **Desktop.** The EmbSIM application on Linux (WebKitGTK): the Tauri shell starts the engine as its sidecar with the Blue Pill example. |
| ![Design, Blue Pill](m2/design-bluepill.png) | **Design.** The Blue Pill drawn as a block of its headers, the circuit wired to the pins it uses, power by net labels. |
| ![Decoded serial](m2/decode-bluepill.png) | **Decoding.** The firmware's boot banner on USART1, decoded frame by frame under the TX trace; the board's artwork with its power and PC13 LEDs lit. |
| ![Inside the Blue Pill](m2/inside-bluepill.png) | **Hierarchy.** Double-clicking the Blue Pill opens its inside: the MCU, regulator, LEDs, crystals, buttons and headers, every pin labelled with its net and live while the firmware runs. |
| ![A bus counting](m2/design-counter.png) | **Buses.** A 4-bit ripple counter; its outputs drawn as the bus `Q`, which shows the count while it runs (`Q = 0xA`). |
| ![The counter on the logic analyzer](m2/run-counter.png) | **The counter running.** Each output halves the one before on the logic analyzer. |
| ![Run, Blue Pill](m2/run-bluepill.png) | **Run.** The board view with the board's artwork above the circuit's parts, meters with units. |
| ![Dark theme](m2/design-bluepill-dark.png) | **Dark theme.** |
| ![Debug](m2/debug-bluepill.png) | **Debug.** As in M1. |
| ![Design, transistor circuit](m2/design-transistor.png) | **Design, transistor circuit.** |
| ![Run, transistor circuit](m2/run-transistor.png) | **Run, transistor circuit.** |

## M1 firmware (2026-10-16)

A Blue Pill runs its firmware in the circuit, and the workbench debugs it.

| | |
|---|---|
| ![Debug perspective](m1/debug-bluepill.png) | **Debug.** A breakpoint on `EXTI0_IRQHandler` hit by pressing the button, stepped once: the source line (`main.c:65`), the disassembly, the registers with the changed one highlighted, memory and the peripherals. |
| ![Run perspective, Blue Pill](m1/run-bluepill.png) | **Run, Blue Pill.** The board's controls (reset, boot pins), the button held, the regulated 3.3 V rail on the oscilloscope, meters on the board's LEDs. |
| ![Design perspective, Blue Pill](m1/design-bluepill.png) | **Design, Blue Pill.** The acceptance circuit's parts and nets with their live voltages. The board itself is not yet drawn on the schematic and nets without drawn wires show as dashed connections: M2 work. |
| ![Design perspective, dark](m1/design-bluepill-dark.png) | **Dark theme.** The same view under the system's dark preference. |
| ![Design, transistor circuit](m1/design-transistor.png) | **Design, transistor circuit** (the M0 example, for comparison). |
| ![Run, transistor circuit](m1/run-transistor.png) | **Run, transistor circuit.** |

## M0 foundation

The engine runs circuits; the workbench shows them.

| | |
|---|---|
| ![Design perspective](m0/design-transistor.png) | **Design.** The button, transistor and LED circuit on the schematic, its library, inspector and net voltages. |
| ![Run perspective](m0/run-transistor.png) | **Run.** The 1 kHz pulse switching the transistor on the oscilloscope, the LED current and the transistor in saturation on the meters. |

## Wired instrument loading and electrical feedback

Captured at 1800 × 1200 against the native engine on 2026-10-26 with
`tests/browser/test_instrument_loading.py`, using the instrument-loading and
electrical-feedback examples. The readouts and warnings come from native circuit
observations; these generic equivalents do not qualify commercial instrument
accuracy, protection, thermal/damage behavior or earth coupling.

![Loaded instrument readings and focused ammeter leads](instrument-loading/wired-instruments.png)

The ammeter's 10 Ω insertion resistance changes a 5 V / 100 Ω circuit from
50 mA to 45.45 mA, with 454.5 mV burden. The fitted scope capture compares the
loaded capacitive input with a resistive input.

![Selected meter net and highlighted terminals](instrument-loading/selected-meter-net.png)

![Opposing digital outputs with a real solved bus voltage](instrument-loading/conflicting-drivers.png)

![Floating input located from the warning list](instrument-loading/floating-input.png)

## Exported SVG (2026-10-28)

`embsim export svg` writes the schematic sheet as the workbench lays it out,
and what a display shows at a moment, for a README or an issue (issue #94).
The Uno example's sheet, and the display showcase's SSD1306 at 300 ms:

![The Uno example's schematic, exported](export-svg/uno-blink-serial.svg)

![The SSD1306 of the display showcase at 300 ms, exported](export-svg/oled.svg)
