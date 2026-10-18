# 01 - Vision and Principles

**Status:** ACCEPTED baseline for 1.0. Changing the mission or a principle is a
new ADR.

---

## 1. Mission

EmbSIM is a virtual electronics workbench: it runs the real firmware of a
microcontroller on a model of its board, inside a circuit the user builds from
real electronic components, with the time, voltages and logic levels the
hardware would have, and lets the user touch it while it runs. An embedded or
circuit engineer should be able to design, bring up, debug and verify a product
on EmbSIM before the first board exists, and keep using it after, because the
same experiment runs headless in continuous integration.

One engine serves every interface: a graphical workbench, a command line, test
harnesses and scripts all drive the same session through the same protocol and
observe the same results.

Where it is going (the owner's direction, 2026-10-17): a real-world simulator
for embedded electronics. Every supported board complete, with the devices
on it; as many real components and sensors as possible, each behaving as its
datasheet says; a schematic among the best there are; a 3D breadboard where
the parts sit and are wired as on a bench; and a world around the circuit
(ambient temperature, humidity, pressure, light, wind, objects that move)
that its sensors measure as they would in a room. It serves students and
professionals alike, and the interface runs at the display's rate. The order
of the work is the flagship first (§9, 07 §2): one board family and its most
used parts done completely, then the world and the breadboard, then breadth.

## 2. Who it is for

| User | What they do with EmbSIM | What they need from it |
|---|---|---|
| Firmware engineer | Writes firmware for a board they may not have yet; debugs it at source level while pressing buttons, turning pots, sending bytes | Exact register behaviour, interrupts, timers, a debugger, serial consoles, a board that looks and acts like the real one |
| Circuit engineer | Designs the circuit around the MCU: drivers, sensors, power, level shifting | Real component models with datasheet parameters, voltages and currents, waveforms, operating points, warnings for violations |
| Educator and learner | Teaches and learns embedded systems and electronics without a lab | A beautiful, forgiving, explorable interface; a catalog that explains itself; examples |
| Verification and CI | Reproduces a scenario on every commit | Determinism, replay, assertions, a CLI with machine-readable output, speed |
| Component and board author | Adds a part, a sensor, a development board | Declarative descriptions, a model SDK, conformance tests, documented fidelity |
| Tool integrator (EmbLinkRTOS, EmbCC, IDEs) | Drives the simulator from another tool | A stable protocol and a C API, shared hardware descriptions |

## 3. What "useful" means here

A simulator is useful when an engineer can trust what it shows and act on it.
That requires:

1. **Real firmware.** The image the toolchain links (ELF, Intel HEX) runs
   unmodified: no stubs, no rewrites, no special build. EmbCC, GCC, Clang and
   Microchip XC outputs are first-class inputs.
2. **Real register behaviour.** Peripherals are modelled from the vendor's
   register description and documented behaviour, not from a convenient API.
   An access to something unmodelled is reported, never silently absorbed.
3. **Real electronics.** A resistor has a resistance, an LED has a forward
   voltage, a transistor has an operating point, a GPIO has an output impedance
   and input thresholds. Circuits connected to the board interact with the
   firmware the way physics dictates, within declared fidelity.
4. **Real interaction.** Buttons, switches, pots, sensors, serial lines, probes
   and wires are manipulated while the firmware runs, and every such action is
   timestamped in virtual time so the run can be replayed.
5. **Real workflow.** Build, flash, run, break, inspect, edit, rerun, assert:
   the loop an engineer runs on a bench, faster and reproducible.

## 4. What "modern" means here

- **One codebase, three platforms.** Linux, macOS and Windows get the same
  workbench, the same CLI, the same results, from one source tree and one CI.
- **Designed before built.** Architecture, specifications, requirements and
  decision records come first; the implementation follows them and STATUS.md
  says how far it got.
- **Data over code.** Components, MCUs, packages, boards and projects are
  validated descriptions. Adding a board is writing a file, not C.
- **Honest fidelity.** Every model declares what it models, to what accuracy,
  from which sources, with which validation. "Supported" is earned by evidence.
- **Deterministic by construction.** Virtual time is integer picoseconds; the
  wall clock never enters a model; every external input is an event with a
  timestamp; two runs of one recording are identical.
- **Beautiful and accessible.** The workbench is a product, not a debug page:
  a coherent design system, light and dark themes, keyboard operation, high
  DPI, reduced motion, screen-reader labels, responsive layouts.
- **Open and extensible.** Apache-2.0; a published component SDK; libraries of
  parts and boards anyone can author, share and validate.

## 5. Principles

- **P1 One engine, every interface.** The GUI contains no simulation; the CLI
  contains no GUI. Both are clients of one session protocol over one core.
- **P2 Declared fidelity.** A model without a fidelity declaration cannot be
  loaded. Unsupported behaviour is observable; strict mode stops on it.
- **P3 Physics owns the net.** Connectivity is a graph of nets and pins with
  electrical pin models. A board's LED is a component on a net, not a callback.
- **P4 Time is integer and global.** One scheduler, one picosecond time base,
  rational clocks, documented ordering of simultaneous events.
- **P5 Descriptions are data, validated by schemas.** Everything a user can add
  (component, MCU, package, board, project, scenario, library) is a JSON file
  with a published schema and a validator.
- **P6 Reuse with provenance.** Code brought in from EmbCC keeps its notices and
  commit history references; behaviour is held to the fixtures that came with
  it.
- **P7 Refuse rather than guess.** A description that cannot be validated, an
  image for another part, a circuit that cannot converge: a named error, never
  a plausible result.
- **P8 Evidence over claims.** STATUS.md and CI runs are the only support
  matrix. A listed register is not a supported peripheral.
- **P9 Toolchain independence.** The simulator never needs a compiler to run.
  Toolchain profiles make build-and-run convenient; the ELF/HEX is the contract.
- **P10 Small core, big catalog.** The C core is dependency-free and stable;
  breadth lives in descriptions, models and libraries.
- **P11 Real from the outside (ADR-030).** Everything a person, a firmware
  image, a tool or another part can observe behaves as it does in the real
  world: voltages, currents, timing, the steps a person takes, the tools
  they use and what those tools show. How the engine computes it may differ
  (USB as packets, not edges on D+ and D-), provided the outside matches and
  the difference is written down. Every piece of work starts by asking how
  the thing works in real life and how to implement it fully; nothing is
  added that has no counterpart in reality.

## 6. Non-goals

- Replacing SPICE for RF, EMC, PCB parasitics, thermal or process variation
  analysis. EmbSIM's analog engine targets the circuits around an MCU. The
  world's temperature, humidity and motion (ADR-026) are what sensors
  measure, not a thermal or mechanical analysis of the board.
- Cycle-exact pipeline and cache modelling of every core. Instruction cycle
  estimates are documented as estimates per core (ADR-015).
- A PCB layout tool. Board artwork and the breadboard view (ADR-027) are for
  building, interaction and understanding, not manufacturing.
- Running untrusted native plugins safely. Native plugins are trusted code
  loaded explicitly; sandboxed models use the scripting tier (ADR-009).
- Hard real-time guarantees. A paced mode keeps the simulation in step with
  the wall clock so real tools can talk to a virtual board (ADR-032), and
  says plainly when a circuit is too large to keep up; speed claims come
  from benchmarks.

## 7. Positioning

| | EmbSIM | Wokwi | Renode | QEMU | SPICE tools | Proteus |
|---|---|---|---|---|---|---|
| Runs unmodified firmware | yes | yes | yes | yes | no | yes |
| Electrical circuit around the MCU | yes (own MNA engine) | logic-level only | no | no | yes | yes |
| Interactive board while running | yes | yes | limited | no | no | yes |
| Deterministic replay | yes | partial | yes | partial | n/a | no |
| Headless CI, CLI | yes | limited | yes | yes | yes | no |
| Open source, offline, cross-platform | yes | hosted | yes | yes | mixed | proprietary |
| Declarative components and boards | yes | yes | yes (C#/Python) | no | netlists | proprietary |
| Source-level debugging | yes (GDB RSP, DWARF) | yes | yes | yes | no | yes |
| Breadboard view, sensors in a simulated world | planned (ADR-026, ADR-027) | breadboard 2D, sensor sliders | no | no | no | 2D, sliders |

EmbSIM's distinct claim is the combination: real firmware, real electronics
and real interaction in one deterministic, open, cross-platform tool with a
first-class command line.

## 8. Definition of success for 1.0

1. The acceptance scenario of SPEC-007 runs on Linux, macOS and Windows: a
   Blue Pill (STM32F103C8) runs an unmodified GCC or EmbCC ELF that reads a
   push button through a pull-up and drives an LED through a resistor and an
   NPN transistor; the user presses the button in the workbench; the firmware
   sees it through GPIO registers and an EXTI interrupt; the LED current and
   the transistor's operating point are visible on the oscilloscope; the same
   scenario runs from the CLI with assertions and reproduces the same
   observations from a recording.
2. At least five boards across three core families (Cortex-M, RISC-V, AVR) and
   one Microchip family (PIC32 or PIC24) run their vendor examples, with the
   evidence listed in STATUS.md.
3. The catalog holds at least one hundred components with fidelity
   declarations and conformance fixtures, and an independent contributor has
   added a component and a board following the SDK alone.
4. The workbench passes the interface checklist of SPEC-011 on all three
   platforms: keyboard operation, assistive labels, light and dark themes,
   high DPI, 60 Hz interaction on a 500-component schematic.
5. CI runs the conformance, golden, differential (against ngspice fixtures and
   EmbCC/QEMU fixtures) and platform suites on every change.

## 9. The flagship and what follows it

**Status:** ACCEPTED direction (owner, 2026-10-17); 07 §2 schedules it.

Breadth without depth is the risk of this vision: a thousand shallow models
convince nobody. So the first goal after the boards and engines of M0 to M5
is one complete experience, judged as a user would judge it:

1. **The flagship.** The Arduino Uno and an ESP32-class board, complete
   down to every peripheral their usual libraries touch, and the thirty or
   so parts people wire to them most (temperature and humidity sensors,
   barometers, IMUs, distance sensors, displays, LED strips, servos,
   steppers, keypads, RTCs, RFID, GPS), each verified against its datasheet
   and, where possible, a capture from the real part; their libraries'
   example sketches run unmodified.
2. **The world.** An environment the circuit lives in (ADR-026): ambient
   values and moving objects that sensors read, set from a panel first and
   from the 3D view later, recorded and replayed like every input.
3. **The breadboard.** A 3D bench (ADR-027) where the same circuit is built
   on breadboards with jumper wires, every wire an edit of the same nets the
   schematic shows.
4. **Then breadth.** Every supported board completed peripheral by
   peripheral against a coverage table, more boards, and libraries of parts
   from the community through the SDK.

Throughout, the interface keeps to its frame budgets (ADR-028), and outside
projects are reused only under licences that fit Apache-2.0 (ADR-029).


## 10. A real bench, not a game

**Status:** ACCEPTED direction (owner, 2026-10-18); 07 §2 schedules it.

The owner's discussion of 2026-10-18 set the bar for everything after the
flagship: the app should feel and work like an engineer's bench, help
people understand how things really work, and take the years that needs
rather than imitate reality. In practice (P11):

1. **USB as it is (ADR-031).** A cable plugged into a virtual computer
   powers the board within the port's limits (the board's fuse and
   regulators, the 5V and 3V3 pins feeding the circuit), carries the
   USB-serial bridge's port to the host as a real serial port, and
   programs the board the way the real one is programmed: the real
   bootloader with avrdude on the Uno, UF2 onto the Pico in BOOTSEL mode,
   the on-board ST-Link or PICkit. Later, MCUs with their own USB appear on
   the host as real USB devices.
2. **Real time and real tools (ADR-032).** A paced mode in step with the
   wall clock; a terminal where the host's own tools (avrdude, make,
   picotool, a Python script) work against the virtual board; IDE
   integration (VS Code, PlatformIO, the Arduino IDE).
3. **Instruments used as the real ones (ADR-033).** A multimeter whose
   current range goes in series and whose fuse blows, oscilloscope probes
   with 1x/10x, ground lead and bandwidth, a bench supply with its current
   limit, a function generator, a logic analyzer with clips, a USB power
   meter.
4. **Imperfections and damage (ADR-034).** Contact bounce, tolerances and
   drift, noise, internal resistance, missing decoupling, bad breadboard
   contacts; absolute maximum ratings that, exceeded, damage a part as they
   would; a fault mode that cuts, shorts or kills on purpose.
5. **Understanding.** An explain mode (a wire's current and why, a
   register changing as a line runs), guided lessons, teachers'
   assignments checked by scenarios, diagnostics that say the real-world
   consequence.
6. **Reality checks.** Recordings from real boards (logic-analyzer
   captures, scope traces, serial logs) compared with the simulation
   automatically.
7. **More of the real world.**
   - Boards people own: Arduino Nano, Mega and Leonardo, STM32 Nucleo,
     ESP32 and ESP32-S3, Pico W, BBC micro:bit.
   - Languages and systems: MicroPython and CircuitPython with their REPL,
     Rust (Embassy), TinyGo; RTOS awareness (FreeRTOS, Zephyr,
     EmbLinkRTOS).
   - Parts you see, hear and feel: buzzers and speakers through the host's
     audio, a microphone from the host's; displays pixel for pixel; motors
     with load and back-EMF; converters, chargers and solar panels; the
     host's keyboard, gamepad or phone as inputs.
   - Buses and radios: CAN, RS-485 and Modbus, 1-Wire, LIN, Ethernet, IR,
     GPS; LoRa and nRF24 between boards, weakened by distance and walls in
     the world; WiFi and Bluetooth on the ESP32 to the real internet.
   - Power and battery life from each MCU mode's datasheet current; a robot
     on the floor of the 3D world; firmware CI as a GitHub Action; a
     classroom mode.
8. **AI and automation (ADR-035).** The session protocol exposed to AI
   assistants and scripts (an MCP server): build a circuit, flash firmware,
   run, read probes, with the same evidence as a person.

Further out: mixing real and virtual hardware, export to KiCad for PCB
work, FPGAs through Yosys and Verilator, Linux boards through QEMU, shared
projects and a community library held to the same evidence, translations.
