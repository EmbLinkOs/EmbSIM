# Interface direction

The product targets a polished engineering workbench across Linux, macOS and
Windows. The current browser UI establishes the initial visual language:
dark navy surfaces, restrained mint signal highlights, clear typography,
monospaced identifiers/time, structured panels, and explicit run state.
Appearance is a product requirement, alongside simulator correctness.

The current circuit overview is a generated read-only connectivity diagram,
not a schematic editor. Real press/release controls, run/pause/reset, stepping,
signal inspection and snapshot export are implemented. No toolbar control may
pretend to implement an unavailable feature.

## Planned workbench

- Central schematic canvas with pan/zoom, grid snapping, named ports and wiring.
- Searchable board/component library with model fidelity and pin documentation.
- Board view with interactive switches and displays, connected to schematic pins.
- Inspector for component parameters, pin states and model limitations.
- Dockable logic analyzer, oscilloscope, serial terminal and protocol decode.
- Debug perspective with source, registers, memory, breakpoints and call stacks.
- Accessible light/dark themes, keyboard operation, touch targets and high DPI.
- Visual diagnostics at the affected wire or pin, with actionable explanations.

Layout must adapt to smaller screens without hiding required actions. Color is
supplemented by state labels, symbols and diagnostic text. Respect reduced motion.
Keyboard focus must remain stable as simulation observations refresh. Heavy
waveform updates must not block control commands or alter simulated timing.

Before a released GUI, validate layouts on all target platforms, verify keyboard
and assistive technology behavior, and benchmark large circuits. Browser-based
delivery now does not preselect the eventual desktop packaging technology.
