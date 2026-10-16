// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { DockLayout, type DockItem } from '@embsim/design';
import { useProject } from '../../stores/project';
import { Placeholder } from '../Placeholder';

export function DebugPerspective() {
  const name = useProject((s) => s.project?.name ?? 'none');
  const mcu = { needsBoard: true, requires: ['gdb' as const] };
  const dwarf = { needsBoard: true, requires: ['gdb' as const, 'debug.dwarf' as const] };
  const left: DockItem[] = [
    { id: 'source', title: 'Source', weight: 2, content: <Placeholder title="Source" description="Firmware source at the current PC, with breakpoints in the gutter (debug.where, debug.breakpoint)." {...dwarf} actions={['step into / over / out', 'continue', 'set breakpoint in source']} /> },
    { id: 'disassembly', title: 'Disassembly', weight: 1.2, content: <Placeholder title="Disassembly" description="Instructions around the PC (debug.disassemble)." {...mcu} /> },
    { id: 'console', title: 'Console', weight: 1, content: <Placeholder title="Console" description="Semihosting and debugger output." {...mcu} /> },
  ];
  const right: DockItem[] = [
    { id: 'registers', title: 'Registers', content: <Placeholder title="Registers" description="Core registers, editable (debug.registers, debug.set_register)." {...mcu} actions={['edit register']} /> },
    { id: 'peripherals', title: 'Peripherals', content: <Placeholder title="Peripherals" description="Register and field decode from the SVD, writable (debug.peripherals)." {...mcu} /> },
    { id: 'memory', title: 'Memory', content: <Placeholder title="Memory" description="Hex view of an address range (debug.memory, debug.write_memory)." {...mcu} /> },
    { id: 'stack', title: 'Call stack', content: <Placeholder title="Call stack" description="Frames from DWARF unwinding (debug.where)." {...dwarf} /> },
    { id: 'variables', title: 'Variables', content: <Placeholder title="Variables" description="Locals and globals of the selected frame (debug.variables)." {...dwarf} actions={['watch variable']} /> },
    { id: 'breakpoints', title: 'Breakpoints', content: <Placeholder title="Breakpoints" description="Code, read, write and access breakpoints (debug.breakpoint)." {...mcu} /> },
  ];
  const items: DockItem[] = [
    { id: 'left', title: 'Code', bare: true, weight: 2, minSize: 400, content: <DockLayout direction="column" items={left} storageKey={`embsim.layout.debug.left.${name}`} /> },
    { id: 'right', title: 'State', bare: true, weight: 1.2, minSize: 300, content: <DockLayout direction="column" items={right} storageKey={`embsim.layout.debug.right.${name}`} /> },
  ];
  return (
    <div className="wb-perspective">
      <DockLayout direction="row" items={items} storageKey={`embsim.layout.debug.${name}`} />
    </div>
  );
}
