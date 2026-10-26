# Proteus schematic and interactive simulation survey

Research date: **2026-10-26**. This is a document-based capability survey and an
EmbSIM gap assessment, not a hands-on benchmark or an implementation announcement.

**Finding:** Proteus combines schematic capture, visible component behavior,
on-sheet controls, mixed-signal simulation, measurement and firmware debugging in
one working diagram. EmbSIM's next schematic milestone should connect those same
workflows. Live LED colors are an immediate, visible gap, with much of the required
model data already available in EmbSIM.

## Scope and evidence

Reviewed Labcenter's public capture, wiring, simulation, peripheral, instrument,
analysis, library, reporting and release documentation, plus its downloadable
212-page tutorial and VSM brochure. Sources were retrieved with normal HTTPS
verification. No paid Proteus installation was run; responsiveness, numerical
accuracy, detailed menu coverage and device-by-device behavior were not tested.

Current product/release pages describe **Proteus 9 / 9.2**. The vendor-hosted file
`Tutorials_p9.pdf` still contains Proteus 8 terminology and screenshots. Tutorial
references below establish documented workflows, not an assertion that every
shortcut or dialog remains identical in 9.2. PDF page numbers mean the physical
PDF pages, counting its first page as 1.

Product boundaries matter:

- **Capture** supplies drawing, connectivity, hierarchy and engineering data.
- **VSM** supplies supported MCU/peripheral co-simulation and interactive models.
- **Virtual instruments** supply observation, stimulus and analysis interfaces.
- **Advanced Simulation** is an additional module for the graph analysis families
  listed below; those features should not all be attributed to basic capture.
- **PCB, BOM, managed libraries, reporting and AI** are related suite workflows.
  Their availability depends on the product/package; this survey is not a price
  or license-entitlement audit.

The inventory covers the documented functional families, not every menu command,
supported component or processor variant. “Not verified” does not mean absent.
The inventory contains **83 feature entries across eight groups**, backed by
20 vendor references. Each source ID links to the source register.

## 1. Drawing, navigation and editing

| ID | Documented capability | Why it matters | Evidence |
|---|---|---|---|
| C01 | Project wizard, schematic templates, sheet dimensions and title/logo/header presets | Consistent engineering drawings from project creation | [S1], [S12] pp. 1–5 |
| C02 | Overview navigator, zoom, pan, fit view and edge auto-pan | Navigate dense diagrams without losing context | [S12] pp. 5–9 |
| C03 | Configurable snap grid, paper/grid/highlight colors, line styles and fonts | Clear drawings and controlled drafting precision | [S1], [S12] pp. 7–9 |
| C04 | Part selection with name/keyword/property searching, symbol and footprint previews | Find an appropriate part before placing it | [S6], [S12] pp. 13–17 |
| C05 | Rotate during placement/movement, dragging and block copying | Fast repeated editing with less dialog work | [S12] pp. 17–19, 26–27 |
| C06 | Cursor/hover feedback for object selection and connectable pins; green pen while starting/ending wires | Make the available action apparent at the pointer | [S12] pp. 7, 19–20 |
| C07 | Editable references, values, property visibility and repositioned part labels | Keep values readable without crowding the symbol | [S12] pp. 24–25 |
| C08 | Automatic sequential annotation and global re-annotation with sheet starting numbers | Avoid duplicate references and organize large projects | [S12] pp. 24–25, 31 |
| C09 | Multi-element devices and gate changes inside a package | Represent dual op-amps and other multi-unit ICs coherently | [S12] pp. 35–36 |
| C10 | Footprint selection/change with previews and pin-to-pad packaging | Carry schematic identity into a manufacturable design | [S6], [S12] pp. 38–39, 68–71 |

## 2. Wires, nets, buses and power

| ID | Documented capability | Why it matters | Evidence |
|---|---|---|---|
| W01 | Modeless pin-to-pin wiring | Connect directly without a separate wire-tool round trip | [S2], [S12] p. 19 |
| W02 | Orthogonal “follow-me” wire routing | Keep wires square while following the pointer | [S2], [S12] p. 19 |
| W03 | Manual intermediate corners over automatic routing | Allow deliberate paths when the proposed route is unsuitable | [S2] |
| W04 | Wire dragging and routing repair when a component moves | Preserve connections through layout changes | [S2], [S12] pp. 19–20, 35 |
| W05 | Pin-to-wire branches with automatic junction dots | Make a branch connection explicit | [S12] p. 20 |
| W06 | Connectivity by touch, with documented differences between placing, dragging and pasting | Reduce repetitive wiring while keeping connection semantics defined | [S12] p. 20 |
| W07 | Auto-repeat of the last wire for aligned, repeated connections | Wire parallel connector/MCU rows efficiently; the tutorial documents alignment/consecutive-operation constraints | [S2], [S12] pp. 26–27 |
| W08 | Named terminals and explicit net labels | Connect distant circuitry without drawing every long wire | [S2], [S12] pp. 21, 41, 173 |
| W09 | Bus wires, bus terminals, bus pins and module ports | Represent address/data groups without hundreds of separate tracks | [S2] |
| W10 | Hidden power pins and power-rail net assignment/configuration | Manage supply connectivity and supply domains | [S12] pp. 22–24 |
| W11 | Global/local power-net behavior across sheets | Keep intended supply sharing explicit in multi-sheet designs | [S12] pp. 23–24 |
| W12 | NC terminals combined with a single-pin-net report | Distinguish deliberately unused pins from overlooked connections | [S2], [S10] |

The older tutorial says an arbitrary wire-to-wire start needs an explicit junction;
it does not establish unrestricted branch creation from every point on a track in
the current release. Also, wire autorouting is not evidence of a complete automatic
schematic-placement engine. PCB autorouting and PCB placement are separate topics.

## 3. Project scale, reuse and library authoring

| ID | Documented capability | Why it matters | Evidence |
|---|---|---|---|
| H01 | Multi-sheet schematics with naming, ordering and navigation | Separate functional blocks while keeping one design | [S2], [S12] pp. 30–31 |
| H02 | Nested hierarchical subcircuits and editable interface ports | Reuse functional blocks at different levels | [S2] |
| H03 | Net highlighting and cross-probing from component/net spreadsheets | Locate a particular connection or part precisely | [S9] |
| H04 | “Design Walk” follows a selected net across sheets | Trace long connections through a larger design | [S9] |
| H05 | Reusable project clips, schematic-only or schematic plus associated PCB layout | Reuse a proven circuit without duplicate annotation or manual reconstruction | [S8] |
| H06 | Keyword/property library search, installed parts and integrated external-part import | Reduce part-authoring work | [S6] |
| H07 | PADS ASCII import, BSDL/multi-element FPGA import and supported vendor pinout mappings | Handle imported and high-pin-count parts | [S6] |
| H08 | Symbol creation, named/numbered/electrical-type pins, device definitions and multiple footprint mappings | Let users author reusable parts | [S12] pp. 63–71 |
| H09 | Library manager, property maintenance, moving/comparing parts between libraries | Maintain a consistent component catalog | [S6] |
| H10 | Approved managed libraries, release/version tracking, Git/SVN integration and project audits | Control the origin and revision of engineering parts | [S6], [S7] |

**Important distinction:** Labcenter's “50,000 installed parts” and “15 million
importable parts” are library/ECAD availability claims. They are not evidence that
all those parts have executable VSM/SPICE models, accurate transient behavior or
manufacturer qualification. An imported symbol/footprint is not automatically a
working simulation model.

## 4. Live component behavior and user interaction

| ID | Documented capability | Why it matters | Evidence |
|---|---|---|---|
| V01 | Colored single LEDs: green, red, blue and yellow | Read output state directly from the circuit | [S3], [S4] |
| V02 | Bi-color and common-anode/common-cathode RGB LED models | Observe independent channels and combined color behavior | [S4] |
| V03 | LED bargraphs, seven-segment displays and dot matrices | Inspect multiplexed indicators and display-driver firmware | [S4] |
| V04 | Character LCD, graphics LCD, OLED and other supported display models | See firmware-produced characters/pixels on the schematic | [S3], [S4], [S12] p. 172 |
| V05 | Buttons and switches operated during simulation | Exercise firmware input handling without stopping execution | [S3], [S12] p. 172 |
| V06 | Interactive potentiometers, including linear/logarithmic models | Change a physical input and observe the ADC/circuit response | [S4], [S12] pp. 172–184 |
| V07 | Interactive sensor stimulus, such as temperature increment/decrement controls | Change the physical stimulus and see the resulting protocol/firmware/display response | [S12] p. 172 |
| V08 | Supported DC motors, inertia/load/encoder variants, steppers and servos | Close a firmware/electromechanical control loop | [S4], [S5] |
| V09 | Active Popups: chosen schematic regions become docked interactive views in the IDE | Keep controls/displays accessible during source debugging | [S11], [S12] pp. 178–184 |
| V10 | Run and single-step behavior linked to the surrounding electronics | Observe the effect of code execution on the whole circuit | [S3], [S11] |

The catalog establishes the availability of these model families. It does not
establish the fidelity of every model. In particular, colored LEDs on screen do
not establish calibrated optical output, damage simulation or accurate perceived
brightness at every PWM frequency. The tutorial explicitly notes that a multiplexed
display may look incomplete during single stepping (PDF p. 182).

## 5. Probing and virtual instruments

| ID | Documented capability | Why it matters | Evidence |
|---|---|---|---|
| I01 | System scope launched during a run, with movable live signal probes and docking | Inspect signals without first placing and wiring a scope component | [S13], [S14] |
| I02 | Four-channel scope, X–Y mode, per-channel AC/DC coupling, gains/timebase, auto/one-shot triggering, cursors and colored traces | Compare signals and examine timing/amplitude relationships | [S13] |
| I03 | Logic analyzer with 24 channels, capture buffer, timing zoom/pan and edge/bus-value trigger conditions | Capture digital timing failures rather than only watch current values | [S13] |
| I04 | SPI and I²C analyzers in passive monitor, master or slave roles; configurable transactions and scripted stimulus | Both inspect and actively exercise a bus | [S3], [S13], [S12] pp. 187–189 |
| I05 | Signal generator: sine, square, triangle and sawtooth, plus AM/FM inputs | Drive analog circuitry with controlled stimulus | [S13] |
| I06 | Digital pattern generator with editing, clock/trigger control, scripts, hold/output-enable and stepping | Repeat complex digital stimulus | [S13] |
| I07 | Full-duplex virtual serial terminal with framing/handshake configuration | Exercise and debug UART communication | [S13] |
| I08 | Counter/timer for interval, frequency and pulse/event counts | Make timing behavior measurable | [S13] |
| I09 | AC/DC meters with ranges and configurable loading; RMS behavior; wattmeter for real/reactive/apparent power of sinusoidal signals | Measure circuit quantities, with instrument-specific behavior | [S4], [S13] |
| I10 | Proteus 9.2 System Meter: movable A/B probes, differential Va−Vb, AC/DC, min/max; current readings and wire-current direction arrows when enabled | Inspect voltage/current directly while adjusting the circuit | [S15] |
| I11 | CAN analyzer with DBC signal decode, frame/bit inspection, transmit, error injection and trace export | Inspect and stimulate supported networked embedded designs | [S5], [S13] |

The System Scope/System Meter observation workflow must be distinguished from
physically wired instrument models. The reviewed pages do not establish a full
input-impedance, probe-compensation, bandwidth or protection specification for
every Proteus instrument. Do not infer ideal or realistic loading universally.
Published channel counts/ranges are product statements, not measurements here.

## 6. Firmware and circuit-linked debugging

| ID | Documented capability | Why it matters | Evidence |
|---|---|---|---|
| D01 | Execute firmware on supported MCU models inside a mixed-mode SPICE circuit | Test firmware against external electronics and MCU peripherals | [S3] |
| D02 | Compile/attach firmware from VSM Studio, with supported compiler detection/configuration | Shorten edit/build/simulate cycles | [S11] |
| D03 | Source/assembly breakpoints and single stepping synchronized with the circuit | Link a code location to a physical signal change | [S3], [S11] |
| D04 | Register/address/variable watch windows and conditional watch breakpoints | Stop on a software-visible failure condition | [S3], [S12] pp. 180–184 |
| D05 | Breakpoints placed on schematic probes/hardware conditions | Stop when a circuit event occurs | [S3], [S12] pp. 184–185 |
| D06 | Component and MCU-peripheral diagnostic/trace messages | Inspect both sides of a communication failure | [S3], [S12] pp. 186–189 |
| D07 | Active display/control popups beside source code | Interact with hardware while staying in the debugging context | [S11] |
| D08 | 9.2 Cortex-M GDB remote integration for compatible external IDEs | Use a familiar debugger against the running simulated circuit | [S15] |

Supported CPU families, peripherals, debug formats and external IDE integrations
are specific to the model/package. “Supports an MCU family” must not be expanded
into “every part and every peripheral is complete.”

## 7. Graph-based analysis: additional module

The following twelve analysis types are explicitly listed by Labcenter's
Advanced Simulation page [S16]:

| ID | Analysis | Intended use |
|---|---|---|
| G01 | Analog transient | Time-domain analog waveforms |
| G02 | Digital transient | Logic-state timing |
| G03 | Mixed-mode transient | Correlated analog/digital behavior |
| G04 | Interactive analysis | Analyze a circuit with interactive stimulus |
| G05 | Distortion | Nonlinear signal distortion |
| G06 | DC parameter sweep | Response across a changed DC/model parameter |
| G07 | Frequency analysis | Frequency response |
| G08 | Fourier analysis | Spectral content of a response |
| G09 | Noise analysis | Circuit noise response |
| G10 | Transfer curve | Input/output transfer relationship |
| G11 | AC transfer sweep | AC transfer behavior under sweep conditions |
| G12 | Conformance | Compare digital results against a reference |

Graph placement, generator/probe placement, drag-to-add traces, live graph updates,
expanded graphs, zoom and measurement cursors accompany those analysis families.
Analog source types include DC, sine, pulse, piecewise-linear, file, audio,
exponent and single-frequency FM; digital source types include edge, pulse,
clock and pattern. EasyHDL supplies scripted arbitrary stimulus. [S16]

Proteus conformance analysis compares data at control-trace edges; absolute edge
times need not match. It must not be described as a universal exact-timestamp
comparison or assumed equivalent to EmbSIM's current scenario assertions. [S16]

## 8. Engineering QA, outputs and current additions

| ID | Documented capability | Why it matters | Evidence |
|---|---|---|---|
| Q01 | Design Explorer spreadsheet, property filtering and bulk edits | Audit and change a large component set coherently | [S9] |
| Q02 | NC/single-pin-net audit and library-version/compliance checks | Make unused connections and part provenance reviewable | [S2], [S7], [S10] |
| Q03 | Assembly variants: fitted/not-fitted status and pin-compatible substitutes | Maintain multiple products from one design | [S17] |
| Q04 | Variant-aware BOM and downstream assembly outputs | Keep a selected configuration consistent across workflows | [S17], [S18] |
| Q05 | Customizable BOM spreadsheet/WYSIWYG views, properties, grouping, CSV/Excel/PDF output | Produce usable engineering/procurement data | [S18] |
| Q06 | Project Notes templates, structured data objects, component-location and variant reports | Produce reusable project documentation | [S10] |
| Q07 | Schematic/layout cross-probing and netlist transfer | Maintain a path from verified diagram to PCB | [S3], [S9], [S12] pp. 38–47 |
| Q08 | 9.2 output-job profiles include schematic PDF, bitmap/SVG/DXF and other suite output tasks | Repeat a configured documentation/manufacturing release | [S15] |
| Q09 | Proteus 9 light/dark themes, redesigned selectors/dialogs and docking | Reduce workspace friction | [S14] |
| Q10 | EDAi/ProPilot contextual assistance for schematic, firmware and simulation; 9.2 AI library-part creation is marked beta | Accelerate supported authoring/analysis tasks | [S15], [S19] |

This survey did **not** establish a current, complete schematic ERC rule matrix,
every import/export round-trip guarantee, VSM behavior for every assembly variant,
universal pin-swap support, collaborative
multi-user editing, arbitrary component failure simulation or full automatic
schematic placement. Those require additional reference/manual or hands-on checks.
PCB clearance/length-matching/HDI features and the PCB 3D viewer are separate from
live schematic behavior and should not inflate this inventory.

The Proteus 9 release page says its new framework enables future Linux/Mac
versions. That statement does not establish that native Linux/macOS products are
currently shipped. EmbSIM's platform and appearance goals need their own evidence.

## 9. EmbSIM comparison: inspect the workflow, not the feature name

**Audit baseline:** EmbSIM commit
[`843df4c88639de36ba6c5cdad9591e10f83776c1`](https://github.com/EmbLinkOs/EmbSIM/commit/843df4c88639de36ba6c5cdad9591e10f83776c1),
[PR #62](https://github.com/EmbLinkOs/EmbSIM/pull/62). At the time of the survey,
main was `b1fc006`; #62 was open. The table includes work on that branch and is
not a claim that all of it is already merged or released.

“Present” means an implementation path was found, not feature parity or a new
performance/accuracy qualification. “Gap” refers to the described workflow.
“Partial” means useful pieces exist but the combined workflow is incomplete.

| Workflow | EmbSIM assessment | Concrete gap / next action |
|---|---|---|
| Grid, backgrounds, navigation, drafting labels | Present | Keep readable values and pin names across themes, zoom and dense sheets; benchmark with representative circuits |
| Pin-to-pin wiring, route edits, move/reroute and undo | Present / partial | Measure crowded-sheet behavior; evaluate repeat wiring, terminal/bus authoring and connectivity-by-touch semantics |
| Live colored wires, pin states and selected-net highlighting | Present on #62 | This visualizes nets; it does not animate component bodies |
| Single-color LED emits light on the schematic | **Gap** | Pass native component outputs into the symbol renderer and add a reusable visible-emission binding |
| RGB/bicolor/multiplexed display appearance | **Gap / model-dependent** | Use independent output channels and actual display state; define temporal rendering for PWM and multiplexing |
| Press a schematic button or drag a schematic pot while firmware runs | **Gap in the schematic** | Existing board hotspots and Run controls must become reusable symbol-local controls |
| Character/pixel output directly on the diagram | Partial / model-dependent | Reuse supported model observations; do not count a catalog symbol as a functioning display |
| Live scope probes and docked plots | Present / partial | Add stronger channel identity, triggers/capture modes and inspection from the main schematic workflow |
| Physical instrument R/C, burden and output impedance | Present on #62, bounded generic equivalents | Keep observation probes distinct from wired loads; extend qualification and instrument controls |
| On-sheet System Meter-style probe/readout interaction | Partial | Readouts exist in panels; direct placement/movement and differential probing need a designed workflow |
| I²C/SPI decoding and bus stimulus | Partial | Decode paths exist; inspect supported framing/errors and add dedicated active master/slave stimulus where needed |
| Cross-linked circuit event, source location and waveform time | Partial | Debug, probes and scenarios exist; one coherent circuit-event navigation workflow still needs acceptance evidence |
| Multi-sheet hierarchical authoring | Gap in inspected project UI/schema | Read-only “inside” inspection and composite models are useful foundations, not a general sheet/hierarchy editor |
| Reusable circuit modules, approved libraries and versions | Partial / separate work | Catalog import, component descriptions and project import/export need a more complete reusable-module/revision workflow |
| BOM, variants and release documentation | Partial / separate work | Inventory/export exists; avoid calling it the same as a managed variant-aware production BOM |
| AC/noise/Fourier/sweeps and reference conformance | Not established as equivalent | Native transient/scenario measurements do not establish all twelve Advanced Simulation families |
| Native OS support and Mac-style presentation | Existing EmbSIM direction | Validate actual installers, keyboard behavior, scaling, accessibility and frame-time on each supported OS |

### Why LEDs can work on the bench but stay static on the schematic

In the inspected branch:

1. Electrical LED models already emit `lit` and `luminance`. RGB models additionally
   emit channel current/luminance and an illustrative `color` value. Functional
   indicator models also expose state. See [the analog model observations][E1]
   and [digital indicators][E2].
2. [BoardView][E3] accepts component outputs, artwork state bindings and interactive
   hotspots. It already has concepts useful for visible state and input controls.
3. [SchematicCanvas][E4] accepts net states and uses them for wires/pin rings. Its
   placed symbols do not receive a component-output snapshot.
4. [ComponentSymbol][E5] renders the catalog SVG at the component's position and
   orientation. Its current props do not provide output-driven lighting or
   symbol-local input dispatch.

The first implementation should connect those existing model and presentation
contracts. The renderer should use electrical observations, with an explicit
separate appearance policy for functional/timed simulation. It must not decide
that a component emits light merely because one GPIO is high.

## 10. Proposed next milestone: live schematic workbench

This is a proposal informed by the survey, not work implemented by this document.
Deliver the following as one usable interaction loop.

### A. Reusable component presentation contract

Expose engine outputs/inputs to placed schematic symbols. Define reusable,
validated bindings for fill, intensity, text, segments/pixels, position and input
hotspots. Share the underlying interaction/state machinery with board artwork
where practical; keep symbol geometry and electrical nets authoritative.

Start with single-color and RGB LEDs, then extend the same mechanism to displays,
switches and supported moving parts. Extension authors should be able to describe
states and controls without editing the application for every new component.

### B. Light and display rendering

Render explicit off/on/unknown appearance, part-defined colors, channel mixing,
and readable output details. Preserve state through pause and clear/replace it
correctly on reset or project replacement. Keep selection halos and wire colors
visually distinct from emitted light.

For PWM and multiplexing, specify the display exposure window. A 30 Hz UI snapshot
of a 1 kHz PWM output can alias; a displayed brightness/color must come from an
engine-supplied exposure statistic or a captured-window measurement with known
coverage. Do not change the electrical result to make the animation attractive.
Brightness remains illustrative until there is actual photometric qualification.

### C. On-symbol controls

Provide an explicit interaction gesture/tool so pressing a button cannot
accidentally move the part or start a wire. Support hold/toggle, keyboard access,
rotary/slider input and bounded numeric sensor stimulus where the model supplies
those inputs. Pointer cancellation, lost capture, focus changes and view changes
must release held inputs correctly.

Use the same input commands and value constraints as CLI/scenario/board controls.
Editing a physical input while firmware runs should change the circuit first;
firmware and downstream indicators should respond to that change.

### D. Measurement from the diagram

Make net hover/pin inspection, selected-net readouts, differential voltage,
current-direction display and scope attachment consistent. Show units, signal
identity, simulation time and whether a measurement is an observation or a wired
load. Navigate from a warning or waveform event back to the relevant terminal.

A current-direction overlay needs a defined branch/reference orientation and
actual signed engine current. A scalar node voltage or arbitrary net direction
is not enough to infer it. Add triggers and reusable stimulus after the basic
live diagram and capture path are reliable.

### E. Acceptance circuits and completion criteria

| Circuit | Required visible behavior and evidence |
|---|---|
| Five individually driven LEDs | Red, green, blue, yellow and white use the declared part color; off/unpowered/reverse/open-return cases stay dark when electrical current does not support emission |
| Common-anode and common-cathode RGB | Check individual and combined channels, polarity, separate resistor currents and output-to-color mapping |
| Firmware PWM indicator | At 0/25/50/75/100% duty, compare exact electrical capture with the declared display-exposure behavior; repeated runs must not randomly appear off due to phase aliasing |
| Pushbutton → firmware → LED | Press/release on the schematic changes native input state, firmware reads it and the schematic LED follows measured output; verify keyboard and interrupted holds |
| Potentiometer → ADC → display | Dragging the control changes circuit voltage and firmware conversion; the displayed value/pixels come from the modeled device |
| Multiplexed seven-segment/display | Check refresh, blanking, live running and single-step visibility under a documented exposure policy |
| Instrument-loaded GPIO/RC | Connecting input R/C or an ammeter changes voltage/current/rise time, and on-sheet readout agrees with CLI/native measurements |
| Floating and opposing drivers | Unknown, floating and confirmed conflict states remain distinct; a warning locates the correct net/terminals |
| Dense interactive sheet | Readable values/pins at useful zoom; no control/selection/wiring conflicts; measure performance at 100, 500 and 1,000 components rather than claim it |
| Save/reopen/export | Preserve part identity, wiring, display/control bindings and parameters; record whether transient runtime state is restored or reset |

Then prioritize repeated wiring/buses and editable hierarchical sheets, followed
by trigger-rich instruments and advanced analysis. Managed libraries, variants
and production outputs should have their own milestones and acceptance data.

## Source register

All sources below are public Labcenter pages or Labcenter-hosted PDFs retrieved
on 2026-10-26. Vendor claims are attributed; no vendor source code or artwork is
copied into EmbSIM. The tutorial is 212 pages. Download hashes for reproducibility:

- `downloads/Tutorials_p9.pdf`: SHA-256
  `0750e41607634b2e3f69054e3926d88c58f6118ef8120eb581053e7a575dba39`.
- `downloads/proteusVsm.pdf`: SHA-256
  `ba76b831eb632a22a3d2c68f91b3d34b88325d95bb42446f853032776bdf3974`.

| ID | Vendor reference | Used for |
|---|---|---|
| S1 | [Schematic Capture](https://www.labcenter.com/schematic/) | Capture feature families and drawing customization |
| S2 | [Wiring and Buses](https://www.labcenter.com/wiring/) | Modeless wiring, routing, buses, hierarchy, NC |
| S3 | [VSM Simulation](https://www.labcenter.com/simulation/) | Firmware/circuit co-simulation, interaction and debugging |
| S4 | [Simulation peripherals](https://www.labcenter.com/peripherals/) | Exact LED/RGB/display families and peripheral model availability |
| S5 | [VSM technologies](https://www.labcenter.com/vsm-technologies/) | System applications and supported bus/motor workflows |
| S6 | [Libraries](https://www.labcenter.com/libraries/) | Search, imports, symbols/footprints and library management |
| S7 | [Managed Libraries](https://www.labcenter.com/managedlibs/) | Approved/revision-controlled library workflows |
| S8 | [Project Clips](https://www.labcenter.com/projectclips/) | Reusable schematic/PCB blocks and hierarchy |
| S9 | [Design Explorer](https://www.labcenter.com/designexplorer/) | Component/net spreadsheet, cross-probing and Design Walk |
| S10 | [Project Notes](https://www.labcenter.com/projectnotes/) | Reports, NC/single-pin audits and documentation |
| S11 | [VSM Studio](https://www.labcenter.com/vsmstudio/) | Firmware integration, debugging and Active Popups |
| S12 | [Official schematic/simulation/PCB tutorial PDF](https://www.labcenter.com/downloads/Tutorials_p9.pdf) | Specific editing and interactive-simulation workflows; legacy terminology retained |
| S13 | [Virtual Instruments](https://www.labcenter.com/instruments/) | Scope, triggers, analyzers, generators, meters and current instrument descriptions |
| S14 | [Proteus 9.0 release](https://www.labcenter.com/whatsnew/9.0/) | New system scope, docking, themes/framework and managed libraries |
| S15 | [Proteus 9.2 release](https://www.labcenter.com/whatsnew/9.2/) | System Meter/current arrows, external GDB, output jobs and AI-part beta |
| S16 | [Advanced Simulation](https://www.labcenter.com/advancedsim/) | Twelve analysis types, graph workflow, EasyHDL and conformance semantics |
| S17 | [Assembly Variants](https://www.labcenter.com/assemblyvariants/) | Fitted status, substitutions and variant-aware downstream behavior |
| S18 | [Bill of Materials](https://www.labcenter.com/bom/) | BOM editing, customization and outputs |
| S19 | [EDAi](https://www.labcenter.com/edai/) | Current vendor-described context-aware assistance |
| S20 | [VSM technical brochure PDF](https://www.labcenter.com/downloads/proteusVsm.pdf) | Corroborating overview of interactive hardware, instruments and debugging |

[S1]: https://www.labcenter.com/schematic/
[S2]: https://www.labcenter.com/wiring/
[S3]: https://www.labcenter.com/simulation/
[S4]: https://www.labcenter.com/peripherals/
[S5]: https://www.labcenter.com/vsm-technologies/
[S6]: https://www.labcenter.com/libraries/
[S7]: https://www.labcenter.com/managedlibs/
[S8]: https://www.labcenter.com/projectclips/
[S9]: https://www.labcenter.com/designexplorer/
[S10]: https://www.labcenter.com/projectnotes/
[S11]: https://www.labcenter.com/vsmstudio/
[S12]: https://www.labcenter.com/downloads/Tutorials_p9.pdf
[S13]: https://www.labcenter.com/instruments/
[S14]: https://www.labcenter.com/whatsnew/9.0/
[S15]: https://www.labcenter.com/whatsnew/9.2/
[S16]: https://www.labcenter.com/advancedsim/
[S17]: https://www.labcenter.com/assemblyvariants/
[S18]: https://www.labcenter.com/bom/
[S19]: https://www.labcenter.com/edai/
[S20]: https://www.labcenter.com/downloads/proteusVsm.pdf
[E1]: https://github.com/EmbLinkOs/EmbSIM/blob/843df4c88639de36ba6c5cdad9591e10f83776c1/core/analog/devices.c
[E2]: https://github.com/EmbLinkOs/EmbSIM/blob/843df4c88639de36ba6c5cdad9591e10f83776c1/core/model/digital.c
[E3]: https://github.com/EmbLinkOs/EmbSIM/blob/843df4c88639de36ba6c5cdad9591e10f83776c1/ui/packages/scene/src/BoardView.tsx
[E4]: https://github.com/EmbLinkOs/EmbSIM/blob/843df4c88639de36ba6c5cdad9591e10f83776c1/ui/packages/scene/src/SchematicCanvas.tsx
[E5]: https://github.com/EmbLinkOs/EmbSIM/blob/843df4c88639de36ba6c5cdad9591e10f83776c1/ui/packages/scene/src/ComponentSymbol.tsx
