# Electronics workbench visual benchmark

Research date: 2026-10-18. Related work: [issue #40](https://github.com/EmbLinkOs/EmbSIM/issues/40).

**Conclusion:** EmbSIM should combine a precise schematic, a buildable physical
bench and live instruments around the same circuit. Better materials alone will
not close the gap with established tools: connector accuracy, reliable editing,
readable diagrams and credible feedback matter more. The first target is one
excellent, complete Uno/breadboard workflow, then reusable quality across parts
and boards.

This is a research and implementation proposal, not a support announcement or a
replacement for the accepted [roadmap](../architecture/07-roadmap.md).
[STATUS](../STATUS.md) remains the implementation evidence. It develops the
existing ADR-027/028/029/030/033 direction rather than selecting a new renderer.

## Method and limits

Reviewed official public product pages, documentation and published illustrations
linked below. Downloaded source pages were inspected for the specific capabilities
reported here. The Fritzing getting-started illustration and CircuitLab easy-wire
illustration were also viewed directly. Those are vendor illustrations, potentially
older than current releases; they are not hands-on measurements of current apps.

The comparison covers representative tools across embedded simulation, circuit
analysis, physical wiring and ECAD/MCAD. It is not an exhaustive census. No paid
application, private workspace or authenticated editor was exercised. A Chromium
attempt to open public demos failed certificate validation in this environment;
HTTPS document retrieval worked with normal certificate verification. No TLS
verification was disabled. Interactive usability, current prices, numerical
accuracy and comparative performance were not independently tested.

**Evidence labels:** D = official documentation/product statement; I = published
illustration inspected; C = EmbSIM source inspected; G = existing EmbSIM gallery
inspected. Recommendations below are design judgments, not measured competitor
rankings. No competitor code or artwork is included in this repository.

## Representative tools and what to learn

| Tool / official source | Evidence and documented strengths | Lesson for EmbSIM | Boundary of this comparison |
|---|---|---|---|
| [Proteus VSM](https://www.labcenter.com/simulation/) | D: MCU firmware within mixed-mode circuits, interactive switches/displays, source debugging, live system scope and virtual instruments | Connect firmware, circuit interaction and measurements in one coherent workspace; make probes easy to discover | Vendor statements, not independent verification of each MCU/peripheral; PCB 3D is a separate workflow |
| [Wokwi](https://docs.wokwi.com/), [diagram format](https://docs.wokwi.com/diagram-format) | D: named parts and pins, wire colours and routing instructions, serial monitor; [logic analyzer](https://docs.wokwi.com/guides/logic-analyzer) and [custom chips](https://docs.wokwi.com/chips-api/getting-started) | Recognizable boards, fast wiring, explicit endpoints, reusable extensions and a short path to running firmware | Its [analog API](https://docs.wokwi.com/chips-api/analog) explicitly describes a digital simulator with limited analog support; do not equate its appearance with general SPICE fidelity |
| [Tinkercad Circuits](https://www.tinkercad.com/circuits) | D, limited: official entry point available, but fetched page contained little readable feature detail | Include beginner-oriented physical circuit construction in the next hands-on comparison | Editor, interaction details and 3D circuit behavior not verified here; do not infer them from Tinkercad's separate 3D design product |
| [Fritzing getting started](https://fritzing.org/learning/get-started) | D/I: breadboard/schematic/PCB views, connector drag wiring, bend points, inspector, custom part creation | Physical connectors must be obvious and views must preserve circuit identity; show package and polarity before placement | Physical documentation and PCB workflow are the benchmark here; this review does not establish firmware simulation |
| [PICSimLab manual](https://lcgamboa.github.io/picsimlab_docs/stable/) | D: manual organizes boards, simulator interface, serial communication, oscilloscope, spare parts and new-board creation | A board should be a usable object with its own controls and peripherals; board authoring deserves documentation | Reviewed manual entry/structure, not every board chapter or backend |
| [SimulIDE](https://simulide.com/p/) | D: drag/drop circuit building, interactive simulation, analog/digital parts, MCU editor/debugger | Keep place → connect → run fast; instruments and controls should stay close to the circuit | Performance claims are vendor statements; no comparative timing run |
| [Falstad](https://www.falstad.com/circuit/), [CircuitJS](https://github.com/sharpie7/circuitjs1) | D: voltage colours, moving current dots, clickable switches, component state feedback and circuit examples | Explain electrical state visually with an explicit legend and selectable overlays | Animation is a diagnostic representation, not literal visible electrons or measured accuracy |
| [CircuitLab](https://www.circuitlab.com/), [documentation](https://www.circuitlab.com/docs/) | D/I: Smart Wires, compact node naming, human-readable values, plotting, vector schematic export | Wiring and moving parts should preserve readable circuits; names/values and exported diagrams are first-class outputs | Published easy-wire image shows a hand cursor at a terminal and a readable small circuit; large-project editing was not tested |
| [NI Multisim](https://www.ni.com/en/shop/electronic-test-instrumentation/application-software-for-electronic-test-and-instrumentation-category/what-is-multisim.html) | D: official circuit design/simulation product reference | Include instrument-oriented circuit analysis in later hands-on evaluation | Both multisim.com URLs redirected to NI's product page; no claim that the former online service remains available |
| [LTspice](https://www.analog.com/en/resources/design-tools-and-calculators/ltspice-simulator.html) | D: schematic capture, probing, waveform viewer, transient/AC/noise learning material | Measurement needs units, cursors, signal identity and useful exports, not just animated LEDs | Analog analysis benchmark; not evidence of physical board/firmware workflows |
| [KiCad schematic](https://www.kicad.org/discover/schematic-capture/), [3D viewer](https://www.kicad.org/discover/3dviewer/) | D: ERC, component libraries; interactive 3D inspection, synchronized PCB updates, ray tracing and rendering settings | Precise symbols and electrical checks; board geometry, camera controls and materials should support inspection | PCB geometry viewing is not live firmware simulation; ray tracing is not a prerequisite for interactive editing |
| [Autodesk Fusion](https://www.autodesk.com/products/fusion-360/overview) | D: schematic/PCB synchronization and ECAD/MCAD integration | Keep physical dimensions and mechanical context accurate; permit board/enclosure inspection later | Broad product overview only; no import round-trip or license tier verified |
| [Altium Designer](https://www.altium.com/altium-designer) | D: multiboard interconnect management, 3D assembly validation and MCAD synchronization | Eventually inspect connector orientation, clearances and assemblies, not isolated attractive boards | Mechanical/PCB benchmark; not proof of MCU firmware co-simulation |
| [Flux](https://www.flux.ai/p) | D: browser schematic/PCB workflow, rules and component research | Study discoverable contextual workflows and how a project stays coherent across stages | AI, routing and time-saving claims were not tested; automated changes still need reviewable electrical consequences |
| [Cirkit Designer](https://www.cirkitdesigner.com/) | D: physical parts reacting during simulation, firmware workflow, custom part pins/logic | Make extension authoring understandable and keep part appearance tied to behavior | AI-generated behavior is not qualification evidence; supported modes need independent fixtures |
| [Virtual Breadboard](https://www.virtualbreadboard.com/) | D: official physical-prototyping/Bread2Board demonstrations and tutorials | Include breadboard construction and transfer-to-real-build clarity in later hands-on sessions | Landing-page review only; detailed simulation and 3D capabilities not established |

[EasyEDA](https://easyeda.com/) was also attempted: the homepage returned HTTP 403
and guessed documentation routes returned 404. It is an **unreviewed follow-up**,
not evidence in this matrix. EveryCircuit's landing page was reachable but did
not provide enough extracted detail for a useful comparison here.

## EmbSIM today: preserve the recent work

Baseline: main commit `d2ebb759a7b46d9a733472cd5f205b7d3872fa4f`, including PR #39.
This is a source/gallery audit, not a fresh execution of the application.

| Area | Existing evidence | Gap to investigate or close |
|---|---|---|
| Schematic | C: [SchematicCanvas](../../ui/packages/scene/src/SchematicCanvas.tsx); [user guide](../user/schematic-studio.md) describes routing, placement, appearance and editing boundaries | Benchmark dense connected editing and label collisions; do not confuse existing heuristic layout with a complete professional constraint solver |
| Physical bench | C: [Bench3D](../../ui/packages/bench/src/Bench3D.tsx), [layout](../../ui/packages/bench/src/layout.ts), [view](../../ui/packages/bench/src/view.ts): Three.js, orbit/pan, frame, parts, plugging, jumpers and unbuilt links | Qualify the actual contact geometry, ambiguous target selection and electrical round trips; add workflow evidence, not a second graph |
| Part/board bodies | C: [parts3d](../../ui/packages/bench/src/parts3d.ts), [shape](../../ui/packages/bench/src/shape.ts): procedural packages, board features, artwork surfaces, pin anchors and generic fallbacks | Named-part package dimensions and pin order need per-variant qualification; procedural resemblance alone is insufficient |
| Shared workspace | C: [BoardPanel](../../ui/apps/workbench/src/perspectives/BoardPanel.tsx): shared selection, input actions, editable bench, part/hole list, jumper colours, 2D choice and initial WebGL availability detection | Test full keyboard task completion, renderer failure/context loss and selection across every view; automatic recovery after renderer failure is not established by initial availability detection |
| Presentation | G: [current bench gallery](../gallery/README.md), especially [design bench](../gallery/bench-3d/design-bench.png) | At 1440×900 the scene competes with library, a permanent bench list, an empty inspector and a large net table; labels and notes cover parts. Prioritize canvas space and semantic label visibility |
| Performance | C: demand-driven rendering and pixel-ratio cap in Bench3D; ADR-028 specifies budgets | Budgets are accepted targets, not measurements from this research. Run a repeatable harness before claiming 60/120 Hz |

The existing bench is substantial progress. The visual risk is **false precision**:
`shape.ts` can infer generic package geometry and header placement, while
`BoardView.tsx` can fall back to distributing missing anchors along an edge.
Those keep unsupported assets visible but must not silently certify a named
board's physical pin locations. For example, package inference groups named
parts by family; each actual orderable package still needs its own evidence.

## Visual direction

### A focused engineering workspace

Use a quiet, Mac-inspired shell: consistent system typography, restrained borders,
clear hierarchy, compact toolbars and subtle elevation. Avoid decorative glass,
oversized cards and a permanent dashboard surrounding a tiny canvas. Maintain
cross-platform shortcuts and visible focus rather than imitating macOS window
controls inside the application.

Keep the existing **Schematic / Bench** switch and **2D / 3D** bench choice.
The same component, terminal and net retain their identity throughout. Selection
opens one contextual inspector; its empty state should be collapsible. The part
list remains available as an accessible alternative, with a compact/folded state.
Measurements open in a resizable bottom dock; the default design workspace favors
the canvas. Do not remove information simply to make a screenshot cleaner.

Use semantic detail by zoom and task: board name at overview, references/values at
working scale, terminal rings and pin labels while wiring or inspecting. Selected
and hovered objects remain identifiable. Suppress overlapping secondary labels,
with an explicit way to reveal them. Keep diagnostics visible independently.

Retain user-selectable schematic grids, anchored during pan/zoom. Offer neutral
paper and graphite bench surfaces alongside the anti-static mat. Shadows and grid
contrast must not obscure holes, leads or dark wires. Make selection, hover,
physical wire colour and electrical-state overlays distinguishable. A colour
legend plus text/shape cues must explain floating, unknown and contention states.

### Board and component identity

For each qualified board revision record manufacturer references, outline,
mounting holes, PCB thickness, connector centers/pitch/orientation, top/bottom
placement and real control locations. A schematic board symbol, 2D artwork and 3D
body map to the same stable terminal IDs. Use manufacturer mechanical drawings
and photographs as complementary evidence; a photograph alone is not a scale or
pinout specification.

Use verified procedural bodies for common packages and glTF/GLB assets where they
improve board-specific detail, following ADR-027. Establish a documented transform
from catalog millimetres/top-down coordinates to renderer axes before asset
imports. Verify scale, origin, rotations, handedness, bottom-side transforms and
contact height against a fixture. Do not introduce a second unit convention.

Every asset needs source URL, revision, license, attribution, package variant,
terminal map and qualification status. Missing models get a clearly labelled
placeholder, never a confidently wrong package. STEP-to-runtime conversion is a
future authoring pipeline to evaluate, not a promised import capability. Separate
asset distribution follows ADR-029; public download does not itself grant reuse.

Model the features users manipulate or identify first: header sockets and metal
contacts, USB/jack bodies, pin 1, diode stripe, LED flat, electrolytic polarity,
DIP notch, terminal screws and button caps. Add cosmetic silkscreen and materials
after those are correct. Resistor bands and displays follow parameters/state;
cosmetic animation never invents engine state. Do not spend the first milestone
on cinematic ray tracing or photorealistic screws.

### Interconnection that engineers can trust

| Action | Required visible result and electrical meaning |
|---|---|
| Hover terminal | Highlight the exact contact, show board reference, printed pin name and net; a larger invisible hit target must not select the neighboring pin ambiguously |
| Start wire | Pencil/connection cursor and persistent source marker; preview endpoint, route and target; Escape cancels without changing connectivity |
| Finish wire | Commit the intended endpoints once; invalid/ambiguous targets explain why; snapped preview matches the committed connection |
| Cross or branch | Schematic crossing without a junction stays separate; a branch shows a junction. In 3D, overlapping insulation is not electrical contact |
| Plug a part | Preview every affected leg/hole, occupied holes and alignment; obey breadboard strip ties, rail splits and center trench; a rotated part must use its actual pin map |
| Move/rotate/delete | Distinguish moving a connected item, lifting a plugged part and deleting a connection; show net consequences, preserve undo/redo and never silently short rails |
| Inspect a net | Cross-highlight the same endpoints in schematic, flat view, 3D and net list; expose partly built physical connections separately from completed jumpers |
| Dress wiring | Support intentional bends/height and wire selection without changing endpoints; preserve chosen colour. Add physical connector mating rules only with declared model support |
| Auto-place/build | Preview, explain changes, honor locks/keep-outs and provide one undoable application. Auto-placement is a design convenience, not a real electrical action; never invent a valid physical assembly |

Accessible terminal lists and numeric placement must complete the same tasks as
pointer picking. A top-down camera, isolate selection and temporary visual
transparency can aid inspection; these are labelled viewing aids, not physical
changes to the board or its electrical model.

### Live behavior and instruments

Press/hold/release buttons, turn knobs, actuate switches and read displays while
firmware runs. Dragging a component and actuating its control need unambiguous
modes. Pointer cancellation, lost focus and keyboard release must not leave a
button held. Show reset/power/connection consequences from the engine, including
unsupported behavior and diagnostics; stop cosmetic animation when disconnected.

Keep **ideal debug views** distinct from the **physical instruments** accepted in
ADR-033. A physical meter or oscilloscope has connected leads and declared loading,
ranges and limits; the UI must not imply those are modeled before they are.
Likewise a bright LED mesh does not prove current, temperature or damage modeling.
World-driven sensor controls follow ADR-026 rather than arbitrary readings hidden
inside an attractive component. Record/replay uses the same input actions as CLI.

## Delivery sequence and acceptance gates

These are proposed work packages within M6–M8, not new release dates. Each lands
as a coherent usable slice, with docs, screenshots and declared omissions.

| Order | Deliverable | Exit evidence |
|---|---|---|
| V1: qualify the flagship | Uno R3 + 830-point breadboard + resistor/LED/button/potentiometer; declutter bench workspace; exact documented terminal mapping | Vendor mechanical/pinout references attached per asset; all exposed endpoints checked against independent reference data, not the same table used to render them; reference screenshots at overview and close-up; no unnamed geometry fallback in the qualified set |
| V2: complete connected editing | Pin picking, wire preview/reconnect/selection, rotation/plugging, branch/crossing clarity, multi-view cross-highlight and reviewable auto-placement | Build and modify the flagship by pointer and keyboard; net lists agree after each view switch; rail-split, trench, rotated part, cancellation and undo/redo regressions; moving a part cannot create a hidden short |
| V3: production board inspection | Camera presets, isolate/reveal, label occlusion strategy, material/surface preferences and robust renderer recovery; evaluate a single licensed GLB asset | Top/bottom connector mapping survives transforms; absent/corrupt/oversized asset and WebGL/context-loss paths stay usable; geometry qualification unchanged between procedural and imported representations |
| V4: live bench proof | Controls, displays and clearly identified ideal/physical instruments integrated into the running flagship | Unmodified vendor firmware responds to button/pot inputs; displayed outputs match engine signals; keyboard/pointer cancellation tests; recorded interaction replays through CLI with the same electrical outcome; physical instrument omissions explicitly listed |
| V5: scalable authoring and release | Board/part asset preview and validation, package variants, provenance checks, large-project polish and platform qualification | A contributor adds one board and one named part using documentation, without bespoke renderer code; intentionally wrong scale/pin map/license is rejected or flagged; performance/accessibility/platform gates below pass |

V1–V4 should iterate against one maintained acceptance project, rather than making
many attractive but unqualified board renders. Schematic improvements continue
alongside it: connected move/stretch, predictable junctions, readable annotations,
net search, hierarchy and sharp exports. Do not replace established editing
behavior with an unrelated new editor during the visual pass.

### Measurement protocol

- **Correctness first:** export canonical endpoint membership before/after a
  schematic ↔ bench round trip. Independently check expected changes for plugging,
  unplugging, rail splits, wire deletion and undo/redo. Compare engine results and
  CLI replay, not pixels alone.
- **Readability:** review 1440×900 and 1280×800 workspaces at normal and 200% UI
  zoom, light/dark themes and representative camera angles. Labels never hide the
  selected terminal or required control; dark wires remain visible. Capture these
  as original EmbSIM gallery images. The proposed layout target is at least 60%
  of available workbench content width for the canvas with auxiliary panels folded.
- **Input/accessibility:** complete place, wire, run, press, measure and undo without
  a pointer. Test visible focus, announcements, accessible names and alternative
  lists. Colour alone cannot communicate connection validity or signal state.
- **Performance:** implement ADR-028's harness: 500 schematic components and a
  50-part bench; frame work under 16 ms at 60 Hz / 8 ms at 120 Hz on a declared
  reference configuration. Record p50/p95/p99 frame work, missed frames, input
  latency, draw calls, memory and asset load size; do not disguise poor tail
  latency with average FPS. Specify hardware, OS, viewport, DPR, workload and
  sampling duration before setting enforced thresholds.
- **Platforms:** Chromium automation plus native Tauri webview smoke on Windows,
  macOS and Linux. Include trackpad, keyboard and mouse camera controls; test
  no-WebGL, context loss, resize, high DPI and reduced motion. Browser CI alone
  does not establish native webview parity.

## Follow-up evidence before claiming superiority

Run the same small build/edit/debug task in available licensed competitors and
EmbSIM, recording completion time, mistakes, corrective actions and readability.
Ask practicing embedded engineers to evaluate it without coaching. Start with
Proteus, Wokwi, Fritzing and KiCad because they cover the four principal workflows;
follow with Tinkercad, instrument tools and mechanical assembly tools as access
permits. Keep raw observations separate from vendor claims.

Only claim a specific advantage after a repeatable comparison, for example fewer
steps to trace a GPIO from schematic to its physical header, with equal electrical
correctness. This study does not establish overall superiority or eliminate the
need for real hardware validation of parasitics, EMC, thermals and unsupported
peripherals. The visual goal is a professional virtual bench whose limits are as
clear as its circuit.
