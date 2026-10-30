# The 3D bench and world: a plan to reach the professional tools

Owner's request (2026-10-30): the 3D bench and world are far behind
Proteus, KiCad and Microchip's tools. Find what they have, plan how to get
there, then build it. This plan follows ADR-027 (the bench is a second
editor of the same nets), ADR-028 (frame budgets), ADR-029 (licences) and
the visual benchmark (`docs/research/visual-workbench-benchmark.md`).

## 1. What the professional tools do in 3D

| Tool | What its 3D view does | Sources |
|---|---|---|
| KiCad 3D viewer | The board's real stack-up: copper, solder mask, silkscreen, paste, board thickness, plated holes. Each footprint carries a STEP/VRML model with an offset, rotation and scale. Standard views (top, bottom, front, back, left, right) with an orthographic toggle. Layer visibility (each layer, through-hole, SMD, virtual parts, models outside the courtyard). Material modes (realistic, solid, CAD). A ray tracer for renders. Export to STEP, VRML and GLB, and render to an image. | [3D viewer](https://www.kicad.org/discover/3dviewer/), [development highlight](https://kicad.org/blog/2020/09/Development-Highlight-3D-Viewer-Improvements), [3D model library and its licence](https://grok.dragonflybsd.org/xref/dports/cad/kicad-library-packages3d-devel/kicad-packages3D-1080b6e565e56bae9be46db2278a1542092d7a2d/LICENSE.md) |
| Proteus 3D visualizer | Board thickness, component models, silkscreen, mask, tracks and vias. A bare-board mode. A height plane (a semi-transparent box at a given height) to find parts too tall for the enclosure. STEP/IGES per footprint, fetched from its library. STEP assembly export. | [3D viewer](https://labcenter.com/3dviewer), [3D visualization](https://www.labcenter.com/blog/pcb-3d-visualization), [STEP](https://www.labcenter.com/step/) |
| Altium Designer | A section view (cutting planes dragged through the board and parts). 3D measurement (object to object, face to face, object to board). 3D clearance checking between bodies. Enclosure STEP imported for fit. Rigid-flex folding. | [3D view](https://www.altium.com/documentation/altium-designer/pcb/3d-view), [3D measuring](https://resources.altium.com/p/3d-measuring-in-altium-designer) |
| Microchip MPLAB / Fritzing / Tinkercad / Wokwi | Board pictures with live LEDs and displays, breadboard wiring with real connector positions. They run firmware, but none is a 3D engineering view. | the benchmark's table |

What sets these tools apart from our bench today:

1. **Real geometry.** Every package is drawn at its JEDEC/IPC dimensions (or
   from the manufacturer's STEP), not as a box of its size.
2. **The board's stack-up.** Copper under a translucent mask, ENIG or HASL
   pads, silkscreen, a FR-4 edge, plated holes. Our boards are a picture
   on a box.
3. **Inspection tools.** Standard and orthographic views, sections,
   measurement, height planes, layer visibility.
4. **Exchange.** Models in (STEP/VRML/glTF), assemblies out (STEP/GLB),
   renders at print resolution.
5. **Render quality.** Ambient occlusion, soft shadows, anti-aliasing and
   physically based metals and plastics; a ray-traced mode for stills.

EmbSIM has what they do not: the circuit runs. LEDs light from the
engine's luminance, displays show the firmware's pixels, buttons press,
jumpers are nets, and the world's objects are what sensors see. The plan
keeps that and adds the five points above.

## 2. Where the bench is today

- **Rendering.** Three.js (r186) with ACES tone mapping, a RoomEnvironment
  for reflections, one shadowed sun, PCF shadows and MSAA.
- **Geometry.** Procedural bodies (`parts3d.ts`): resistor bands from the
  value, LED domes, TO-92, TO-220, DIP, SMD, buttons, pots, modules.
- **Boards.** A box with the 2D artwork as its texture, and headers.
- **Camera.** Perspective only, with "frame", "top" and "perspective".
- **Tools.** No measurement, section, height check, layer visibility or
  export beyond a screenshot.

## 3. The plan

Every stage must be usable when it lands (owner, 2026-10-17). Each stage
is one PR, with its tests and gallery shots.

### Stage 1: inspect as an EDA viewer does (this PR)

- **Views.** Top, bottom, front, back, left, right and isometric, each
  framed on the bench or the selection. Perspective and orthographic
  projections, with the same controls. Keys as KiCad's: Z top, Shift+Z
  bottom, Y front, Shift+Y back, X left, Shift+X right, I isometric,
  O projection.
- **Measure.** A tool: two clicks on any surface (snapped to a pin or hole
  within 1.5 mm) give the distance in mm with its Δx, Δy and Δz, drawn in
  the scene until cleared. Altium's object-to-object measurement in its
  simplest form.
- **Section.** A cutting plane across X, Y or Z at a chosen position, either
  side kept (Altium's section view). The bench's objects and their shadows
  are clipped; the desk stays whole. The cut faces are left open (capping
  them is later work).
- **Height plane.** A semi-transparent plane at a height above the desk.
  Parts whose bodies rise through it are outlined and listed (Proteus's
  height check, for an enclosure's lid).
- **Layers.** Show or hide boards, parts, breadboards, jumpers, the world's
  bodies and labels (KiCad's render options).
- **Render quality.** Draft (as today) or quality, a choice per user,
  presentation only:
  - quality adds ambient occlusion (GTAO), bloom on what emits light (a
    second render of only the emissive surfaces feeds it, so a bright desk
    does not bloom), 4× multisampling and a 4096² shadow map;
  - quality falls back to draft when frames miss the budget (ADR-028).
- **Export.**
  - A still at 2× or 4× the view's resolution (for documentation).
  - The scene as GLB (glTF binary) for Blender or an MCAD tool, in
    millimetres, with each part a named node.
  - STEP export needs a B-rep kernel (OpenCascade: LGPL, a referee only
    under ADR-029) and is left for later.

Exit:
- each view, measurement, section, height check and export works from the
  toolbar and the keyboard;
- the pure functions (view placement, measurement, height check, section)
  have unit tests;
- a Playwright spec measures between two known holes (2.54 mm × n);
- gallery shots before and after.

### Stage 2: real packages (JEDEC and IPC geometry)

- A package generator in TypeScript, as KiCad's (packages3D-generator) is
  in Python, from the standards' dimension tables:
  - SOIC/SOP/TSSOP/MSOP (MS-012, MO-153), QFP/LQFP/TQFP (MS-026),
    QFN/DFN (MO-220), SOT-23/SOT-223/SOT-89;
  - TO-92/TO-220/TO-252, chip passives 0402 to 2512, MELF;
  - electrolytic and ceramic disc capacitors, HC-49 crystals,
    pin and socket headers, JST and Dupont housings;
  - USB-B, USB-C and micro-B, DC barrel jacks, tactile switches and
    trimmers.
- Each with its pin 1 mark, mould draft, lead bends (gull-wing, J-lead),
  exposed pad and marking text from the part's name.
- A catalog part names its package and variant (`package: "SOIC-8"`); the
  bench draws that and its legs land at the pads' positions.
- Unit tests:
  - each body's extents against the standard's;
  - each lead's tip against its pad;
  - the pin order against the part's pin table.

### Stage 3: model assets (glTF in, KiCad's models as references)

- `component.json` may name a glTF model (`model3d: {file, offset,
  rotation, scale}`) beside the catalog part. Each model's licence and
  source are recorded in the part's provenance (ADR-029).
- A converter tool turns a KiCad VRML model into glTF with its materials,
  offline. KiCad's packages3D is CC-BY-SA 4.0 with an exception for
  designs, so converted models are separate assets with their attribution,
  never merged into code.
- A loader caches models and instances repeated parts, so one model serves
  a hundred LEDs.

### Stage 4: boards as boards (the stack-up)

- From the board's artwork:
  - FR-4 core with its edge colour and thickness;
  - copper (traces from the artwork's copper layer);
  - solder mask as a physical clearcoat over the copper, the copper showing
    through;
  - ENIG or HASL pads and plated through-holes with barrels;
  - silkscreen in its ink;
  - real holes cut through the board (mounting holes, header holes).
- The Uno, NUCLEO, Pico and ESP32-C3 boards drawn from their published
  layouts where the licence allows (the Uno's is CC-BY-SA, Arduino's
  reference design).

### Stage 5: interaction as a CAD tool

- An outline on the selection and on hover (the outline pass): done, drawn
  after tone mapping in draft and in quality.
- Cross-probing: select in the schematic and the 3D view frames it, and
  the reverse.
- An exploded view (each part lifted off its host): done, up to 25 mm.
- A view cube in a corner: done (a CSS cube turned by the camera's
  rotation; its faces frame the standard views).
- Jumpers that sag as wire does: done for spans of 70 mm or more, which
  drape onto the desk or the parts under them; short ones still arc.

### Stage 6: the world (M7)

- A room around the bench: floor, walls, table, window light, a sun by the
  time of day.
- Bodies with physics (Rapier, Apache-2.0, ADR-029), and a robot on the
  floor.
- Temperature and light as fields drawn when asked (a heat map over the
  bench).
- The world's objects are what the sensors' rays hit, as today.

### Stage 7: performance and qualification

- Instancing for breadboard holes and repeated parts, level of detail by
  distance, frames drawn on demand as today.
- A Playwright harness measures frame times on the reference scenes and
  enforces ADR-028's budgets (8.3 ms at 120 Hz, a 16.7 ms floor).
- The visual benchmark's V1 to V4 qualifications.

## 4. Not planned

- A full ray tracer in the browser. Quality mode's ambient occlusion and
  soft shadows cover the bench, and GLB export hands stills to Blender's
  Cycles.
- STEP export, until a permissively licensed B-rep path exists.
