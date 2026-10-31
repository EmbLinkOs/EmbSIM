# Project status and evidence

**Reviewed:** 2026-10-16 at the M5 implementation (verification: recordings and replay, coverage and profiling, the Verify perspective, the Python client, EmbSIM as EmbLinkRTOS's emulated target) on branch `claude/affectionate-allen-40uv0o`. This inventory is
maintained by hand until evidence tooling exists. It is not a generated
support matrix. Specifications define target contracts; their acceptance
does not mean the described behaviour exists.

## The STM32F446 completed further, and the NUCLEO-F446ZE (2026-10-31)

The F446's model gains what firmware sees of its clocks and flash: each
oscillator and PLL is ready DS10693's typical start-up time after it is
enabled (LSE's 2 s, a crystal's 2 ms, a PLL's 75 to 100 us), CIR's ready
interrupts, the flash's program and erase times with BSY and the bus stall
of code running from it, the CRC unit, and TIMx_OR's remaps (TIM5 measuring
LSI). Evidence: `test_stm32f4`'s `test_startup_times`, `test_crc`,
`test_tim5_lsi` and its flash test (BSY's 16 us and a sector's 250 ms with
no instruction run meanwhile); CubeF4's NUCLEO-F446RE suite, its
EEPROM_Emulation now ending past 13 s.

The NUCLEO-F446ZE (MB1137, STM32F446ZE in LQFP144) is a board: its LEDs,
buttons, power paths and jumpers, the Zio and morpho connectors (every I/O
on the morpho once), CN13's lines and the VCP on USART3. Evidence: its six
fixtures, and 21 of CubeF4's STM32F446ZE-Nucleo
examples run unmodified under scenarios (`run.py --board nucleo_f446ze`).
The board follows ST's MB1137 B-01 schematic part by part (its LED,
button and USB circuits, its solder bridges), and the NUCLEO-F446RE was
checked against MB1136 C-03 and C-04 (B1's network, CN7's SB48/SB49). Not
yet: the USB OTG FS controller (CubeF4's USB device and host applications)
and U12, the switch that powers VBUS for a host; MCO1/MCO2 on their pins;
the ST890, TSV631 and S9013 datasheets' own values (their parts declare
what is assumed).

## Boards as boards (2026-10-30)

Stage 4 of [the 3D plan](engineering/bench-3d.md) has begun. Every board's
PCB is built from what its artwork says:
- the outline is the `pcb` path or the `#pcb` rounded rectangle: the Uno's
  chamfers and step, the DevKit's rounded corners;
- the soldermask colour is the `.pcb` rule's fill, used for the underside;
- the mounting holes are the `.mount` circles: the Uno's four, drilled
  through with plated barrels and rings.

The cut edge shows the glass-epoxy core, and the top is the artwork under a
clearcoated mask. Evidence: `board-artwork.test.ts` (the Uno's outline, mask
and 3.2 mm holes read from its real artwork; the body's extents and
barrels) and the gallery. Not yet: copper traces under the mask (the
artworks are pictures, not layers), and holes on boards whose artwork does
not mark them.

## Packages at their standard dimensions (2026-10-30)

Stage 2 of [the 3D plan](engineering/bench-3d.md) is under way. A package
module (`ui/packages/bench/src/packages3d.ts`) draws parts from their
standards' dimension tables, as KiCad's generator does:
- DIPs per JEDEC MS-001/MS-011, the 28-pin per Microchip's 28P3: drafted
  bodies, pin-1 notch and dimple, stamped leads whose tips land on the pins;
- DIP sockets;
- 2.54 mm socket strips with their openings and contacts, and pin strips;
- the USB-B (USB 2.0 mouth and tongue), micro-USB and USB-C receptacles;
- a 2.1 mm barrel jack;
- the 6 mm tactile switch;
- HC-49/US crystals;
- QFP and QFN bodies sized by pin count (MS-026, MO-220);
- radial electrolytics with the stripe over the negative lead;
- an RF module (the ESP32-C3-MINI-1's shield can and antenna area), a new
  board-feature kind.

Evidence: `packages3d.test.ts` (16 tests: the standards' nominal
dimensions, every DIP lead's tip on its pin, outlines, which way mouths face,
the stripe's side) and the gallery's close-ups. Not yet: SOIC/TSSOP and chip
passives on modules, gull-wing bends on QFP leads (they lie flat), and
models from files (stage 3).

## The 3D bench as an inspection viewer (2026-10-30)

The owner asked for the 3D bench and world to reach Proteus's, KiCad's and
Altium's level. [The plan](engineering/bench-3d.md) compares the bench with
their 3D views and sets seven stages: inspection, JEDEC package geometry,
glTF model assets, the board's stack-up, CAD interaction, the world, and
performance. Stage 1 is built (`ui/packages/bench`, the bench's toolbar):
- standard views on KiCad's keys, and an orthographic camera;
- a measure tool, snapping to pins and holes;
- a section plane on any axis, and a height plane that lists the parts
  through it (the Proteus height check);
- layer visibility;
- a quality render, with ambient occlusion and bloom only on what emits
  light, falling back to draft when frames miss 60 Hz;
- a PNG still at 2× or 4×, and GLB export (glTF binary in metres, a node per
  part).

Evidence: `inspect.test.ts` (12 tests: view framing, measurement, section,
height plane) and `bench-tools.spec.ts`. The spec measures R2's legs as
10.16 mm (4 × 2.54 mm) through the pointer, checks that the GLB file starts
with `glTF`, and checks that no tool edits the project. Not yet:
- real package geometry, model files and the board's copper, mask and
  silkscreen (stages 2 to 4), which is where the bench still looks least
  like a professional tool;
- capped section faces;
- STEP export (no permissively licensed B-rep kernel);
- frame times measured against ADR-028.

## I2C parts' captures (2026-10-30)

`replay_captures.py` now replays I2C captures: the master's half of SCL
and SDA is played back at the captured times, SDA is released in the slots
the part drives, and every bit it drove (each ACK or its absence, each bit
read from it) is compared one for one. Another part on the bus is played
back as captured. 19 sigrok captures of a real Microchip 24AA025UID (public
domain, `i2c/eeprom_24xx/microchip_24aa025uid`) match through the new
`emblink/eeprom_24aa025uid`, about 20,000 bits in all:
- byte writes 1 to 6 ms apart, where the part does not answer during its
  write cycle (between 3.10 and 4.03 ms on this part; the model takes
  3.5 ms, the datasheet gives a 5 ms maximum);
- page writes of 8 to 48 bytes, wrapping at 16;
- reads of the whole array, ending in the protected half's codes and serial
  number.

With a 5 ms or 3 ms write cycle, or the 24C02's 8-byte pages, the same
captures fail, as they should. Two captures of a DS3231 module (`i2c/rtc_dallas_ds3231`) match through
`emblink/ds3231` with its new `start`: the ACKs, the time and temperature
reads. Three reads show registers set before the capture, so only their
ACKs are compared. The DS1307's captures are sampled too coarsely (200 and
500 kHz) to replay. Not yet: SPI parts (MAX7219 and others) and the
remaining I2C sensors in sigrok-dumps (BH1750, SHT3x, ...).

## Real parts' captures (2026-10-29)

The first evidence from real hardware beyond datasheets: 15 sigrok captures
of a real AM2302 (public domain, sigrok-dumps `am230x/am2302` at 0ad13477;
about 230 reads, starts from 100 us to 20 ms, reads 71 ms to 2.3 s apart) are
replayed into `emblink/dht22` by `tools/verify/replay_captures.py`
(`real_captures` test). The host's starts are played back at the captured
times; the model's answers are decoded and compared with the real part's:
answered or not for every read from the real part's first answer, each
timed phase's mean within 2 us (6 us for the response's end, which a host
that drives the line after its start masks), the checksums. All 15 match.
The comparison found the model's datasheet timing 3-14 us off the real
part's, a byte-boundary delay the datasheet does not show, a start threshold
of 800 us where the part answers from about 400 us, and no busy time where
the part ignores reads for about 0.4 s after one; all are now modelled as
measured. Four sibling parts' captures (AM2303, AM2320, AM2321, AM2322) agree
with the AM2302's times within 2 us; they are not part of the suite. A
capture of two real DS18B20s on one bus (sigrok-dumps `onewire/ds18b20`) is
replayed the same way through `emblink/ds18b20` twice over: the four ROM
searches, 512 bits the parts sent, match bit for bit (the two ROM codes and
their CRCs among them); the presence (27.4 us after the reset, 120.1 us) and
a read 0 (28.4 us) now match within 0.4 us where the model had the
datasheet's 30 us. The capture's scratchpad reads all come before a
conversion could finish, so they show what the parts held before it and are
not compared. Not yet: captures of other parts (sigrok-dumps holds DS3231,
MAX7219, BH1750, SHT3x and more, each needing its protocol's decoder in the
harness)
and of the boards themselves (a capture kit for an Uno and a NUCLEO is
described in `tests/captures/README.md`).

## Diagnosis, documentation and planning commands (2026-10-28)

Issues #86-#102, from the same project. Evidence: `test_diagnose` (a stale
image warned and a fresh one not; `--dump-regs` decoding USART2; the circuit
profile; `perf.island_size` for a 4.7 kOhm pull-up from the Uno's UART TX to
5 V, and not for the PWM line that drives an LED; `export svg` of a sheet and
of an SSD1306; `plan` on the NUCLEO-F446RE checked against the F446's
alternate-function table, no pin twice, clear of LD2 and SWD, and refused on
the ATmega328P, which has no such table); `test_json`'s `test_match` and
`test_session`'s diagnostics rules; the MFRC522's `card-swap` fixture; and
`tests/vendor/freertos`, which builds the unmodified FreeRTOS kernel V11.1.0
and checks every task's state, wait (delay, notification index 1 for ever, the
`rxq` queue), pending notification and stack high-water mark, with scenario
`rtos` assertions sampled each millisecond. `mcu.irq_storm` was checked by hand
(INT0 held low on the Uno: "INT0 (exception 1) entered 401 times in 787 us").
Not done: a regulated rail as a boundary between analog islands (#86 warns
instead), SMP and 16-bit FreeRTOS ports, live symbol states in exported SVG,
pin plans for MCUs without an alternate-function table.

## Diagnostics found at load, probes and images (2026-10-28)

Issues #80-#85, from a FreeRTOS access-control firmware on the NUCLEO-F446RE.
The STM32 I2C model keeps the SR1-then-SR2 ADDR sequence across a CR1 write
(the reference manual's single-byte receive clears ACK in between) and clears
BTF and TXE when a START completes in transmission; `test_stm32f4`'s
`test_i2c_master` now runs the one-byte receive and a repeated START after BTF
(14 failures on the old model, none now). Diagnostics carry the phase they were
raised in and a run's reset keeps the load's, so `--json` reports, scenario
reports and `diagnostics.forbid` see the netlist checks (`test_session`'s
`test_load_diagnostics`). `embsim run` stops at load when a firmware image
cannot be read (`--allow-no-image` to run without it); a probe with an unknown
kind or target fails the load with a suggestion; `embsim validate` reports the
netlist checks and the circuit settled at its first instant. Not yet: checking
that an `output:` or `device:` probe's quantity exists on its part.
## Live schematic milestone (2026-10-26)

Branch `feat/live-schematic-workbench` makes the schematic the primary Run surface
and adds native symbol bindings, on-sheet controls, accepted-time electrical LED
exposure and ideal differential voltage leads. Generic bicolor and 8×8 matrices
reuse independent junctions. Existing LCD/OLED, serial displays, segments and
bargraphs now show model outputs on their symbols. Floating-common functional
segments/bargraphs stay dark. Attached repeated labels complete drawing traces.

Local checks pass 49 native CTest targets, 274 UI unit tests, all 31 browser
workflows, strict validation of 899 JSON descriptions/projects/fixtures and the
native live-schematic browser acceptance. The live circuit passes ten assertions;
the fully routed firmware RGB example retains all 39 assertions. Analog tests
include 1 kHz independent 25/50/75% RGB exposure, repeated reads and reset.
Actual native captures are in the [gallery](gallery/README.md).

See [implementation, evidence and remaining release gates](engineering/live-schematic.md)
and the [live acceptance circuit](../examples/live-schematic/README.md).
This is not completion of all surveyed capabilities, full device qualification,
a Proteus-parity claim or a 1.0 release. Editable project hierarchy, triggered
capture, advanced analyses and managed engineering workflows still require the
explicit gates in that document. No new 3D development is included.

## Wired instrument loading and electrical feedback (2026-10-26)

Branch `feat/loaded-instruments` adds generic configurable wired voltmeter,
ammeter, scope-input and function-generator equivalents. Their finite input,
shunt and output impedances enter the native solve; scope capacitance reuses the
adaptive integrator. The loading bench checks 13 independent analytic targets;
twelve component fixtures exercise loading, polarity and transient behavior.
Local verification passes 49 native CTest targets, 12 instrument fixtures /
37 assertions, and the 13-assertion loading and 3-assertion deliberate-fault
scenarios. Native tests check zero/one/two-lead construction without implicit grounding,
current conservation, reset, refined RC convergence, digital
conflict clearing and loaded opposing drivers. Protocol checks verify current
contention and receiver identities for digital and mixed nets.

The Run panel shows impedance, lead nets and actual readings with locate/trace
actions. Selected nets expose every terminal, junction and label. Pin names and
live states remain horizontal through rotation. Live input warnings distinguish
Z, X and opposing known drives, and locate their nets; unwired declared digital
inputs without declared pulls also produce structural warnings. A zero analog
voltage is not treated as proof of floating.

See the [instrument contract and limits](engineering/instrument-loading.md) and
[interactive loading bench](../examples/instrument-loading/README.md). These are
linear equivalent circuits, not commercial instrument accuracy, bandwidth,
protection, noise, thermal/damage or earth/chassis qualification. Ideal observation
probes remain available alongside the explicitly wired loads.

## Firmware-driven RGB and waveform measurements (2026-10-26)

Branch `feat/indicator-pwm-evidence` extends the named indicators with an
ESP32-C3-DevKitM-1 circuit booted from a normal flash image through the ROM.
Firmware programs LEDC and polls a physical MODE button; 39 assertions check
1 kHz at 25/50/75% duty, off/full-on/return transitions, mean/RMS currents and
finite GPIO voltage drop against independent source-derived bounds. Scalar MCU
resistance/slew settings now initialize base GPIO models; the example uses the
declared 20 Ω driver instead of the generic 25 Ω fallback. This linear driver
is representative and does not qualify MCU drive-strength or process corners.

CLI and workbench share time-weighted sample-and-hold mean/RMS/digital duty
semantics, rejecting missing coverage and unknown/nonfinite intervals. Reports
retain measurement/window identity and units. The scenario editor exposes these
checks; scope/logic strips show RMS, duty and coverage. Native probe metadata
preserves declared units/sample rates, and the scope separates voltage/current
groups on its single vertical axis. Same-time waveform events retain their
original timestamps.

Local verification passes all 48 native CTest targets, 249 UI unit tests and all
30 browser workflows. The CLI measurement contract checks 22 valid/invalid
assertions. Repeating the firmware circuit at doubled capture rates passes its
39 assertions and agrees within 1 nA for measured mean/RMS currents and 0.001 of
duty fraction. These are numerical circuit checks, not real-device accuracy.
Native streaming checks also cover bounded 30,001-sample delivery, eviction of
unsent versus already delivered history, constant-signal watermarks and restart.
The workbench does not extrapolate a partial delivered chunk to the engine clock.
The selected advance interval is shared by the toolbar, F7 and command palette.
[PWM evidence and limits](engineering/indicator-pwm.md) and the
[rebuildable example](../examples/esp32c3-qualified-rgb/README.md) explain the
source bounds, sampling, raw-image analysis limits and unsupported effects.
Thermal/charge/optical behavior remains open; configurable physical instrument
loading follows in the milestone above;
the 3D emitter still shows instantaneous observations and can alias PWM.

## First named indicator evidence batch (2026-10-26)

Branch `feat/qualified-indicators` builds on the circuit-construction milestone.
It adds WP7113ID and WP154A4SUREQBFZGC with source-backed pin maps and body
envelopes and a checked **25 °C, 2–20 mA typical DC curve** envelope. The latter
uses independent RGB die parameters and the manufacturer's R/COM/B/G order at
1.27 mm pitch. The library and inspector expose the evidence scope. This is not
full manufacturer corner, optical, thermal, damage, transient or mechanical
qualification. Numeric LED body dimensions replace the generic dome envelope for
these exact entries; source lead forming is not automatically manufactured.

Native indicator terminal mapping now preserves open leads as floating private
nodes. Optional channel overrides inherit generic global parameters, and mixed
zero/nonzero series resistance allocates only active internal rows. Source-point
holdouts, loaded 3.3/5 V circuits and a physical-switch named RGB example exercise
those contracts. [Indicator source/evidence methodology](engineering/indicator-qualification.md)
records reproduction, fixtures and exclusions. Further named families precede
realistically loaded instruments.

Local verification passes all 45 native CTest targets, 73 LED/RGB fixtures with
354 assertions, and the named RGB example's 28 assertions. The UI passes 241 unit
tests and all 29 browser workflows, typecheck, lint and the production build.
Both exact source PDFs reproduce the stored plot samples. Native
[captures](gallery/README.md) show unequal red/blue currents and the evidence
panel without engine diagnostics or browser errors. These checks establish the
declared nominal DC envelope and integration, not the excluded device behavior.

## Circuit construction and live RGB indication (2026-10-25)

Branch `feat/circuit-construction` builds on the workspace milestone. It adds
existing-jumper selection, appearance and route editing, atomic reconnection,
shared net/strip tracing, and native load/commit plus Python validation of direct
wire invariants. Appearance-only transactions preserve the clock and probes.
RGB electrical behavior uses three native junctions with external current
limiting; functional mode requires a known common return. The 3D emitter consumes
engine color/luminance instead of an independent animation. The switch-driven
RGB acceptance circuit visits every mix and off, with 28 assertions; isolated
LED/RGB fixtures execute 143 assertions across 30 circuits. These are generic
model checks, not manufacturer qualification or an expanded IC support claim.

[Construction contract and evidence](engineering/circuit-construction.md) and
[bench instructions](user/bench.md) state the controls and limits. Local checks
pass all 45 native targets, 238 UI unit tests, 134 validator tests
and 819 strict schema/catalog files. Native RGB captures show engine-observed
magenta and off without browser errors. Subsequent work prioritizes qualified
named component families before loaded instruments.
Native pose-dependent component-leg import occupancy, an external ESP32-C3
firmware/PWM RGB circuit, optical calibration and full mechanical qualification
remain open.

## Responsive workspace and deferred bench loading (2026-10-25)

Branch `ui/workspace-layout`, based on main `896e4f0`, separates the project menu,
perspective navigation and utilities from the simulation controls. Long project
names truncate within their menu instead of intercepting navigation. The shell
fits compact windows; dock minima scale only when space is insufficient, retain
saved proportions/fold choices, and restore them as space returns. Split resizing
starts from the displayed pane edges. Status details remain keyboard-scrollable.
The schematic ribbon uses a more compact height and stays out of Bench view.

The pure bench model is separate from the optional 3D renderer. Schematic startup
does not request the renderer; loading retains keyboard selection and a later
choice of 2D. Asset failure retains 2D and offers a fresh-page reload, separately
from in-place context-loss retry. The initial JS entry changes from 1,550.42 to
909.17 kB (estimated gzip 466.36 to 300.17 kB); 640.97 kB is deferred until 3D
is requested. Frame-time and wall-clock startup qualification remain open.

[Workspace behavior and limits](engineering/workspace-layout.md),
[bundle evidence](engineering/bench-loading.md) and the [gallery](gallery/README.md)
record the scope. Local typecheck, lint, production build, 232 unit tests across
49 files, all 27 browser workflows, the affected follow-up workflows, 790
schema/catalog files and documentation integrity pass. A fresh GCC Debug CLI
build validates the Uno project and serves its deferred renderer without browser
page errors. Hosted platform checks are recorded on the milestone PR; these are
client viewing/layout changes, not new simulation models or hardware qualification.

## Named surface-mount package milestone

Branch `feat/named-package-bodies` adds reusable SOT-23 and SOT-223 procedural
bodies. BSS138 and LM1117IMP-3.3 use reviewed manufacturer outlines and their
canonical terminal identities. BAT54 uses a clearly marked reference family
outline; its manufacturer-source gap remains open. Surface-mount leads cannot
become breadboard insertions through top-down overlap. Close-up labels follow
body bounds. These are representative visual bodies, not PCB land patterns,
soldering support or electrical-model qualification. LM1117's rendered output
tab shares OUT2 and is not independently wireable.

The [package evidence](engineering/package-geometry.md) records sources,
dimensions and omissions; [native captures](gallery/README.md) inspect isolated
parts. Local typecheck, lint, production build, 221 UI tests across 47 files and
all 40 native CTest targets pass. Hosted checks and browser results are recorded
on the milestone PR.

## Physical assembly authoring milestone

Branch `feat/bench-assembly` builds on the inspection milestone. Every proposed
leg lists its hole, occupancy owner or unseated state; keyboard proposals draw
viewing-aid rings on the native bench. Workbench insertion and direct-hole jumper
authoring reserve occupied holes while allowing another hole on the same strip.
Rejected placement leaves the project untouched. Numeric bench placement is
independent from schematic coordinates. Breadboard moves and quarter turns carry
inserted parts by a shared rigid transform, preserving hole IDs, nets and undo.
Raised board headers no longer become inferred breadboard insertions from 2D overlap.
A completed asynchronous breadboard placement preserves a later user selection;
a browser regression dispatches both intents before the placement promise settles.

These are client authoring guards, not a native loader/mechanical qualification.
Partial insertions and abstract component-pin lead connections remain supported;
imported conflicts are not automatically certified or repaired. Jumper curves
update on commit rather than continuously following a dragged assembly. Per-leg,
occupied-hole, batch-swap, all-quarter-turn and mutation-atomicity fixtures cover
these boundaries. Native GCC Debug passes all 40 CTest targets after integrating
the world/sensor merge. UI/browser results are recorded on the milestone PR;
[native Uno previews](gallery/README.md) show rejected and free-strip insertion.

## Physical bench inspection and wiring milestone

Branch `feat/bench-inspection` develops the merged 3D bench rather than replacing
it. The bench targets projected contacts, names source/destination endpoints,
rejects ambiguous neighboring contacts, validates typed terminals, and avoids
duplicate/reversed jumpers. Pending wire/placement gestures cancel on Escape,
pointer cancellation/capture loss, focus/visibility loss and tool/edit/live-mode
changes; interrupted button holds send a release. Selection focus/top cameras,
paper/graphite/mat surfaces, label modes, notes and a foldable keyboard list make
inspection configurable. The list stacks below narrow scenes. Focus canvas exposes
the existing side-panel focus mode directly from the bench. Renderer failure or
context loss selects an explained 2D fallback with retry.

Board bodies, contact positions and renderer pin heights share thickness, standoff
and header-style data. Selected-part geometry labels identify catalog dimensions,
artwork anchors or approximate package geometry, without a qualification claim.
Generic lead anchors remain approximate, and picking ignores the owning body when
checking occlusion; it is not a mechanical accessibility/collision validator.
GLB/STEP import, all-catalog qualification, complete label occlusion and measured
ADR-028 rendering budgets remain outstanding.

Local UI validation passes typecheck, lint, production build, 212 tests across
46 files and all 17 Playwright workflows. Hosted CI results are recorded on this
milestone's pull request. Local GCC Debug validation passes all 39 CTest targets, including native bench and
protocol regressions; native-server Uno captures are in the
[gallery](gallery/README.md). The new browser regressions cover endpoint
cancel/commit/exact undo, interrupted live-button holds, appearance persistence,
compact keyboard-list access and renderer context-loss recovery. The button checks
use the demo engine; the screenshots use `embsim serve` and the Uno project.

## Schematic graphics and interaction milestone

Branch `schematic/visual-interaction`, based on merged PR #34 (`333f091`), separates
idle wire color from component strokes, strengthens selection feedback, renders
hollow pin sockets and adds pencil/grab/grabbing cursor cues. Reference/name/value
annotations include effective catalog defaults and SI units, with deterministic
candidate placement and a compact option. Device appearance preferences include
wire palette and label detail. Navigation no longer hides pins and annotations.
Candidate placement reduces collisions; it does not guarantee an overlap-free
dense drawing or move electrical geometry.

Local validation passes 149 UI tests across 34 files, typecheck, lint and
production build; 36 repeated browser workflows plus three focused visual checks;
and the real-engine schematic regression, including direct drawing, exact undo,
track editing and group operations. The visual checks cover selected wires during
simulation, hollow sockets, pencil/grab cursors, catalog defaults, persistent color
and label settings, and visible pins/details during navigation. Native light/dark,
terminal-detail and compact screenshots are in the [gallery](gallery/README.md).
The first hosted macOS browser run exposed immediate assertions before selection
color transitions and duplicate selection completion. The browser checks now wait
for the resulting color and selected duplicate before continuing. Hosted platform
validation is pending for this correction.

## Circuit organization milestone

Branch `schematic/circuit-organization`, based on merged PR #31 (`322817a`), adds
persistent flat groups, selectable canvas outlines, an object browser, native
placement locks and top-level inventory export. Group assignment, rename, removal
and lock changes use transactional history; instance deletion prunes groups.
Locked parts remain fixed obstacles in automatic placement. Direct transforms
refuse mixed locked selections. Complete copied component groups remap identities
and unlock copies. Export aggregates exact explicit model/version/parameter data,
without flattening boards or qualifying a purchasing BOM. ADR-025 is proposed.

Local verification: 146 UI tests across 33 files; typecheck, lint and production
build; 33 Chromium browser workflows (11 repeated three times); the real-engine
browser regression including groups, lock-safe dragging, undo and themes; native
session organization checks and protocol save/reload regression; 552 schema/catalog
files and documentation integrity all pass. All 36 native CTest entries pass
(90.71 seconds); the local firmware-build wrapper explicitly skips because no
supported cross compiler is installed. The hosted matrix exercises compiler and
platform coverage. Native screenshots are in the [gallery](gallery/README.md). Each
MCU's peripherals, modelled or not, are tabled in [coverage.md](coverage.md)
(generated; the `coverage_tables` test fails when it is stale).
The first hosted macOS browser run caught a focus/drag race: Objects navigation
scheduled viewport focus after the test had read its pointer coordinates. Direct
navigation now focuses synchronously unless a preview must first be discarded;
the drag regression verifies a stable unobscured header before reading coordinates.
The corrected organization workflow passes 10 consecutive local browser runs;
typecheck, lint and production build pass. Hosted cross-platform validation is
pending for this correction.

## Schematic cross-platform CI corrections

The authoring PR's first hosted run exposed an extensionless-import collision:
`SelectionProperties.tsx` and `selectionProperties.ts` resolved to the helper on
case-insensitive filesystems. The helper is now `parameterEditing.ts`, and the
repository policy checks ambiguous source-module paths with regression fixtures.
An unchanged delayed server refresh also invalidated active wire drafts; refresh
now retains revision identity when canonical project content is unchanged, while
real changes still invalidate stale drafts. The selection browser assertion now
checks the logical selection rather than counting only viewport-mounted symbols.
Failed browser jobs retain screenshots and traces for diagnosis.

Local verification: 138 UI tests across 32 files, typecheck, lint and build pass;
source-path policy and its fixtures pass. The hosted cross-platform rerun passed all 17 checks, including the merge
gate: [run 38965461112](https://github.com/EmbLinkOs/EmbSIM/actions/runs/38965461112).

## Schematic authoring and repair milestone

Based on merged main `2183be8` (including PR #29's native peripheral changes),
`schematic/authoring-workbench` integrates existing-track segment repair, exact
part/board placement, group offsets, shared-parameter batch editing, contextual
actions, net drawing/terminal controls and navigable structural/drawing checks.
Track reshaping previews before committing, preserves original endpoints and
same-net contacts, cancels without history and commits geometry in one undo step.
Clearing a drawing keeps the net; terminal disconnection is a separate action.

The check report runs in a cancellable revision-bound worker with timeout/error
recovery. It identifies structure and drawing problems, provides filters and
location/repair actions, and treats unused pins as information. Declared hidden
terminals are distinguished from invalid references. This does not implement
full electrical rules, physical clearances, persistent routing constraints or
large-sheet qualification. ADR-024 is proposed for review.

Editing rejects stale/concurrent snapshots and preserves history entries when
undo/redo fails. Placement no-ops preserve manual tracks. Foreign-net tracks no
longer follow a coincident pin when a part moves or is removed. Focus-mode layout
namespaces restore panel order/weights without remounting the canvas.

Evidence on the integrated tree:

- 136 UI tests across 31 files pass, including worker stale-result/failure/retry/
  timeout cases, junction-preserving track reshaping, hidden terminals, batch
  parameter validation, edit guards, failed history recovery and dock restoration.
- Typecheck, lint and production build pass. Ten Chromium workflows pass: pointer
  and numeric track edits, Escape/pointer cancellation, contextual keyboard
  actions, exact placement, mixed batch properties, check-to-route repair,
  terminal disconnection, manual bends, previous layout/background and run flows.
- The native engine rebuilds on current main. All 35 CTest entries report success
  in 101.55 seconds; `firmware_build` explicitly skips compilation because no
  supported firmware cross-toolchain is installed (34 entries perform checks).
- The expanded real-server browser regression passes preview/apply/undo/redo,
  connection/auto-arrangement, explicit bends, layout-only segment repair,
  native batch properties and geometry-clear/check navigation with exact project
  restoration. Light, dark and compact views are captured in the gallery.
- Documentation integrity checks 110 documents and 103 requirements without
  problems; banned-construct checks pass. Hosted platform/desktop checks are
  pending, and no new macOS/Windows execution is claimed.

## Manual schematic wire authoring

The schematic accepts multiple orthogonal wire legs, with visible committed
waypoints and a live final leg. Space flips the live elbow, Backspace/Delete
removes a leg, and Escape cancels without a project mutation. Arrows and Enter
support keyboard routing. Explicit routing takes precedence over smart wiring
and auto-arrange when the connection is completed; the entire connection is one
undoable edit. These waypoints are geometry, not persistent routing constraints;
a later automatic layout may replace them, and no clearance validation is claimed.

Evidence on `schematic/manual-wire-routing`, based on merged main `977957e`:
115 UI tests, typecheck, lint and production build pass. Six Chromium smoke
workflows pass, including pointer bends, elbow flipping, backtracking, keyboard
bends, cancellation and undo with auto-arrange both enabled and disabled. The real native-server
schematic regression also passes: unfinished bends do not mutate the project,
the completed bend survives serialization, components remain in place and one
Undo restores the complete previous project. Documentation integrity checks 109
documents and 100 requirements with no problems. Native code is unchanged; no
new native build or full native test run is claimed for this UI change.

## Continuous canvas appearance and reloads

A viewport-covering, world-anchored background replaces the finite SVG sheet
surface that exposed a gray strip after panning. Engineering, line, dot, cross
and plain patterns have paper, warm, graphite and blueprint palettes in both
themes. Spacing, line/dot weight and contrast are adjustable. Validated local
preferences persist through reloads and apply to Design, inside and Run views;
editing snap and electrical data remain independent. System typography, rounded
chrome and restrained shadows refine the schematic workspace.

Reload testing exposed a native-server stall while the old WebSocket remained
attached. Auxiliary HTTP requests are now serviced between session polls without
replacing its notification target. Host/origin restrictions remain enforced; a
second session upgrade returns HTTP 409 and incomplete auxiliary headers have a
250 ms deadline. The regression covers asset delivery while the circuit is
running, time advancement, authority rejection, busy upgrades and partial requests.
ADR-022 records the proposed decision for review.

Integrated main `067852b` (including the Pico SDK work) on Linux, GCC 14.2,
Python 3.12 and Node 24. Observed locally:

- All 34 development CTest outcomes pass. The firmware-build wrapper explicitly
  skips compilation without a supported toolchain; other targets execute.
- The affected host, HTTP asset and protocol tests pass under Address/undefined-
  behavior sanitizers (3/3). This is targeted sanitizer evidence, not a claim
  that the entire sanitizer suite was rerun.
- All 114 UI tests pass; typecheck, lint and production build pass. All four
  Playwright smoke tests pass, including preference persistence and pan coverage.
- The new native-engine browser check passes for five patterns, four palettes,
  size/contrast/spacing changes, three consecutive reloads, reset, both themes
  and 1024×700. Pixels sampled across a panned plain canvas match the selected
  surface color; the background fills the viewport without a finite-sheet seam.
- The existing native-engine schematic/undo and library/live-input/waveform
  browser regressions also pass without page errors.
- All 552 JSON files validate; documentation checks 108 documents and 99
  requirements with no problems. Repository policy checks pass.

The [gallery](gallery/README.md) contains original screenshots. Remote platform
and installer checks for this branch remain to be observed.

## Schematic Studio

The Design perspective now includes connected auto-placement, route-only planning,
whole-sheet and selected scopes, spacing/direction options, reviewable worker
previews, layout-only application and single-step undo. Placement avoids existing
symbols; an opt-in connect-then-arrange mode combines connectivity and layout in
one edit. Smart wire previews and transform rerouting use exact pin anchors,
obstacle clearance and foreign-net track reservations. Missing geometry remains
an airwire with a warning; disconnected drawn clusters are counted correctly.

The ribbon adds alignment/distribution, component/net search, net spotlight,
selection focus, side-panel focus mode and display controls. The canvas adds a
navigator and grid-compatible magnetic drag guides. The
[user guide](user/schematic-studio.md) documents heuristics and limits, including
unchanged label/bus positions, allowed crossings and bounded planning searches.
The desktop worker policy permits the bundled worker; installers still require
platform CI evidence.

Integrated with main `baf3f49` (including the vendor-firmware/analog-reference
work) on Linux, GCC 14.2, Python 3.12 and Node 24. Observed on this branch:

- All 33 development CTest outcomes pass on the integrated revision. The
  firmware-build wrapper explicitly skips compilation because no supported
  firmware toolchain is installed; the other targets execute, including replay,
  analysis, catalog assertions, multiboard and headless examples.
- All 108 workbench tests pass, including layout determinism, obstacle/track
  routing, transformed anchors, free placement, disconnected airwire clusters,
  magnetic guides, presentation-only application and stale-preview rejection.
- TypeScript, lint and the production build pass. The three Playwright smoke
  tests pass, including the new worker-preview/apply/undo/focus-mode workflow;
  this test is included in the existing three-platform smoke job.
- The native-engine schematic browser regression passes: Preview sends no
  transactions; Apply changes only layout and does not restart the engine;
  Undo/Redo restore geometry; connect-then-arrange undoes as one edit. Finder,
  display controls, light/dark themes and 1024×700 are exercised without page
  errors. The existing real-engine library/live-control regression also passes.
- All 552 JSON files validate with no errors/warnings; documentation integrity
  checks 107 documents and 97 requirements with no problems. Policy checks pass.

This branch's remote platform/installer checks remain to be observed. Local
Chromium evidence does not establish native webview behavior on every platform.

## Scenario editor hardening

Integrated with main `7ddeafb` after PR #14. The Verify editor checks draft timing, assertion IDs,
input identifiers and expectation fields before Run or Save. Invalid JSON remains
visible and blocks execution instead of silently using an earlier expectation.
Edits clear previous assertion badges. Project binding and model-specific value
validation remain engine responsibilities. This change does not qualify electrical
models or complete the remaining release gates.

Observed locally: 92 UI tests pass, including editor correction and validation
regressions; TypeScript checks, lint and the production build pass. Documentation
integrity checks 103 documents and 96 requirements with no problems. Native code
is unchanged. This branch still needs its own platform CI results.

## Release hardening

The [1.0 readiness checklist](engineering/release-readiness.md) tracks release
gates separately from implementation milestones. Debugger board selection now
clears inspected state and rejects responses from an earlier selection; delayed
and overlapping request regressions are covered in the workbench store tests.
Observed on this branch: all 86 UI tests, TypeScript checks, lint and the
production build pass. Documentation integrity checks 103 documents and 95
requirements with no problems. Native code is unchanged; native and platform
CI results have not been rerun locally for this slice. PR #14 was subsequently
observed with all 17 remote checks passing, including platform builds, desktop
and browser smoke jobs. This is not a declaration
of 1.0 readiness.

## Implemented subset

| Area | Current implementation | Evidence and limits |
|---|---|---|
| Design | Architecture 01 to 09 (ADR-001 to ADR-021 and ADR-023 accepted; ADR-022/024 proposed), SPEC-001 to SPEC-014, 103 requirements | Reviewed documents; `tools/docs/check_docs.py` passes |
| Repository | CMake presets, Makefile wrapper, CI matrix, documentation and policy workflows, templates, ruleset JSON, policy and banned-construct checkers | Local runs of the checkers; remote workflow results are recorded here when observed |
| Core: scheduler and time | Rational clocks, class-ordered event heap, delta-cycle bound, cancellation by generation (SPEC-002) | `test_sched`: ordering, cancellation, overflow, limits, 72 MHz and 32.768 kHz clocks exact over 10^9 edges |
| Core: nets and digital engine | Pin models and logic families, four-value resolution with strengths, contention and floating diagnostics, inertial delays, islands and bridges, the model API; gates, flip-flop, sources, pulls, transmission-gate switches, indicator, probe, UART virtual terminal (SPEC-003) | `test_engine`: resolution, floating inputs, gate timing, inertial pulses, clock, switch, UART loopback |
| Core: analog engine | MNA per island, Newton-Raphson, deterministic dense LU, gmin and source stepping, trapezoidal with LTE control and backward-Euler restarts, breakpoints, bridge crossings bisected to the configured resolution, steady islands quiet (not stepped until touched or a breakpoint); resistor, capacitor, inductor, V and I sources (dc, pulse, sine, pwl, exp), switch with bounce, potentiometer, diode, LED, Zener, NPN/PNP, NMOS/PMOS, op-amp, regulator, pull (SPEC-004) | `test_analog`: 1224 checks (including multi-island stepping and regulator rails) and seven analytic fixtures under GCC and Clang, each also against ngspice 42's solution of the same netlist (`ngspice.json`, `tools/analog/ngspice_fixture.py --check` in CI): operating points agree to seven digits, transients within 6.4 mV (0.21 %); BDF2, junction capacitances, sparse solver not implemented |
| Core: models and catalog loading | Registry of primitives with merged digital and electrical tiers; libraries by path; component instantiation with parameter validation, presets, `$param` pin models and composite expansion through ports; boards as composites whose MCU runs firmware (SPEC-005, 006, 007 subset); user libraries from `$EMBSIM_LIBRARIES` or the user data directory | `test_session`: temporary library, loader, inputs, snapshots, scenario assertions, determinism across sessions, composite expansion, bad project |
| Lua model tier | Lua 5.4.7 vendored (`third_party/lua`, MIT), one state per instance with its own environment, text chunks only, a 1,000,000-instruction budget per evaluation and a 64 MiB memory cap; `ctx` with pins (value, level, voltage), drive, release, timers, inputs, diagnostics and `print`; I2C and SPI slaves decoded in C (`ctx.bus.i2c_slave`, `ctx.bus.spi_slave`); `on_input`, `on_time`, `on_set`, `observe`; errors and budget overruns stop the instance until reset (SPEC-013 §2) | `test_lua`: a counter, determinism across runs, faults and the budget, I2C and SPI slaves, syntax errors; 38 catalog parts are Lua models and pass their fixtures |
| Component SDK and validation | `embsim catalog validate [DIR] [--library] [--json]` in the engine: every item instantiated under each fidelity policy it declares and its fixtures run as project and scenario, items reported ok, broken or invalid; `catalog new component\|board NAME [--lua]` writes a template that validates as written; `catalog install DIR\|ARCHIVE.zip\|URL [--to] [--force]`: a directory, a zip archive (stored and deflated entries, the inflater and the reader EmbSIM's own) or a URL fetched with curl, unpacked into a staging directory that is removed after (SPEC-014 §3 to §6) | `catalog_sdk` (`tests/protocol/test_sdk.py`): a Lua component and a board from the templates validate, install into a user library and are listed from it through `$EMBSIM_LIBRARIES`; the same library zipped deflated with a top directory, stored at its top, at fast compression and served over HTTP installs byte for byte the same files; an archive with an entry outside the library, a corrupt one (its CRC-32) and a file that is not one are refused, and no staging directory is left; a part whose fixture fails is reported broken and install refuses it; `test_zip` (stored, fixed-code and dynamic-code streams from zlib, an archive, and every truncation and one-byte change of each refused or decoded in bounds under ASan and UBSan) |
| Importers | `tools/importers/rtos2board.py` (EmbLinkRTOS board YAML), `dts2board.py` (DeviceTree source, its own parser), `kicad2project.py` (KiCad netlist to a laid-out project), sharing `boardgen.py`; `svd2regs.py` (SPEC-014 §7); `edc2regs.py` (a PIC32's or a PIC24's EDC from Microchip's device pack, with a PIC32's CLR/SET/INV registers and vectors, a PIC24's PPS functions and configuration words, pin functions and the package) | `importers` (`tools/test_importers.py`): the boards they write validate and pass their power fixture; `edc2regs.py` on a made-up PIC32's and PIC24's EDC; `examples/kicad-flasher` is a KiCad netlist imported and run |
| Core: session and protocol | Command path with virtual timestamps and the input log, probes (digital on change, analog and value at a rate, thresholded digital on analog nets), snapshots and topology, pacing slices, notifications, scenarios with assertions and the run report, project transactions (edits from the workbench: layout-only commits keep the run, circuit changes are checked then rebuild it) and `project.save`, the JSON-RPC method table over in-process, stdio and WebSocket (SPEC-008) | `test_session` (in-process), `tests/protocol/run_conformance.py` (nine cases on stdio and WebSocket, transports compared), `test_host` (files, sockets, SHA-1, base64, frames) |
| MCU runtime | Cortex-M3 core extracted from EmbCC (Thumb-2, NVIC, SCB, SysTick, DWT, FPU), with ARMv7E-M's DSP instructions for the Cortex-M4 (the parallel adds and subtracts, SEL, the saturating, packing and dual multiplies), and what an RTOS port relies on (SPEC-009 §3): the MPU on the parts that have one (PMSAv7 and PMSAv6, eight regions, subregions, the background region, HFNMIENA, MemManage with MMFAR), the default map's execute-never regions, LDRT and STRT, unprivileged access to the private peripheral bus refused, stacking and unstacking faults taken as derived exceptions, and lazy floating-point context stacking (LSPACT, FPCAR, FPDSCR), bus with bit-band and aliases, ELF and Intel HEX loaders, semihosting; decoded instructions kept per memory region's generation and bursts of instructions between the run's checks, and lazy device clocks on the STM32s (about 31 million instructions a second on a release build for a busy firmware at 180 MHz); register files from EmbSIM JSON (ADR-020); STM32F1 models: RCC, GPIO, AFIO, EXTI, USART, TIM (time base, and output compare and PWM on the AFIO pins), ADC1 and ADC2 (the regular group from the pins' voltages, at the ADC clock), SPI1 and SPI2 as masters or slaves (receive-only, bidirectional, MODF, CRC) and I2C1 and I2C2 as masters or slaves (10-bit addresses, stretching, arbitration, PEC, SMBus) on their pins, IWDG and WWDG, RCC_CSR's reset flags, FLASH, PWR; runs in quanta on the scheduler with pins as signal events (ADR-019); USART frames on the TX pin and decoded from the RX pin; NRST and BOOT0 (SPEC-009) | `test_mcu` (11022 checks): EmbCC's Blue Pill golden fixture with five documented USART timing deltas, the same output from a description converted from ST's SVD, serial input, a breakpoint-and-continue run equal to an uninterrupted one (MCU-006); 101 DSP and ARMv7-M instructions over 4096 operand pairs each give QEMU's mps2-an386 results and flags (`dsp_differential`, the expected hashes checked against QEMU in CI); 28 cases of the MPU, privilege, stacking faults and FP stacking print what QEMU's mps2-an386 prints (`cm4_core_differential`, tests/firmware/cm4-core, its expected output checked against QEMU in CI); `examples/bluepill-button-led` runs firmware built by Clang and by EmbCC; an application built with ST's unmodified STM32F1 HAL and CMSIS device files (their GCC startup and linker script, a CubeMX Makefile's flags, GCC 13.2 and newlib-nano) passes 7/7, in CI's linux-gcc job too: the HAL's clock tree to 72 MHz from HSE through the PLL, `HAL_Delay` on SysTick, EXTI0, TIM2's update interrupt and USART1 both ways; a second, `adc_pwm`, 6/6: the HAL's ADC calibrated at 12 MHz, polled and by its interrupt, TIM3's PWM at 1 kHz exactly, and the PWM's average through an RC low-pass read back at 0.8 and 2.5 V; a third, `spi_i2c`, 6/6: the HAL's SPI master at 2.25 MHz against the catalog's 25LC040 (WREN, WRITE, RDSR through its write cycle, READ) and its I2C master at 100 kHz against the 24C02 (Mem_Write, IsDeviceReady through the write cycle, Mem_Read of one, two and five bytes, an absent address) (`tests/vendor/stm32cube/run.py`); `test_mcu`'s TIM3 PWM edges to the cycle and an ADC conversion's 504 cycles and code |
| MCU runtime: AVR | AVR5 core extracted from EmbCC (cycle-exact, SREG, the vector table, SLEEP), its state a parameter; ATmega328P models over the register file written from the datasheet: PORTB/C/D with pull-ups and PUD, INT0/INT1 and pin-change interrupts, Timer/Counter0, 1 and 2 in every waveform mode with double buffering and the OC pins switching at their clock, Timer/Counter1's input capture (ICP1 or the comparator, the noise canceler, ICR1 at the edge's clock), USART0 at its baud rate, the 10-bit ADC from the pins' voltages, the SPI master (modes 0 to 3, SPI2X, WCOL, SS's mode fault) on PB2 to PB5 and the TWI master (the datasheet's master statuses, repeated START, TWWC) open-drain on PC4 and PC5, the watchdog (time-outs on its 128 kHz oscillator, WDR, interrupt and system reset modes, the timed sequence, WDRF through its reset), the analog comparator (AIN0 or the bandgap against AIN1 or the ADC multiplexer's pin, its edges and vector, the crossing found in the analog solver), the EEPROM, the clock prescaler; the peripherals' clocks run lazily (owed cycles paid before the core touches a timed device, at the next event and before outside input) and a lone AVR with quiet islands runs 100 µs quanta with its UART frames queued on the TX pin (SPEC-009 §8); an AVR disassembler; DWARF with 2-byte addresses | `test_avr` (1318 checks: flags, cycles, Timer0's interrupt, fast PWM's overflow flag clock by clock, a USART frame's cycles, ports, the ADC, the SPI's frames in loopback, its timing, modes, WCOL and mode fault, the TWI's START, a NACKed SLA+W, a repeated START, SLA+R and bytes ACKed and NACKed, STOP, TWWC, SCL's period and prescaler, the watchdog's time-out, timed sequence, both modes, WDR and its reset with WDRF, the comparator's ACO, ACI, edges, vector, bandgap, ACME, ACD and its thresholds on the pins, input capture's edges, noise canceler, ICR1 as TOP, the comparator as its source and its vector, the disassembler); `examples/uno-blink-serial` 7/7 with firmware built by clang and by EmbCC; Arduino sketches built with the unmodified ArduinoCore-avr (11b9130, avr-gcc 7.3 with the IDE's flags; Blink 924 bytes as the IDE reports) pass their scenarios, in CI's linux-gcc job too: Blink 4/4, a core tour 10/10 (`delay`, `millis`, `analogRead`, `analogWrite` on Timer0 and Timer1, `attachInterrupt`, `Serial` at 115200 both ways), and `spi_wire` 5/5: the core's SPI library at 4 MHz against the catalog's 25LC040 (WREN, WRITE, RDSR through its write cycle, READ; every SCK edge through D13's LED counted) and its Wire library against the 24C02 (a page write, NACKed polls, a repeated START into `requestFrom` at 100 and 400 kHz, an absent address), and `watchdog` 5/5: avr-libc's WDT interrupt waking the part from power-down four times, then `wdt_enable` left to time out twice, a boot count in `.noinit` surviving each reset, MCUSR's PORF then WDRF, and `comparator` 3/3: the comparator's interrupt times an RC network charged and discharged through 10 kOhm into 1 uF against 2.5 V, 6960 and 6956 us measured with `micros()` where R * C * ln 2 is 6949 us, and `input_capture` 4/4: Timer1's input capture measures `analogWrite(3, 64)` wired to ICP1, 32640 cycles a period every time (490.196 Hz) and 8192 high, as the datasheet's phase-correct PWM gives (`tests/vendor/arduino/run.py`, which builds the libraries a sketch includes); EmbCC's QEMU-refereed AVR fixtures not run here (no QEMU) |
| MCU runtime: RISC-V | RV32IMAC core extracted from EmbCC (Zicsr, machine-mode traps, compressed instructions), its state a parameter, interrupt lines from the devices, WFI as sleep; physical memory protection (the part's entries, OFF, TOR, NA4 and NAPOT, locks binding machine mode, user mode and MPRV, partial matches failing); FE310-G002 models written from SiFive's manual: the CLINT (mtime at the 32.768 kHz RTC, exactly), the PLIC (priorities, threshold, claim and complete), the GPIO (interrupts, IOFs), the UARTs (FIFOs, watermarks), the PRCI (HFROSC, HFXOSC, PLL), the always-on domain's watchdog (key, scale, comparator, zerocmp, sticky interrupt, a chip reset the domain survives) and RTC, PWM0-2 (comparators, zerocmp, deglitch, sticky, one-shot, the channels on IOF1) and SPI1 and SPI2 (FIFOs, modes 0 to 3, csmode AUTO, HOLD and OFF, the delays, watermarks) on their pins; the RISC-V disassembler | `test_riscv` (285 checks: ALU and divide cycles, stores at negative offsets, an ECALL trap, the CLINT interrupt at exactly one RTC second, a UART frame, the PLIC's reserved words, a locked NAPOT entry against machine mode's store, user mode with and without an entry, the RTC's compare at one second, the watchdog's key, zerocmp period and food, its reset and what survives it, the disassembler); `examples/hifive1-blink` 7/7 with firmware built by clang and by EmbCC; fourteen of SiFive's freedom-e-sdk examples, built with the unmodified freedom-metal library (the SDK's pinned 1cec4a2) and the HiFive1 Rev B's BSP by riscv64-unknown-elf-gcc 13.2 with picolibc, pass their scenarios (47 assertions), in CI's linux-gcc job too: hello, sifive-welcome (the RGB LED on the CLINT's timer), csr, uart-interrupt (through the PLIC), example-user-mode, minimal-boot, example-rtc, example-watchdog, example-pmp, timer-interrupt, software-interrupt, return-pass, example-pwm (PWM1 at 1 Hz on the RGB LED), example-spi (three frames on SPI1 with CS held) (`tests/vendor/freedom-metal/run.py`; an example's exit status printed by a wrapper around freedom-metal's `metal_shutdown`); example-i2c waits on I2C0, which is not modelled, and the PMU's sleep is not |
| MCU runtime: RP2040 | Cortex-M0+ (Armv6-M) on both cores, interleaved on one clock with each core's NVIC, SysTick and event register (WFE, SEV), core 1 launched through its bootrom's FIFO handshake and powered off and on by the PSM; the register file converted from Raspberry Pi's RP2040.svd (BSD-3-Clause) on the part's bus rules (atomic XOR, set and clear aliases; byte writes across the word) with peripherals held in reset by RESETS; models extracted from EmbCC: the clocks (XOSC, ROSC, the PLLs, the generators, FC0) with clk_sys as the core's clock, PSM, the watchdog's microsecond tick and its reset, the timer's alarms, IO_BANK0 and the pads (function select, overrides, pulls, interrupts to each core), the SIO (GPIO, spinlocks; each core's CPUID, divider and FIFO, SIO_IRQ_PROC0/1), the PL011 UARTs timed at their divisor, the SSI with the Pico's flash; the PWM's slices (fractional divider, free-running and phase-correct, double-buffered CC and TOP, the wrap interrupt) on their pins and the ADC (clk_adc's 96 cycles a conversion, GPIO26-29 and the temperature sensor, START_MANY with DIV, round robin, the FIFO and its interrupt), the SPI masters (PL022: FIFOs, 4 to 16 bits in modes 0 to 3, CSn, loopback, overrun) and the I2C masters (DW_apb_i2c: the command FIFO with RESTART and STOP, open-drain SCL and SDA at HCNT and LCNT, the bus held between commands, aborts and their source) on their pins, written for EmbSIM; a bootrom stand-in that checks boot2's CRC, copies it to SRAM and enters it, and the bootrom's documented functions done by the host (ADR-021): the table lookup, the bit operations, memcpy and memset, the flash functions (erase and program with the W25Q16JV's times), reset_to_usb_boot, the float and double tables (version 3, correctly rounded arithmetic, the bootrom's conventions) | `test_rp2040` (1828 checks: SPI0's frames in loopback, its timing, FIFOs, overrun and modes, I2C0's NACKed address and its abort, a held bus kept through a disable and enable, a repeated START, the clear registers, a disable that lets the bus go; PWM slice 0's edges, wrap and buffered CC, the ADC's conversion time on clk_adc and none without it, its codes, round robin and FIFO, the boot through boot2 and a bad CRC, the aliases and byte writes, RESETS, PLL_SYS at 125 MHz measured by FC0, a timer alarm on the 1 µs tick, the divider, a UART bit time, core 1's launch, CPUID, a WFE ended by the other core's SEV, the PSM's power, the ROM's functions through its tables); `examples/pico-blink-serial` 7/7 and `examples/pico-dual-core` 9/9 with firmware built by clang and by EmbCC; an application built with the unmodified Pico SDK 2.3.1 (its CMake, GCC 13.2, newlib-nano) passes 9/9, in CI's linux-gcc job too: the SDK's clocks (clk_sys 125 MHz), float and double through the bootrom's tables, the divider, core 1 through `multicore_launch_core1` and the FIFO, `flash_range_erase` and `flash_range_program` read back through the XIP, a repeating timer, a GPIO interrupt, stdio on UART0 both ways; a second, `pwm_adc`, 7/7: `hardware_pwm` at 1 kHz exactly with its wrap interrupt, `hardware_adc` on a 1.0 V source, the temperature sensor and the PWM's average through an RC low-pass (0.8 and 2.5 V), free-running conversions round robin through the FIFO's interrupt; a third, `spi_i2c`, 7/7: `hardware_spi` and `hardware_i2c` against the catalog's 25LC040 (WREN, WRITE, RDSR through its write cycle, READ) and 24C02 (a page write, NACKed polls through its write cycle, a current-address read and a random read after a repeated START, an absent address), every SCK and SCL edge counted (`tests/vendor/pico-sdk/run.py`); the SDK's `runtime_init_usb_power_down` touches USB while it is held in reset, as on the part, which is one `mcu.held_in_reset` warning |
| MCU runtime: STM32F446RE (in progress, the NUCLEO-F446RE plan) | The Cortex-M4F core with FPU, DSP and the MPU (`core.mpu`); the register file converted from ST's Apache-2.0 STM32F446.svd by `tools/importers/svd2regs.py`, with reset values and interrupt assignments corrected from RM0390 where the SVD differs (recorded in its source); the LQFP64 pinout and the ADC channels converted from ST's BSD-3-Clause open pin data by `tools/importers/stm32pins.py`; the clock tree to 180 MHz (`stm32-rcc`: HSI, HSE as crystal or ST-Link MCO bypass, LSE, LSI, the main PLL's P and R outputs, the bus prescalers, TIMPRE, the core clock from HCLK; out-of-spec VCO, SYSCLK, APB clocks and flash wait states reported) and PWR's voltage scaling and over-drive (`stm32f4-pwr`), with the low-power modes: Stop (the 1.2 V domain's clocks stopped but the RTC's and IWDG's, HSE and the PLLs off, an EXTI line's interrupt or event waking the core on HSI, under-drive's UDRDY) and Standby (RAM lost, the backup SRAM kept by BRE, the pins let go but the WKUP pins and RTC_OUT, a WKUP edge, an RTC event, NRST or the IWDG ending it with a power reset of all but the backup domain, WUF, SBF, EWUP, CWUF, CSBF, the option bytes' nRST_STOP and nRST_STDBY), and the core's SLEEPDEEP, SLEEPONEXIT and SEVONPEND; the flash interface (`stm32f4-flash`: KEYR's unlock, programming at PSIZE's width with its errors, sector and mass erase, EOP and the interrupt, the option bytes through OPTSTRT and their write protection; the times not modelled); the GPIO ports (`stm32f4-gpio`: the four modes, push-pull and open-drain, the speed setting the pin's output resistance and edge rate, pulls, BSRR, LCKR's lock sequence, AFR choosing among the alternate functions of `io.alternate`, converted from ST's open pin data), SYSCFG (`stm32f4-syscfg`: EXTI line ports, the memory at 0, the compensation cell) and EXTI's 23 lines with events; RCC's peripheral reset bits; the timers (`stm32-tim`, shared with the F1: up, down and center-aligned counting, repetition, output compare and PWM, input capture with filters, complementary outputs with dead time and break, LOCK, COM, the slave modes and encoder mode, external clocks, TRGO between timers); SysTick's HCLK/8 reference; the USARTs and UARTs (`stm32-usart`, shared with the F1: frame formats with parity, OVER8, overrun and the error flags, IDLE, breaks and LIN, mute mode, flow control, half-duplex, the synchronous clock), the runtime carrying each frame's format on the TX waveform and to the RX decoder; DMA1 and DMA2 (`stm32f4-dma`: streams and channels by RM0390's request mapping, direct and FIFO modes, circular and double buffer, memory-to-memory, the flags and interrupts) with the USARTs', SPIs' and timers' requests; SPI1-4 (`stm32-spi`, shared with the F1: master and slave, receive-only and bidirectional, NSS and MODF, the TI mode, CRC with the automatic CRC frame after a DMA transfer); I2C1-3 (`stm32-i2c`, shared with the F1: master and slave, 10-bit addresses, clock stretching, arbitration, BUSY and BERR from the bus, PEC, SMBus, DMA with LAST); ADC1-3 (`stm32f4-adc`: both groups, the timers' and EXTI's triggers, scan, continuous and discontinuous, RES, OVR, the watchdog, DMA, the internal channels and the temperature sensor at the world's temperature; the common block's dual and triple modes) and the DAC (`stm32f4-dac`: triggers, noise and triangle, DMA and underrun, the buffer, its voltage on PA4 and PA5); the device signature with its calibration values; the IWDG and WWDG (`stm32-iwdg`, `stm32-wwdg`, shared with the F1) and the RTC (`stm32f4-rtc`: calendar, alarms, wakeup, timestamp and tamper, RTC_OUT, calibration, backup registers); RCC_CSR's reset flags and the backup domain kept through resets; the other peripherals keep their register values and raise `mcu.unsupported_access` (docs/coverage.md) | tests/firmware/cm4-core prints QEMU's output on a bare board around the part; tests/unit/test_stm32f4.c (the HAL's clock sequence to 180 MHz, HSE's kind, the spec warnings, the backup domain, GPIO's modes and lock, the AF table, EXTI on PC13 through SYSCFG, an internal line, an event, the boot remap, PWM on PA5, center-aligned and repetition counting, capture with overcapture and the filter, dead time and break on TIM1, TIM3 clocking TIM2, a quadrature encoder, one-pulse, SysTick's reference, USART2's frames, parity, breaks, OVER8, overrun, the error flags and IDLE, mute mode, nCTS and nRTS, the synchronous clock, USART2 sent and received by DMA in circular and double-buffer modes, memory-to-memory through the FIFO, the FIFO configuration and transfer errors, a timer's DMA burst, SPI1 as master with OVR and MODF, as slave across NSS, CRC-8 and CRC-16, full-duplex DMA with its CRC, the bidirectional, receive-only and TI modes, I2C1 as master against a device (POS read, stretching, ARLO) and as slave against a hand-driven bus (stretching, STOPF, AF, dual address, general call, BERR), I2C DMA with PEC and LAST, I2C1 wired to I2C3 with a 10-bit address and PEC, ADC conversions against the calibration words, scan by DMA, OVR, injected groups, TIM2-paced conversions, dual mode, the DAC's timing, voltage, triangle and underrun, the reset flags, the watchdogs' timing, window and hardware option, the RTC's calendar, alarms, wakeup, backup domain, timestamp and tamper, and its time across a switch to 180 MHz, Stop's stopped timer and RTC wake-up event, under-drive's UDRDY, Standby's lost RAM, kept backup SRAM, released pins, WKUP edge and kept flags, the option bytes' low-power reset, SEVONPEND and SLEEPONEXIT, the flash interface's keys, programming and its errors, a sector erase, the option bytes' write protection and OPTSTRT, a slave receiving over a written DR and no STOPF after a NACK); STM32CubeF4's own examples for the NUCLEO-F446RE built unmodified (its STM32CubeIDE projects' sources, defines and linker scripts, GCC 13.2, newlib-nano) pass 21 scenarios on the board, in CI's linux-gcc job too: GPIO_IOToggle, HAL_TimeBase_TIM, _RTC_ALARM and _RTC_WKUP, RCC_ClockConfig, RTC_Calendar (`var` probes on its time and date strings), TIM_PWMInput (a 20 kHz, 25 % input measured), UART_Printf, UART_ReceptionToIdle_CircularDMA (typed text echoed across the buffer's wrap), PWR_CurrentConsumption in Sleep, Stop, under-drive Stop, Standby (woken by PA0) and Standby with the RTC with and without the backup SRAM (woken 20.48 s later by the RTC on LSI), the four I2C_TwoBoards examples on two boards (ComPolling, ComIT, ComDMA: the buffers whole, SCL's edges counting exactly the bytes; AdvComIT: 19 checked write-and-read cycles in 0.65 s), and EEPROM_Emulation through the flash interface (`tests/vendor/stm32cubef4/run.py`); `tools/validate.py --all`; the importers' unit tests |
| Board: NUCLEO-F446RE (in progress, the NUCLEO-F446RE plan, step 9) | `emblink/nucleo_f446re` (MB1136 C-03, UM1724 Rev 14): the STM32F446RET6 with HSE from the ST-LINK's 8 MHz MCO and X2 for LSE; LD2 on PA5, B1 on PC13 (R29, R30, C15), B2 and C14 on NRST, LD3 on +5V, BOOT0 held low by R33, C26 on VCAP_1; the power paths (U5V through T2 STS7PF30L and JP5, VIN through U3 LD1117S50 and D4 STPS2L30A to E5V, U4 LD39050PU33R to +3.3V, JP6 (IDD) to VDD with an ammeter's place across it); the Arduino and morpho headers, every pin a port, D0/D1 open as the solder bridges leave them; the ST-LINK's virtual COM port as the console on USART2; original artwork; linker script and toolchain profiles. Not yet: the ST-LINK itself (enumeration, its current negotiation, drag-and-drop programming, LD1), the STM32's supply current, `reference.json` and the 3D bench's exact positions | the board's fixtures (USB power, VIN with JP5 left on U5V and moved to E5V, JP6 removed with an ammeter, B1); `examples/nucleo-f446re-blink-serial` (180 MHz from the MCO, LD2's 1 Hz blink, B1 through EXTI13, the VCP's echo, `reg`, `mem` and `var` probes; `examples_headless`); the STS7PF30L's and STPS2L30A's fixtures |
| Board: ESP32-C3-DevKitM-1 (the RTOS boards plan, part 2, step 6) | `emblink/esp32c3_devkitm1` (V1.0 schematic and dimension drawing): the ESP32-C3-MINI-1 (`espressif/esp32c3`'s `mini1` package); VBUS through D1 (1N5819) to 5V, U2 (SGM2212-3.3) to 3V3, D5 on 5V through 5.1 k; R5 (10 k) and C6 (1 uF) on CHIP_PU, RST (SW2) on it, BOOT (SW1) on GPIO9, R20 holding GPIO8 high; the SK6812MINI-HS RGB LED on GPIO8 (`emblink/sk6812mini_hs`); J1 and J3, every pin a port; the CP2102N as the console's bridge on UART0 and a host serial port; original artwork; its features on the 3D bench. CHIP_EN low holds the chip off; RST restarts it with `rst:0x1 (POWERON)`, BOOT held through RST gives the ROM's download mode. `run --target` plugs it into a USB port. Not yet: the auto-program circuit (Q1, Q2) on DTR/RTS, which a pseudo-terminal does not pass, so esptool's reset into download mode; the CP2102N's USB and supply current; decoupling and bulk capacitors (C1: the analog engine does not converge with it on the regulator's output yet); `reference.json`; the first 200 ms of a run cost about 2.5 s of wall time while C6 charges | the board's six fixtures (USB power, CHIP_PU's delay and release, RST held and released, BOOT); ESP-IDF v5.4's hello_world through `embsim run --target` and BOOT+RST into download mode by hand; the SK6812MINI-HS's fixtures |
| MCU runtime: ESP32-C3 (in progress, the RTOS boards plan, part 2, steps 1-7) | `espressif/esp32c3`: the ESP-RV core (`esp-rv32imc`, TRM v1.4 chapter 1): RV32IMC with user mode and no A, only the part's CSRs (mie, mip and the standard counters are illegal), mstatus's MIE, MPIE, MPP and TW, a vectored mtvec on a 256-byte base, five-bit mcause codes, MRET clearing MPIE; the performance counter (mpcer, mpcmr, mpccr, also at 0x800-0x802 where the ROM starts it) and the user trap CSRs the ROM writes; the trigger module (eight triggers, exact or NAPOT match on execute, load or store); 16 PMP entries without static priority; the cpu_gpio CSRs. The interrupt matrix and controller (`esp32c3-intmtx`). **The boot:** Espressif's own boot ROM, from its Apache-2.0 ROM ELF (`tools/importers/esprom.py`), runs from the reset vector: its banner, the strapping (`esp32c3-gpio`'s GPIO_STRAP), the reset cause (`esp32c3-rtc`), then an ESP image loaded from the flash through SPI1, its checksum and SHA-256 checked on the SHA accelerator (`esp32c3-sha`, Typical SHA: SHA-1, SHA-224, SHA-256), and its entry; a blank flash prints `invalid header: 0xffffffff` until the RTC watchdog's flash-boot protection resets the part (`rst:0x10 (RTCWDT_RTC_RST)`, `Saved PC` from `esp32c3-assist`'s PC logging). The in-package flash, an XMC XM25QH32C (`nor.c`: its instructions, status registers, typical busy times, security registers, suspend, power-down), behind SPI0 and SPI1 (`esp32c3-spimem`: user commands and the fixed ones); the cache and MMU (`esp32c3-cache`): 128 entries of 64 KB pages on the instruction and data buses, invalidate, preload, lock and freeze. UART0 and UART1 (`esp32c3-uart`); RTC_CNTL (`esp32c3-rtc`: reset causes, the RTC timer, the RWDT, the software resets). A project's `bin` firmware is a flash image at offset 0. **Clocks and timers:** SYSTEM (`esp32c3-system`: CPU_CLK from XTAL_CLK / 2 out of reset, the PLL's 80 or 160 MHz or RC_FAST_CLK, APB_CLK, the peripherals' clock enables and resets, the software interrupts); the system timer (`esp32c3-systimer`: two 52-bit counters at 16 MHz, three comparators in target or period mode); TIMG0 and TIMG1 (`esp32c3-timg`: T0 with its prescaler, direction, alarm and auto-reload; the MWDT's stages, write key, configuration update and flash-boot protection, so a blank chip resets with `rst:0x7 (TG0WDT_SYS_RST)` after 1.3 s; the RTC_SLOW_CLK calibration); the eFuse controller (`esp32c3-efuse`: read and program commands, the chip's factory bits: v0.3, QFN32, 4 MB XMC flash, a MAC, or the one its board's settings.mac gives). **Pins:** the GPIO matrix and IO MUX (`esp32c3-gpio`, `esp32c3-iomux`: the pads' reset configuration from TRM table 5.12-1, outputs, inputs, interrupts, the UARTs on whichever pins the matrix gives them); USB Serial/JTAG (`esp32c3-usbjtag`) with no host on its pins, as on a DevKitM-1. **ESP-IDF:** the analog blocks' I2C master (`regi2c.c`), RND_DATA (`esp32c3-apbctrl`, seeded), the radio's register blocks with the PHY's calibrations answering as an ideal radio (`radio.c`, `regi2c.c`), the Wi-Fi MAC on the session's air (`wifi.c`: transmit queues, completions, the receive list with ESP-IDF's rx_ctrl header, the address filters, ACKs, the interrupt, the microsecond counter and TSF, the key table and CCMP, the random number register; each chip's random numbers its own); the SHA accelerator's DMA-SHA, the AES accelerator (`esp32c3-aes`: Typical AES, DMA-AES in ECB, CBC, OFB, CTR, CFB8 and CFB128) and the RSA accelerator (`esp32c3-rsa`: modular multiplication and exponentiation by Montgomery products, multiplication, to 3072 bits), a 40 MHz BSS's primary channel, the key entry a frame names, the ROM's layout pointer, a CPU reset clearing the permission control; ESP-IDF v5.4's hello_world runs unmodified from the ROM through its bootloader and app to `esp_restart`. **Peripherals:** the GPIO matrix carries any modelled peripheral's signals; RMT (`esp32c3-rmt`: two transmitters and two receivers over the 192-word RAM, normal, wrap and continuous modes, loop counts, the carrier, the receivers' idle threshold, filter and demodulation); LEDC (`esp32c3-ledc`: four timers, six generators, dithering, fading); I2C (`esp32c3-i2c`: master's command list with its bus timing, stretching, arbitration and timeout; slave with 7- and 10-bit addresses, the general call, ACK control and stretching); GP-SPI2 as a master (`esp32c3-spi2`: CMD, ADDR, DUMMY and data on 1, 2 or 4 lines or QPI, full and half duplex, the four modes, its buffer or DMA); the GDMA (`esp32c3-gdma`: three channels each way on descriptor lists, memory to memory); the SAR ADCs and temperature sensor (`esp32c3-adc`: one-time sampling, the timer's scanning into the GDMA, filters, thresholds, the world's temperature). the Bluetooth LE core (`ble.c`: the native clock, its timers, the interrupt FIFO, the exchange memory and table, advertising events on the air, a CONNECT_IND received, connection events as the slave and as the master with their receive and transmit descriptors and sequence numbers, channel selection #1 and #2, scan and initiating events with the CONNECT_IND). Not yet: a USB host on GPIO18/19, GP-SPI2's slave mode, TWAI, I2S, HMAC and the digital signature, Wi-Fi beyond a scan, a station, an access point, ESP-NOW and HTTP to the internet beyond a router, Bluetooth beyond advertising, scanning and connections (`docs/engineering/c3-radios.md`: steps 1 to 3 are done for a scan, step 4's air and access point are under way: ESP-IDF's `wifi/scan` lists the bench's access points with their RSSI and its `station`, as it ships, joins a WPA2-PSK network and gets its address by DHCP; step 6 is under way: NimBLE's `bleprph` advertises, a phone's scanner lists it, a tablet's GATT browser connects and discovers its database, and `blecent` on a second board connects to it as the master; Arduino-ESP32's WiFiScan, WiFiClientConnect and BLE Server and iBeacon (Bluedroid) run as they ship; scan responses are sent to the phone's active scanning and BLE packets collide on the air; ESP-IDF's http_request and Arduino-ESP32's BasicHttpClient reach a web server beyond the router) | `test_esp32c3` (the core's CSRs, atomics, counter, trigger, PMP, interrupts and WFI; the ROM on a blank flash, booting an image that reads the flash through the MMU, a bad digest; the flash's commands and busy times; the cache's staleness and invalidate; SHA-1, -224 and -256; the PC logging; the CPU clock and the software interrupts; the system timer's target and period alarms; T0's alarm and reload, the MWDT's stages and key, the RTC_SLOW_CLK calibration; the eFuses; the blank chip's TG0WDT reset; the GPIO's reset, output, input, interrupts and matrix UARTs; the periodic calibration; the analog I2C master; RND_DATA and the radio's blocks; the PHY's calibrations; the Wi-Fi MAC's queues, completions, receive list, filters, reload and TSF, its CCMP both ways, its random number register and another chip's own numbers; DMA-SHA's one- and two-block messages; AES against FIPS 197 and SP 800-38A's CBC and CTR; RSA's modular product and power, product, search position, interrupt and a wrong M' against Python's integers; the BLE core's version, clock, timers, interrupt FIFO and an advertising event on three channels, a connection event as the slave (its channel, window, receive descriptor, answer, acknowledgement and a resent packet), as the master on channel selection #2 (the specification's sample channels), a scan event and an initiating event's CONNECT_IND and window offset, a SCAN_REQ answered with the scan response, an active scan event's SCAN_REQ and the SCAN_RSP taken; the temperature sensor's own clock; USB Serial/JTAG without a host; a CPU reset's permission control; a peripheral's reset keeping its timers' rates; RMT sending and receiving through the matrix, its loops, wrap, carrier, filter and stop; LEDC's PWM, 0 and 100 %, dithering, fading and overflow counter; I2C as master against a device the test plays (writes, a repeated-START read, a NACK, the bus timing, a stretched clock, END) and as slave to a bit-banged master; GP-SPI2 in the four modes, quad I/O, LSB first and by GDMA; the ADCs' one-time samples, attenuations, grounded input and monitor, their scanning by GDMA, the temperature sensor), `tools/test_esprom.py`; ESP-IDF v5.4's own examples, unmodified, under scenarios (`tests/vendor/esp-idf`: hello_world, blink, ledc_basic, temp_sensor, adc_oneshot_read, i2c_eeprom, gptimer, uart_echo, real_time_stats, wifi_scan, wifi_station_open, wifi_station, bleprph, blecent with bleprph on a second board, Bluedroid's ble_ibeacon as sender and receiver on two boards, espnow on two boards, wifi_softap with the station example on a second board, http_request fetching example.com's page through the router; CI's esp-idf job); Arduino-ESP32 3.2.0's sketches through arduino-cli (`tests/vendor/arduino-esp32`: Blink on the RGB LED, DigitalReadSerial, Fade, GetChipID, RepeatTimer, WiFiScan, WiFiClientConnect, the BLE library's Server (Bluedroid) read by a tablet's GATT browser and its iBeacon decoded by a phone's scanner, HTTPClient's BasicHttpClient; CI's arduino-esp32 job) |
| MCU runtime: nRF52840 | Cortex-M4F; the register file converted from Nordic's nrf52840.svd (BSD-3-Clause), peripherals that share an address space merged, FICR with a part's factory words; Nordic's tasks, events and interrupts; models extracted from EmbCC: CLOCK (HFXO, LFCLK), the GPIO ports (drive modes, pulls, SENSE, LATCH, DETECT), UART0/UARTE0 and UARTE1 with EasyDMA and SHORTS, timed at BAUDRATE on their PSEL pins, TIMER0-4, RTC0-2; GPIOTE written for EmbSIM (event and task channels, PORT), and SPIM0-3, TWIM0-1 (EasyDMA, the shorts, SUSPEND, ERRORSRC; open-drain SCL and SDA), PWM0-3 (EasyDMA sequences in the four decodings, Up and UpAndDown, loops) and the SAADC (AIN0-7, VDD, gains and references, oversampling, limits, scans into Data RAM) on their pins | `test_nrf52` (275 checks: SPIM2's transfer with ORC and its events, TWIM0's NACKed address held until STOP, a TXRX through LASTTX_STARTRX and LASTRX_STOP, PWM0's two channels and its period, the SAADC's calibration, scan, codes and limit; FICR, the HFXO's debounce, the ports and GPIOTE's edges and tasks, a UARTE buffer's frames and ENDTX, a TIMER compare, an RTC compare); `examples/nrf52840dk-blink` 7/7 with firmware built by clang and by EmbCC; an application built with Nordic's unmodified nrfx 4.6.0 drivers and MDK (its system file, GCC startup and linker script, GCC 13.2 hard-float, newlib-nano) passes 8/8, in CI's linux-gcc job too: nrfx_clock's HFXO and LFXO, nrfx_uarte both ways through its RX buffers, nrfx_timer's compare and CLEAR short against nrfx_rtc's counter and compare, nrfx_gpiote's falling edge; a second, `nrfx_periph`, 8/8: nrfx_spim against a 25LC040, nrfx_twim against a 24C02 (a page write, NACKed polls, TXRX, a suspended TX then an RX, an absent address), nrfx_pwm at 1 kHz exactly and nrfx_saadc on a 1.0 V source, VDD and the PWM through an RC low-pass, every SCK and SCL edge counted (`tests/vendor/nrfx/run.py`); the radio and PPI are plain registers |
| MCU runtime: PIC32MX | MIPS32r2 with MIPS16e extracted from EmbCC, its state a parameter: the M4K (EIC mode, two register sets, fixed mapping, the core timer) and QEMU malta's 24Kc; WAIT and idle loops sleep; the MIPS32 disassembler; the PIC32MX795F512L's register file converted from Microchip's Apache-2.0 device pack (`tools/importers/edc2regs.py`) with its CLR, SET and INV registers, shared vectors and pin functions; models extracted from EmbCC: the EVIC (priorities, subpriorities, single- and multi-vector modes, the shadow set), the oscillator from the configuration words (the PLL, the switch, PBDIV, SYSKEY's lock) with SYSCLK as the core's clock, Timer1-5 with 32-bit pairs, ports A-G with the change notices and their pull-ups, UART1-6 timed at their baud rate on their pins | `test_mips` (168 checks: a delay slot, exceptions in and out of one, the core timer in compatibility and EIC modes, MIPS16e, ll/sc, the disassembler); `test_pic32` (1326 checks: CLR/SET/INV and the pins, the clock from the configuration words and the lock, Timer1, a change notice, UART2's bit time and persistent interrupt, the example's image); `examples/explorer16-pic32mx` 8/8 with firmware built by clang and by EmbCC; the ADC, SPI, I2C, DMA, USB, Ethernet and CAN are plain registers |
| MCU runtime: PIC24 | The PIC24F core extracted from EmbCC, its state a parameter (and its dsPIC33E mode with the DSP engine, untested here): every PIC24F instruction and addressing mode, REPEAT and the iterative divide, the Extended Data Space and program space visibility, table reads, traps (`mcu.trap`) and interrupts through the vector table, DISI; PWRSAV and a branch to itself sleep; program memory at twice its address and the data space at 0x2000000 on one bus; the disassembler; the PIC24FJ1024GB610's register file converted from Microchip's Apache-2.0 device pack with its PPS functions and configuration words; models extracted from EmbCC: the interrupt controller, the oscillator (FNOSC from the configuration words, OSCCON's unlock sequences, the PLL's 32 MHz and CPDIV) with FCY as the core's clock and the PPS lock, Timer1-5, ports A-G with ANS, the pulls and the interrupt-on-change; UART1-6 written for EmbSIM, timed on the pins the PPS gives them | `test_pic24` (1465 checks: instructions as xc16-as assembles them and their cycles, an address error trap, an interrupt taken from PWRSAV, the clock from the configuration words and OSCCON's lock, Timer1, the interrupt-on-change, UART2 through the PPS and IOLOCK, the disassembler, the example's image and its XC16 line table); `examples/explorer16-pic24` 8/8 with firmware built by XC16 v2.10; the ADC, SPI, I2C, the capture/compare modules, DMA, USB and the RTCC are plain registers |
| Record and replay | Recordings (SPEC-012): the engine, the project's and images' SHA-256, every input, and observations in time order (each core's interrupt entries, digital probe transitions from reset, the serial probes' text, warning and error diagnostics, the end); `embsim run --record`, `embsim replay` (exit 4 at the first divergence, with both observations; a changed project or image refused unless `--allow-drift`), `embsim record to-scenario`, and `record.start`, `record.stop`, `record.replay` (a file or the recording itself) and `record.to_scenario` over the protocol; a recording made in a running session begins with the digital probes' levels and replays from its start (`from_ps`, `from_seq`, `immediate` inputs); `tools/verify/compare_reports.py` diffs two run reports | `record_replay` (`tests/protocol/test_replay.py`): all twenty examples recorded twice in two processes with equal observations, each replayed exactly, an injected divergence, a changed image refused, a recording turned into a scenario passing, `record.replay` over `embsim sidecar`; `python_client`: recordings made from reset, from 7 ms and after an earlier input each replay identically and pass as scenarios; `test_host` checks SHA-256 against FIPS 180-4's vectors; event traces (`--trace-events`) and analog observations are not implemented |
| Coverage, profile, stacks and faults | Every core watched instruction by instruction when a machine carries analyses (SPEC-009 §12; extracted from EmbCC's analyses): coverage per source line and function from the DWARF line tables and the ELF's functions and code labels; the profile by call path on a shadow stack per core, from each core's calls and returns and the exception entries and returns every core reports (Cortex-M, AVR, RISC-V, MIPS with its delay slots, PIC24; the RP2040's two cores rooted apart); `embsim run --coverage` (gcov-style text with the source, lcov, JSON) and `--profile` (text, collapsed stacks, JSON); the stacks' depths (`--stack`, `analysis.stack`: each core's main and process stacks against the link's `__stack_limit` or the end of the data, each function's own frame and its depth with its callees, `mcu.stack_overflow` when the main stack goes past its limit); the fault report (`--faults`, `analysis.faults`, `mcu.fault`: a Cortex-M's faults with HFSR and CFSR decoded, RISC-V and MIPS exceptions, PIC24 traps, the instruction, the registers, a backtrace from `.debug_frame` or the shadow stack, the handler); `analysis.start`, `.stop`, `.coverage`, `.profile`, `.stack`, `.faults`; the run report's summaries | `analysis` (`tests/protocol/test_analysis.py`): all nine firmware examples measured twice in two processes alike, the profile's functions adding up to the instructions and cycles, lcov agreeing with the JSON line for line, the collapsed stacks adding up to the cycles, every board's stack within its limit and main's depth at least its frame, a recursion past `__stack_limit` (`tests/firmware/stack-overflow`) raising `mcu.stack_overflow` once in the right function with the depths adding up, a load from nothing (`tests/firmware/fault`) raising `mcu.fault` once as a HardFault (FORCED, PRECISERR, BFAR) with the backtrace peek, read_sensor, main, Reset_Handler from `.debug_frame`, the protocol's methods; RISC-V and MIPS faults decoded by hand (a load access fault on the HiFive1, an address error on the PIC32MX, each with its `.debug_frame` backtrace); the PIC24's decoding is not exercised (no XC16 in CI); the Pico's two-core scenario runs in the same wall time with both on |
| Python client | `python/embsim` (`pip install ./python`, standard library only): `Session` over `embsim sidecar` (time, inputs, serial text, probes, scenarios, recordings and replay, coverage and profile, the debugger), `run()` and `replay()` over the command line with its exit codes, a pytest fixture | `python_client` (`python/tests/test_session.py`) against this build |
| An emulated target for test runners | `embsim run --target BOARD --image FILE` (a catalog board, no project), `--console`, `--until-text TEXT` (ends at the millisecond the text appears; exit 1 when it never does), `--image ID=FILE` over a project; `docs/user/ci.md` (the CI contract, GitHub Actions, GitLab CI, Python) | `run_target` (`tests/protocol/test_target.py`); EmbLinkRTOS's nine Arduino Uno conformance tests pass on EmbSIM's Uno in 4.4 s (its `avr-uno-embsim` preset and `tools/embsim/run_embsim.py`, EmbLinkOs/EmbLinkRTOS#12, whose `avr-embsim` CI job builds EmbSIM's `main` and passes them); its ten Cortex-M4F conformance tests pass on EmbSIM's NUCLEO-F446RE in 4 s (its `nucleo_f446re` board at 180 MHz, the `cm4-nucleo-embsim` preset and the `cm4-embsim` CI job); its twelve RV32IMC conformance tests pass on EmbSIM's ESP32-C3-DevKitM-1, each booted by Espressif's ROM from its flash image (its `esp32c3_devkitm1` board, the `rv32-c3-embsim` preset and the `rv32-embsim` CI job, EmbLinkOs/EmbLinkRTOS#20); `--target` plugs the board into a USB port (5 V on its USB_VBUS, VBUS or U5V) |
| Multi-board projects | several boards in one project, each MCU with its own firmware on the session's time, connected by nets | `multiboard` (`tests/protocol/test_multiboard.py`): an Uno and a Blue Pill exchange bytes over one serial line at their real baud rates and logic levels; two runs agree on every event |
| Debug | Breakpoints by address, symbol, or file and line; watchpoints; step, halt, continue; registers and memory read and write; Thumb disassembler; ELF symbols; DWARF 2 to 5 line tables (XC16's packed sections too); `debug.*` methods; GDB remote protocol server (`embsim gdb`) | `test_mcu` (source lines), `tests/protocol/test_gdb.py` (registers, memory, breakpoints, step, continue over RSP); LLDB debugged the example through `embsim gdb` in this workspace |
| Toolchains | Detection from the profiles' version commands, builds by template expansion (`embsim build`, `embsim toolchains`, `toolchain.detect`, `toolchain.build`) (SPEC-010 subset) | `tests/protocol/test_build.py`; the EmbCC and Clang profiles built the examples here; the gcc-arm profile (GCC 13.2) builds the Blue Pill, both Pico examples and the nRF52840 DK, gcc-avr (avr-gcc 7.3) the Uno, and gcc-riscv (Ubuntu's riscv64-unknown-elf-gcc 13.2 with picolibc, found under its alternative prefix) the HiFive1, each passing its scenario unchanged, in CI's linux-gcc job too; XC16 v2.10 (free mode, installed by hand) built `examples/explorer16-pic24`; the TinyCC, XC8 and XC32 profiles are not exercised (not installed), and CI has no XC16 |
| CLI | `run` (with `--record`, `--coverage`, `--profile`, `--target`/`--image`, `--until-text`, `--console`), `replay`, `record to-scenario`, `validate`, `serve` (with `--allow-origin`), `sidecar`, `catalog` (`list`, `validate`, `new`, `install`), `board`, `models`, `gdb`, `build`, `toolchains`, `version` | `tools/ci/run_examples.py`: twenty examples validate and pass their scenarios (40 steps): counter-bus, button-led, button-transistor-led, rc-filter, bluepill-button-led with its firmware (the LED driven through an NPN transistor as 01 §8 describes), display-showcase 6/6, kicad-flasher, temperature-alarm, temperature-divider, light-controlled-switch, optocoupled-input, transistor-relay, uno-blink-serial 7/7, hifive1-blink 7/7, pico-blink-serial 7/7, pico-dual-core 9/9, nrf52840dk-blink 7/7, explorer16-pic32mx 8/8, explorer16-pic24 8/8, two-boards-uart 8/8 |
| Catalog | 181 components in 20 categories with symbols, artwork and 480 component fixtures, named manufacturer parts among them (passives, sources, semiconductors, 74HC and CD4000 logic, the 555, regulators, displays: 7-segment, the TM1637 4-digit module, HD44780 LCDs (16x2, 20x4), SSD1306 OLED, MAX7219, WS2812B LEDs and strips; I2C and SPI devices: the PCF8574 expander, 24C02, 24LC256 and 25LC040 EEPROMs, DS1307 and DS3231 clocks, sensors, the DS18B20 on 1-Wire, the BME280 and BMP280, the MPU-6050, the HX711 with a load cell; the NEO-6M GPS; the TSOP38238 IR receiver with an NEC remote; the MFRC522 RFID reader with a MIFARE Classic card; breadboards of 830, 400 and 170 points whose strips and rails are `ties`, with the bench's place for every leg (`layout.bench`, SPEC-007 §2); actuators: servo, the 28BYJ-48 stepper motor with the ULN2003A and its driver board, a stepper driver, buzzer); nine MCUs with their register descriptions and packages (STM32F103C8 LQFP48, STM32F446RE LQFP64, ATmega328P DIP-28, FE310-G002 QFN48, RP2040 QFN56, nRF52840 aQFN73, PIC32MX795F512L TQFP100, PIC24FJ1024GB610 TQFP100, ESP32-C3 QFN32 and in the ESP32-C3-MINI-1); nine boards with artwork and power fixtures (Blue Pill, NUCLEO-F446RE, Arduino Uno R3, HiFive1 Rev B, Raspberry Pi Pico, nRF52840 DK, Explorer 16/32 with the PIC32MX795F512L PIM and with the PIC24FJ1024GB610 PIM, ESP32-C3-DevKitM-1); eight toolchain profiles; JSON Schemas for every kind; the Python validator with semantic checks | `embsim catalog validate catalog`: 190 ok, 0 broken, 0 invalid, 503 fixtures run (`catalog_fixtures`); the HD44780 LCD against the LiquidCrystal library, its `HelloWorld` run unmodified (4-bit at the controller's own timing, nothing shown until the contrast pot is turned) and its fixtures for the busy controller ignoring a write and a floating R/W reading; the PCF8574 and a 20x4 LCD against LiquidCrystal_I2C, its `HelloWorld` run unmodified through a backpack wired as the common board is (address 0x27 from the pulled-up A0..A2, the backlight switched by P3) and the expander's fixtures for the address pins, the interrupt and a low supply; the DS18B20 against OneWire and DallasTemperature, the `Simple` example run unmodified (the search algorithm, MATCH ROM and the scratchpad's CRC, CONVERT T polled to its 750 ms end) and its fixtures for presence, conversion, 9-bit resolution and the supply (own, parasite, too low); the BME280 against Adafruit's library, its `bme280test` run unmodified (chip ID, soft reset, the trimming parameters, normal mode at x16, readings compensated by the library's own formulas to the room's values, the altitude from them) and its fixtures for forced and normal mode, the address pin, ctrl_hum latching, SPI and a low supply; the BMP280 on the same model against Adafruit's BMP280 library, its `bmp280test` run unmodified (chip ID 0x58 at 0x77, normal mode at x2/x16 with the x16 filter) and its fixtures for a forced measurement, the absent humidity and the 2 s standby; the HX711 with a 5 kg load cell against the HX711 library, its `HX711_full_example` run unmodified (raw readings, tare, the scale factor, power-down and up) and its fixtures for settling, a read, its end, power-down, channel B, saturation, noise and a low supply; the MPU-6050 against Adafruit's library, its `basic_readings` run unmodified (WHO_AM_I, DEVICE_RESET polled, the ranges and filter set, a tilt and a turn read back in m/s^2 and rad/s) and its fixtures for sleep at power-on, wake, saturation, the gyro scale, the DLPF delay, the AD0 address, the 8 kHz data-ready interrupt, temperature and a low supply; the WS2812B against Adafruit NeoPixel, its `simple` example run unmodified on a 16-LED ring (the library's bit-banged 800 kHz frames decoded and latched) and its fixtures for a colour, the 0.6 us threshold, a chain passing bits on, the V5's 280 us latch and a low supply; the DS3231 against RTClib, its `ds3231` example run unmodified (OSF at power-up, the clock set from the build time and counting, the temperature register) and its fixtures for power-on, midnight, the countdown-chain reset, both alarms, the square wave, the 32 kHz output, temperature and a low supply; the 4x4 membrane keypad (sixteen switches with the membrane's contact resistance and bounce) against the Keypad library, its `HelloKeypad` run unmodified, and its fixtures for a press and the bounce; the NEO-6M against TinyGPSPlus, its `DeviceExample` run unmodified over SoftwareSerial (the fix, the date and time, a move, the coordinates as the Uno's floats print them) and its fixtures for the sentences without a fix, a hot fix's RMC and GGA, the cold start's 27 s, the time pulse and a low supply; the SSD1306 against Adafruit's library, its `ssd1306_128x64_i2c` run unmodified (the splash frame matching one rendered independently from the library's bitmap, then the single pixel) and its fixtures for the orientation with and without remap, the charge pump, the 100 ms power-up and a timed scroll; the MFRC522 with a MIFARE Classic 1K card against the MFRC522 library, its `DumpInfo` run unmodified (REQA, anticollision, SELECT, authentication of every sector and the whole card read) and its fixtures for the antenna, a REQA, the antenna off and a low supply; the KY-040 rotary encoder against PJRC's Encoder, its `Basic` example run unmodified (four counts a detent, the bounce visible in the polled count) and its fixtures for two detents, the direction, the bounce, floating without + and the press; the 28BYJ-48 stepper through its ULN2003 board against the Arduino Stepper library, its `stepper_oneRevolution` run unmodified (200 steps a way at 60 rpm turning the geared shaft 34.9 degrees, as on the bench) and its fixtures for full steps, the pull-in rate, a stall beyond it, a ramp past it, the wrong lead order, holding and overload, bipolar drive and the ULN2003's saturation voltage; the TM1637 4-digit display against TM1637Display, its `TM1637Test` run unmodified through every test (digits at each position, numbers with and without leading zeros, hexadecimal, the colon, the seven pulse widths, off and on, DONE) and its fixtures for the digits, power-on, the colon and the points, fixed addressing, the address limit, the ACK, a STOP mid-byte and the key read; the TSOP38238 IR receiver with the kit's NEC remote against IRremote, its `SimpleReceiver` run unmodified (frames and repeat codes decoded by the library's Timer2 sampling) and its fixtures for the leader, a frame, repeats, the bit timing and a low supply; the HC-SR04 against NewPing, its `NewPingExample` run unmodified (the echo from the speed of sound at the air's temperature, the range limits) and its fixtures at 20 and 0 C; the servo against the Arduino Servo library, its `Sweep` and `Knob` examples run unmodified (the horn at the rated speed, the dead band, the library's first pulse after a full Timer1 wrap) and its `travel` fixture; the DHT22 against Adafruit's DHT library, its `DHTtester` example run unmodified (`tests/vendor/arduino`: every read decoded, the previous measurement sent, the element's 10 s lag on a step from 23.4 to -5.3 C) and its fixtures for the datasheet's start-up second, previous data and response time; `catalog_fixture_assertions` runs the component fixtures through the CLI; `tools/validate.py --all`: 690 files, 0 errors; the tools' unit tests: 137 tests; the STM32F103C8's register description written from RM0008 (ADR-020), the RP2040's and the nRF52840's converted from their vendors' BSD-3-Clause SVD files, the PIC32MX795F512L's and the PIC24FJ1024GB610's from Microchip's Apache-2.0 device packs |
| The world (ADR-026) | Fields (temperature, humidity, pressure, illuminance, sound, wind) declared by the project or set by inputs, read by every sensor input bound to them (`inputs[].world`) while the input is not set, the input's own default when the field is not declared; boxes and spheres on the bench met by a distance sensor's beam (`bench.sense`) from its place in `layout.bench`; world inputs (`world`, `world.BODY`) scheduled, recorded and replayed; `set_world` without a restart; `world.get`; reset restores the project's room; the World panel and bodies on the 3D bench with the sensors' beams | `tests/protocol/test_world.py` (an LDR divider following the room's light, a part's own value overriding it, bad world inputs refused, bodies added and removed, `set_world` applied without a restart and saved, reset restoring the room); NewPing's `NewPingExample` 6/6 measuring a wall moved to 30, 250 and 1 cm and the room at 0 C; Adafruit's `DHTtester` 3/3 reading the room's 23.4 C and 61.2 %RH and its drop to -5.3 C; `e2e/world.spec.ts`; every fixture and example unchanged; fields as functions of position, moving bodies, physics and motion for IMUs not implemented |
| The air, a Wi-Fi access point, the internet, phones and sniffers (the ESP32-C3 radios plan, steps 4, 6 and 7, under way) | The 2.4 GHz medium (`core/engine/air.h`): frames with their channel, power, rate and airtime (802.11 DSSS and OFDM, BLE), delivered when they end to every radio tuned to their channel at the free-space path loss over the bench distance (`layout.bench`; a board's parts where the board is) above -97 dBm, ACKs for unicast frames; `emblink/wifi_ap`: beacons with WPA2-Personal's RSN element or open, the 4-way handshake and CCMP, probe responses, open authentication, association with AIDs, deauthentication; data frames with LLC/SNAP, power-save buffering (TIM, PS-Poll, More Data, group frames after the DTIM); the router behind it answering ARP and pings and leasing addresses by DHCP; relaying between its stations; its NAT to the internet beyond and its DNS forwarder; the internet beyond the routers (`core/engine/inet.h`: IPv4 packets between the routers' WAN sides and the hosts out there, each link's latency, names resolved); `emblink/web_server`, a web server there (TCP as a server with its handshake, MSS, window, retransmission and FIN; HTTP/1.x GET and HEAD, 404 and 501; ICMP echo); the stations and a log in its outputs; `emblink/ble_scanner`, a phone's scanner app: active scanning of channels 37, 38 and 39 in turn (SCAN_REQs with the specification's backoff), each advertiser's address, PDU type, name (from its advertising or scan response) and RSSI, an iBeacon's UUID, major, minor and power; `emblink/ble_central`, a GATT browser: a CONNECT_IND to its target, the connection as its master (channel selection #1, acknowledgement, L2CAP fragments, the supervision timeout, a connection that failed to be established tried again), GATT discovery and reads; overlapping BLE packets on a channel colliding unless one is 21 dB above; BLE packets taken from their access address on; `emblink/wifi_scanner`, a phone's Wi-Fi analyzer: channels 1 to 13 scanned as a phone does (a probe request on each, or passively), each network's SSID, BSSID, channel, security (Open, WEP, WPA, WPA2, WPA3, -Enterprise) and RSSI; `emblink/air_sniffer`, a Wi-Fi adapter in monitor mode on one channel or an nRF-Sniffer-style dongle on one advertising channel: every frame it hears with its FCS, or the access address, PDU and CRC-24, its time, channel and RSSI, the snapshot length; the workbench's Air panel (Run, the Air tab): the phones' lists with signal bars, the sniffer's frames as Wireshark's packet list names them (802.11 management frames by SSID, EAPOL's four messages, ARP, IPv4 with its ports, BLE PDUs with their addresses and names), a frame's bytes, saved as pcap (radiotap, LINKTYPE_BLUETOOTH_LE_LL_WITH_PHDR); the supply current parts draw (ADR-036): a part's model sets it, its power pins fed by the circuit sink it to its ground; the ESP32-C3's from its datasheet (the core running and waiting at its clock over 1 ms windows, held off, the radio listening and sending by modulation); `emblink/usb_power_meter`, an inline USB tester (its insertion resistance, its own draw from the host's side, readings averaged over its refresh at its resolution, mAh and mWh) | `test_engine` (airtimes, channels, path loss, delivery and ACK timing, range, reset; the access point's beacon, probe response, authentication, association and deauthentication, ARP, DHCP's offer, ACK and NAK, a ping, a frame kept for a dozing station, its TIM and PS-Poll, against a scripted station); `emblink/wifi_ap`'s two fixtures (the beacon interval); ESP-IDF's `wifi/scan` 19/19 with two access points 3 m and 12 m from the board; its `station` set for an open network 13/13 and as it ships, WPA2-PSK, 13/13: joined, keyed and leased 192.168.1.100; `test_engine`'s WPA2 test plays a supplicant through the 4-way handshake and CCMP; `test_wcrypto` (the standards' vectors). The scanner: `test_engine` (channels in turn, the window, two advertisers, an iBeacon decoded, active scanning's SCAN_REQ, the SCAN_RSP's name and the backoff), its fixture; the air's BLE collisions and capture (`test_engine`); NimBLE's `bleprph` listed 2 m away at -37 dBm. The browser: `test_engine` against a scripted peripheral (the CONNECT_IND's fields, the channels and timing, the discovery, the supervision timeout), its fixture; `bleprph` connected, its four services and their characteristics discovered and read (14 assertions). The internet: `test_engine` (a drop, a ping's round trip, names, the web server's handshake, a request in two segments, 200, FIN, RST, 404, retransmission), `web_server`'s fixture, `http_request` and `BasicHttpClient`. The analyzer and the sniffer: `test_engine` (a scan of two access points listed by RSSI with their security, passive scanning, the BLE access address and CRC-24, the 802.11 FCS, the snapshot length), five fixtures; the pcap files read by scapy, every FCS and the CRC-24 its own computation's; `air.test.ts`; `e2e/air.spec.ts`; The supply current: `test_analog`'s `test_supply_current` (a part's current through a supply's resistance, none when its own pin is the supply, a supply that cannot carry it), the meter's four fixtures, `hello_world` reading 16.7 mA and `http_request` 102 mA while scanning through the meter; Wi-Fi's power save: `test_esp32c3`'s `test_wifi_tsf` (the TSF loaded, the TBTT's interrupt ahead of each beacon, a TSF timer, source 2, the radio deaf with the common clock off), `http_request` in power save at 21.9 mA, keeping its access point. `http_request`'s association, 4-way handshake and protected data captured on channel 6, `bleprph`'s and `ble_ibeacon`'s advertising, the phone's SCAN_REQ and the SCAN_RSP on 37 (docs/gallery). Not yet: the host's own network, TLS, 802.11n, WPA3, BLE's 2M and Coded PHYs, BLE encryption, retransmissions, Wi-Fi contention and collisions, fading, the sniffer following a connection, light sleep, supply tables for the other MCUs and parts |
| A board's serial port on the host (ADR-031, ADR-032) | `port.open`/`close`/`list`, `port.state`, `embsim serve --serial`, the Serial panel's "Open on this computer": a board's console bridge (`console.bridge`) or a terminal part as a pseudo-terminal on Linux and macOS; the host program's rate on the line, a mismatch decoding as garbage both ways (`esk_uart_rebaud`); opening the port resets the Uno through its DTR capacitor (the reset switch held 0.7 ms, MCUSR's EXTRF set, other flags kept); bytes dropped while no program has the port; the host's bytes recorded inputs (`{"base64"}` for binary) | `tests/protocol/test_host_serial.py` (tests/firmware/uno-echo: the power-on banner lost, `boot 02` on opening, the upper-cased echo at 9600, garbage at 115200, no reset on closing, a reset on reopening, the reset and bytes recorded); `test_session`'s `test_uart_rebaud`; `SerialTerminal.test.tsx`; Windows (com0com, Q16), the bridge's frames on PD0, modem lines and Optiboot not implemented |
| Boards qualified against reference data (visual benchmark V1) | A board's `reference.json` (kind `board_reference`, SPEC-006 §3.1): outline, mounting holes, every terminal's pad and connector bodies from sources independent of its artwork, checked by `tools/validate.py` within its tolerance, every port required to have a position; the Uno R3's from Arduino's A000066 datasheet and KiCad's `Arduino_UNO_R3` footprint and symbol, its artwork redrawn from it (the headers had been up to 4 mm out); the ESP32-C3-DevKitM-1's from Espressif's V1.0 dimension drawing, read from the PDF's vector paths (its 30 header pads, outline and micro-USB body; the pads had been 0.39 mm high, the RGB LED 1.1 mm and the USB 0.64 mm out), the check reading oblong (rect) pads as well as round ones | `tools/test_validate.py`'s `test_uno_qualified_against_its_reference` (the Uno passes; D8 0.5 mm off and the earlier USB-B place are refused); `test_devkitm1_qualified_against_its_reference` (the DevKitM-1 passes; its earlier J1 pin 1 and micro-USB places are refused); the BB830's body, 630-point area and continuous rails cite BusBoard's datasheet; the Uno's DIP socket, resonator, reset button and LEDs, and the breadboard's hole offsets from its edges, unqualified and declared; the NUCLEO-F446RE has no reference yet (UM1724's drawing is an image) |
| The Uno's USB power path: a resettable PTC fuse and the VIN switch (ADR-031, ADR-034) | `ptc_fuse` (SPEC-004): a two-node thermal model heated by the power it passes, its resistance rising gently below the polymer's switch and steeply above it, its temperatures integrated on their own clock (component wakes) with the island touched on a 1 % change; `emblink/mf_msmf050` fitted to Bourns' typical time-to-trip curve, its room the world's temperature | the part's 11 fixtures on a current-limited 15 V supply: holds 0.5 A for 30 minutes, trips at 1.0 A, at 8 A within 0.15 s (not after 5 ms), at 2.4 A between 0.4 and 2 s, 0.6 to 1.0 W tripped, trips at 0.6 A at 85 C and holds 0.75 A at -40 C (the derating table, not fitted), resets once the fault is removed; the post-trip resistance, part spread and ratings' damage not modelled. On the Uno: USB_VBUS through F1 and T1 (`emblink/fdn340p`, a level-1 P-MOSFET with its body diode) to +5V, U5A (`emblink/lmv358`) turning T1 off when VIN/2 is above 3.3 V | the board's fixtures from a 5 V host through a 0.25 ohm cable: 5.0 V unloaded; 4.64 V at 600 mA (the cable, F1 and T1), held for 10 minutes; a 2 ohm load sags the rail to 3.8 V, F1 trips in about 16 s and the rail falls under 0.6 V with F1 passing 0.8 W; 9 V on VIN turns T1 off and USB carries nothing; the FDN340P's on-resistance, threshold and body diode and the LMV358's comparator, follower and rail fixtures |
| Op-amp dominant pole and slew rate (SPEC-004 §7) | `opamp` with `gbw`: an internal node integrating the input stage's drive (single pole at gbw/gain, slewing at `slew`, gated at the output limits so it recovers from saturation at once), the output and that node depending on the supply pins; Newton limiting on tanh's argument for the static characteristic; the LM358 (0.7 MHz, 0.3 V/us) and LMV358 (1 MHz, 1 V/us) | each part's `slew` fixture (a follower given a 2 V step) and `bandwidth` fixture (a gain of 11 given a 10 mV step settling with its 11 / (2 pi GBW) time constant); further poles, phase margin, input offset, common-mode range and current limits not modelled |
| Workbench | TypeScript workspace: typed protocol client and mock session, design system with light and dark themes; schematic editing against the engine through project transactions (place, wire, move, rotate, mirror, delete, copy, paste, duplicate, net labels and renames, buses with their live value, undo and redo, save to the project file); boards drawn as blocks of their ports, and the inside of a board or composite part opened on a read-only, live sheet; the 3D bench (ADR-027, Three.js), the Design canvas's second view and the Run perspective's Board panel: boards with their PCB features, breadboards and parts built from their packages on a desk, parts dragged into a breadboard's holes and plugged in, jumpers between holes and pins, unbuilt connections as dashed links built in one click, notes, live LEDs and controls, every edit a project transaction shared with the schematic (which hides breadboards), a keyboard list of every bench action, and the flat 2D bench (artworks with live LEDs and buttons, nets as wires between their pins) without WebGL; the clock's real-time rate; oscilloscope, logic analyzer with UART, SPI and I2C decoders, serial terminal, meters with units; board view artwork states with value maps, formats and text (SPEC-005 §8); the Debug perspective (source, disassembly, registers, breakpoints, memory, peripherals, console); the Verify perspective (a scenario's inputs and assertions edited in place, run with each assertion's result in its row and saved into the project; recordings replayed with the divergence shown or turned into a scenario; coverage with each file's source and counts and the functions by cycles, lcov and collapsed stacks to download); command palette; platform adapter; Tauri 2 desktop shell with the engine as sidecar and its icon | `pnpm typecheck`, `lint`, `test` (202 tests incl. axe, the decoders, artwork states and their scoping, the bench's wires, the bench model's plugging, unplugging, jumpers and unbuilt links, every catalog part's bench shape, resistor colour codes), `build` pass; Playwright smoke (open, run, press, observe, export; keyboard place and wire; theme) and the bench (a breadboard added, a resistor plugged into a10 and a14, a jumper to Q1's base and its undo) pass in Chromium; `examples/uno-breadboard` passes uno-blink-serial's scenario built on a breadboard (7/7); editing, copy and paste, and save checked in Chromium against `embsim serve`; the Blue Pill's boot banner decoded from USART1 frame by frame; `tools/perf/large_project.py` 500 inverters with 1,499 wires and 1,000 labels measured by `scripts/perf.mjs` in headless software-rendered Chromium: pan and part drag 16.8 ms at the 95th percentile, zoom 33 ms; usable at 1024x700. The desktop shell is built and run here on Linux (WebKitGTK 4.1 under Xvfb) with the Blue Pill example loaded, and its deb is built by `tauri build`; macOS and Windows installers come from CI |

Observed on 2026-10-16 in this workspace: Ubuntu 24.04, GCC 13.3 and Clang 18.1, CMake 3.28, Python 3.13, Node 22. `ctest` on the GCC Debug build: 32 of 32 targets pass (test_analog, test_avr, test_catalog, test_engine, test_host, test_json, test_lua, test_mcu, test_mips, test_nrf52, test_pic24, test_pic32, test_riscv, test_rp2040, test_sched, test_session, catalog_fixtures, tools_unittests, catalog_sdk, importers, docs_integrity, banned_constructs, catalog_fixture_assertions, protocol_conformance, gdb_server, firmware_build, multiboard, examples_headless, record_replay, analysis, run_target, python_client); the same pass on the Clang, TinyCC and AddressSanitizer builds. The CI matrix (Linux with GCC, Clang and TinyCC; macOS with Clang; Windows with MSVC; the workbench; tools and catalog; documentation; repository policy) was observed green on the M0 pull request and on the M1 pull request (EmbLinkOs/EmbSIM#2, head 9bc3d42, 2026-10-16) the M2 pull request (EmbLinkOs/EmbSIM#3, all 17 checks) the M3 pull request (EmbLinkOs/EmbSIM#4, all 17 checks) and the M4 part 1 pull request (EmbLinkOs/EmbSIM#5, all 17 checks, after its MSVC fixes), which ran the Blue Pill golden fixture, the GDB server and the firmware build on all three platforms.

## First named-part catalog batch (component work)

After integration with M3, the catalog contains 128 components, including 26 manufacturer/package entries
and a generic open-collector comparator channel. The named batch adds discrete
semiconductors, four regulators, full LM358P/LM393P packages, NE555P, LM35DZ,
TMP36GT9Z and six full 74HC packages. `part` metadata is searchable and shown
in the inspector; all named parts have original symbols and package illustrations.
See [the catalog guide](../catalog/README.md) for the inventory, exact supported
behaviors and source-review limits. Manufacturer access was blocked during the
initial batch; the follow-up source review below records the recovered evidence.
No manufacturer qualification of the electrical models is claimed.
This batch supplements the merged M3 implementation.

Evidence from the component-work branch on Linux, GCC 14.2, Python 3.12 and Node 24:

- `ctest --preset dev --output-on-failure --no-tests=error`: 20/20 target outcomes
  successful. `firmware_build` explicitly skips without a cross compiler; all
  other targets execute. The new native catalog test has 23 checks.
- `catalog_fixtures` instantiates all 129 catalog items (components plus board);
  `catalog_fixture_assertions` executes all 251 component fixtures through the CLI, including
  all old fixtures, not only schema validation. It checks nominal outputs, all HC
  channels and delays, loaded electrical rails, comparator pull-ups, sensor endpoints,
  555 retained state/reset/divider and RC oscillation, and solver health.
- The combined Address/undefined-behavior sanitizer build passes all 20 CTest
  outcomes, including Lua, catalog instantiation, fixture assertions and examples.
  The firmware-build wrapper explicitly skips without a cross compiler.
- Python tools: 119 tests. `tools/validate.py --all`: 422 files, no errors/warnings.
- Workbench typecheck, lint, production build and 75 existing tests pass.
  A real-engine Chromium check verifies named-part search, manufacturer/package
  labels, the inspector's datasheet link, and the Run view without page errors.
- `examples/temperature-alarm` passes six assertions through the CLI. Its LM35DZ,
  LM393P and 2N3904 circuit lights and extinguishes an LED as temperature changes.

macOS, Windows and TinyCC have not been rerun locally for this batch. Existing
CI defines those checks; remote results must be observed before claiming them.
The earlier workbench Debug and hold-state findings are intentionally outside
this component batch.

## Manufacturer source review after the M4 merge

Based on main `22deb05`, including both the named-part and M4 merges. Reviewed
25 named entries against 23 manufacturer PDFs; the three L78 entries share a
source. Each entry now records revision, SHA-256, review date and page references.
[The source record](engineering/named-part-sources.md) distinguishes reviewed
package facts from model qualification. TMP36GT9Z's TO-92 map is confirmed from
Figure 4 and its exact ordering code from the ordering guide. The onsemi 2N7000
link and the Infineon redirect are replaced by working manufacturer PDF URLs.
Nexperia BAT54 remains reference-only: the document URL is unavailable and the
manufacturer CDN denies alternative document requests.

The five new electrical fixtures check LM35DZ at 4 and 30 V, TMP36GT9Z at 2.7
and 5.5 V, and a TMP36 with its ground at 1.2 V. The latter checks both absolute
net voltage and the sensor's output observation relative to its own ground.
Nominal transfer and pin identity are covered; electrical accuracy, thermal
behavior and operating corners remain unqualified. The existing pin-map
regression now checks every terminal of all 25 source-reviewed entries.

Combined integration evidence on Linux, GCC 14.2 and Python 3.12:

- All 23 development CTest outcomes pass after the two merges; the firmware
  build wrapper reports its missing cross compiler explicitly.
- Current engine catalog validation: 131 valid items (128 components, three
  boards), no broken or invalid entries, 259 fixtures including the boards.
- The independent fixture runner passes 256 component fixtures and 782
  assertions; all 119 Python tools tests pass.
- Schema/semantic validation: 443 files, no errors or warnings. Documentation
  integrity: 89 documents, 92 requirements, no problems.
- Both merged PRs ([#5](https://github.com/EmbLinkOs/EmbSIM/pull/5) and
  [#6](https://github.com/EmbLinkOs/EmbSIM/pull/6)) were observed with all 17
  checks passing, including macOS/Windows core, desktop and browser smoke jobs.
  This is evidence for the merged baseline; the source-review branch needs its
  own CI results.
- No UI or native engine code is changed by this source-review follow-up.

## Interactive library and circuit examples

Based on main `5cc2762` after PR #8. The [interactive library record](engineering/interactive-library.md)
lists 18 new named entries, a reusable relay contact controller, the manufacturer
source status and all electrical approximation boundaries. The catalog totals
157 components, including 44 named parts; 38 have a reviewed manufacturer source,
while six retain explicitly unreviewed references. No vendor qualification is implied.

The explorer adds grouped categories/counts, multiword search, manufacturer and
package filters, model-tier and named-part filters, and Overview/Pins/Models
previews. Unsupported policy/broken entries cannot be placed or dragged. New
artwork is original and supports live input hotspots and readable decimal values.
Six native electrical primitives cover environmental resistance, photodiodes,
optocouplers, relay contact timing, SPDT switches and four-pad tactile buttons.
The B3F-1000 fixtures include permanent pad pairs and unused duplicate pads.

Four wired examples cover a temperature divider/adjustable alarm, a light-controlled
transistor switch, an optocoupled input with offset grounds, and a transistor relay
driver with an R/L coil and external flyback diode. Each has a CLI scenario and
GUI controls. The real-engine browser test caught a restart/subscription defect;
structural edits now reload runtime bindings and restore waveforms.

Observed locally on Linux, GCC 14.2, Python 3.12 and Node 24:

- All 23 development and 23 Address/undefined-behavior sanitizer CTest outcomes
  pass. The firmware-build wrapper explicitly skips compilation without a cross
  compiler; other targets execute, including the M4 multiboard examples.
- Engine catalog validation: 150 valid items, no broken/invalid entries and 311
  fixtures. The independent runner passes 308 component fixtures / 897 assertions.
- All 15 example projects validate and their scenarios pass (30 CLI invocations).
- All 522 JSON files validate; 119 Python tool tests pass. Documentation integrity
  checks 94 documents and 94 requirements with no problems.
- Workbench typecheck, lint, build and 82 UI tests pass, including accessibility
  checks, combined library filtering and numeric artwork formatting.
- `tests/browser/test_library.py` passes in Chromium against the actual engine:
  manufacturer/package search, category browsing, preview sections, placement,
  light/dark themes, live press/release, refreshed input controls and waveform
  samples that cross the expected high/low limits after the placement restart.

The new branch still needs its own remote macOS/Windows/TinyCC and desktop checks.
Local evidence must not be confused with the green checks recorded for older merges.

## Specified but unimplemented

The PIC32MX's ADC, SPI, I2C, DMA, USB,
Ethernet, CAN and watchdog; the PIC24FJ1024GB610's ADC, SPI, I2C, capture
and compare modules, EPMP, RTCC, DMA, USB, watchdog and flash programming,
and a dsPIC33 part; and the PIC32MZ's FPU, DSP ASE and microMIPS; the
RP2040's PIO, DMA, SPI and I2C slave modes and USB;
the nRF52840's radio, PPI, SPIS and TWIS; the RISC-V F and D
extensions; on the STM32F1 DMA, SPI and I2C slave modes, the ADC's
injected group and the timers' input capture, on the ATmega328P SPI and TWI slave modes, on the FE310 the PMU, QSPI0's programmed mode and I2C (their
registers hold values and raise `mcu.unsupported_access`); DWARF variables (`debug.variables`); the GDB
server over the session protocol (`debug.gdb`); a debugger's choice of core
on a part with two (it sees the core that runs next); full event traces and analog observations in recordings (SPEC-012 §1, §5),
native models (SPEC-013 beyond §2), the
per-project `lua_budget` setting (the budget is fixed), ZIP64 and
compressed formats other than deflate for library archives, the
`catalog.validate` protocol method (the CLI has it), binary waveform frames
(samples are JSON arrays with `encoding: "json"`), a fault report for the AVR (it has no faults to decode) and MIPS16e
instructions disassembled in it, editing inside a board or
composite (the inside is read-only), signed installers and an update channel,
importers for Eagle, Altium and EasyEDA.

## Validation recorded in this workspace

Recorded as it happens: the checks listed above, on the dates given.

## Known limits and unresolved evidence

- Fast digital traffic on an analog net is costly under the electrical
  policy: on the Uno, D13 (SCK) carries the L LED and its resistor, so
  every 4 MHz SPI clock edge is an analog step (about 25 times slower
  than the functional policy for a polling SPI sketch). Sketches that need
  no analog behaviour run their examples under the functional policy
  (rfid `DumpInfo`); the paced real-time work (ADR-032) will take it up.
- An MCU sees an input that comes from an analog comparator crossing at the
  start of the quantum after the crossing, at most `core_quantum_ps` (1 µs
  by default) late (ADR-019); digital inputs are exact, and so is a
  device's answer to the core's own edge on an analog net (the next quantum
  waits out the edge's ramp).
- Five USART timing deltas from EmbCC's Blue Pill fixture are documented in
  `tests/firmware/bluepill-golden/deltas.json` (frames on the pin take
  their full duration; EmbCC's are instantaneous).
- `esk_host_run` on Windows (CreateProcess) is exercised only by CI
  (`firmware_build` passed on the M1 run).
- The `main` branch is protected by the repository's ruleset; pull
  requests run the CI matrix.
- Instruction timing of every planned core is an estimate except AVR
  (ADR-015).
- A supply symbol (`emblink/vcc`) on a net that a component model drives (a
  regulator's output) is not added as a source (`analog.supply_driven`):
  feed a board's regulated rail from a voltage source, as the Uno and
  HiFive1 examples do.
- The Debug perspective's register view lists registers numbered below 64,
  so a RISC-V core's CSRs are not shown there (GDB reads them); its memory
  view starts at 0x20000000 whatever the core.
- An FE310 starts at 0x20010000, where the HiFive1's bootloader jumps: the
  mask ROM and the bootloader are not simulated.
- The RP2040's bootrom is a stand-in (`core/mcu/rp2040/boot.c`): it boots
  boot2 from flash as the bootrom does and the host does its documented
  functions (ADR-021), but the BOOTSEL button has no effect and the XIP
  cache is not modelled.

## Updating this page

Record source revision, configuration, tool versions, runner results, cases,
skips and artifacts together. Move a feature to implemented only after
inspecting the code; call a requirement verified only after its named
evidence passes.
