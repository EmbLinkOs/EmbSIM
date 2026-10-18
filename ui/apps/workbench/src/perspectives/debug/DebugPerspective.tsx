// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The Debug perspective (SPEC-011 §4): a board's core halted, stepped and
// inspected while its circuit is held at the same instant. The toolbar runs,
// halts and steps; the disassembly shows the instructions around the PC with
// breakpoints in the gutter; the state column shows registers (editable, the
// ones the last step changed marked), breakpoints, memory, peripherals and the
// console. Everything shown is the engine's answer to the debug.* methods.

import { Badge, Button, DockLayout, EmptyState, Input, type DockItem } from '@embsim/design';
import { Bug, CirclePause, Play, RotateCcw, StepForward } from 'lucide-react';
import { useEffect, useMemo, useRef, useState } from 'react';
import { tryClient } from '../../connection';
import { hex32, hexReg, useDebug } from '../../stores/debug';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';
import { Placeholder } from '../Placeholder';
import { demangle } from './demangle';

function useDebugBoard() {
  const boards = useSession((s) => s.boards);
  return useMemo(() => boards.filter((b) => b.runtime && b.runtime !== 'none'), [boards]);
}

export function DebugPerspective() {
  const name = useProject((s) => s.project?.name ?? 'none');
  const debuggable = useDebugBoard();
  const capable = useSession((s) => s.capabilities.includes('debug'));
  const running = useSession((s) => s.running);
  const refresh = useDebug((s) => s.refresh);

  // stops (a breakpoint, a pause) refresh the view
  useEffect(() => {
    const c = tryClient();
    if (!c || !capable) return;
    const offStop = c.on('debug.stopped', () => void refresh());
    const offState = c.on('control.state', (st) => {
      if (!st.running) void refresh();
    });
    void refresh();
    return () => {
      offStop();
      offState();
    };
  }, [capable, refresh]);

  useEffect(() => {
    if (!running) void refresh();
  }, [running, refresh]);

  if (!capable || debuggable.length === 0) {
    return (
      <div className="wb-perspective">
        <Placeholder
          title="Debug"
          description="Halt, step and inspect a board's firmware: disassembly with breakpoints, registers, memory, peripherals and the console."
          requires={['debug']}
          needsBoard
          actions={['a board whose MCU has firmware (boards[].firmware in the project)']}
        />
      </div>
    );
  }

  const left: DockItem[] = [
    { id: 'source', title: 'Source', weight: 2.2, content: <SourcePanel /> },
    { id: 'disassembly', title: 'Disassembly', weight: 1.6, content: <DisassemblyPanel /> },
    { id: 'console', title: 'Console', weight: 1, content: <ConsolePanel /> },
  ];
  const right: DockItem[] = [
    { id: 'registers', title: 'Registers', weight: 1.4, content: <RegistersPanel /> },
    { id: 'breakpoints', title: 'Breakpoints', weight: 0.8, content: <BreakpointsPanel /> },
    { id: 'memory', title: 'Memory', weight: 1.2, content: <MemoryPanel /> },
    { id: 'peripherals', title: 'Peripherals', weight: 1.2, content: <PeripheralsPanel /> },
  ];
  const items: DockItem[] = [
    { id: 'left', title: 'Code', bare: true, weight: 2, minSize: 400, content: <DockLayout direction="column" items={left} storageKey={`embsim.layout.debug2.left.${name}`} /> },
    { id: 'right', title: 'State', bare: true, weight: 1.3, minSize: 300, content: <DockLayout direction="column" items={right} storageKey={`embsim.layout.debug2.right.${name}`} /> },
  ];
  return (
    <div className="wb-perspective wb-debug">
      <DebugToolbar />
      <div className="wb-debug__body">
        <DockLayout direction="row" items={items} storageKey={`embsim.layout.debug2.${name}`} />
      </div>
    </div>
  );
}

function DebugToolbar() {
  const { where, busy, error, step, resume, halt } = useDebug();
  const running = useSession((s) => s.running);
  const boards = useDebugBoard();
  const board = useDebug((s) => s.board) ?? boards[0]?.id;
  const reset = async () => {
    await tryClient()?.control.reset({});
    await useDebug.getState().refresh();
  };
  const place = where
    ? `${where.core !== undefined ? `core ${where.core} · ` : ''}${where.function ? `${demangle(where.function)}+${where.offset ?? 0}` : ''} ${hex32(where.pc)}${where.file ? ` · ${where.file.split(/[\\/]/).pop()}:${where.line}` : ''}`
    : '';
  return (
    <div className="wb-debug__toolbar" role="toolbar" aria-label="Debugger">
      <Bug aria-hidden size={16} />
      <span className="wb-debug__board">{board}</span>
      {running ? (
        <Badge tone="accent">running</Badge>
      ) : where?.stopped ? (
        <Badge tone="warning">stopped: {where.stopped}</Badge>
      ) : (
        <Badge>paused</Badge>
      )}
      <code className="wb-debug__where">{place}</code>
      <span className="wb-debug__spacer" />
      <Button size="sm" variant="primary" icon={<Play size={14} />} disabled={running || busy} onClick={() => void resume()} title="Continue (F5)">
        Continue
      </Button>
      <Button size="sm" icon={<CirclePause size={14} />} disabled={!running && !!where?.stopped} onClick={() => void halt()} title="Halt the core and the session">
        Halt
      </Button>
      <Button size="sm" icon={<StepForward size={14} />} disabled={running || busy} onClick={() => void step(1)} title="One instruction (F11)">
        Step
      </Button>
      <Button size="sm" disabled={running || busy} onClick={() => void step(10)} title="Ten instructions">
        Step 10
      </Button>
      <Button size="sm" icon={<RotateCcw size={14} />} onClick={() => void reset()} title="Reset the session">
        Reset
      </Button>
      {where && (
        <span className="wb-debug__counts">
          {where.instructions.toLocaleString()} instructions · {where.cycles.toLocaleString()} cycles
        </span>
      )}
      {error && <span className="wb-debug__error" role="alert">{error}</span>}
    </div>
  );
}

function SourcePanel() {
  const source = useDebug((s) => s.source);
  const where = useDebug((s) => s.where);
  const lineBps = useDebug((s) => s.lineBreakpoints);
  const toggle = useDebug((s) => s.toggleLineBreakpoint);
  const current = useRef<HTMLTableRowElement | null>(null);
  useEffect(() => current.current?.scrollIntoView({ block: 'center' }), [where?.line, source]);
  if (!source) {
    return (
      <EmptyState title="No source">
        {where && !where.file ? 'The image has no line information: build it with -g.' : 'Halt the core to see the line it is at.'}
      </EmptyState>
    );
  }
  const code = new Set(source.lines);
  const lines = source.text.split('\n');
  const here = where?.file === source.file ? where.line : undefined;
  return (
    <div className="wb-debug__scroll">
      <div className="wb-debug__file">{source.path}</div>
      <table className="wb-debug__table wb-debug__src" aria-label={`Source of ${source.file}`}>
        <tbody>
          {lines.map((text, k) => {
            const n = k + 1;
            const set = lineBps.includes(`${source.file}:${n}`);
            return (
              <tr key={n} ref={n === here ? current : undefined} className={n === here ? 'is-pc' : undefined}>
                <td className="wb-debug__gutter">
                  {code.has(n) && (
                    <button
                      type="button"
                      className={set ? 'wb-debug__bp is-set' : 'wb-debug__bp'}
                      aria-label={`${set ? 'Remove' : 'Set'} breakpoint at line ${n}`}
                      onClick={() => void toggle(source.file, n)}
                    />
                  )}
                </td>
                <td className="wb-debug__addr">{n}</td>
                <td className="wb-debug__text">{text || ' '}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}

function DisassemblyPanel() {
  const insns = useDebug((s) => s.insns);
  const where = useDebug((s) => s.where);
  const breakpoints = useDebug((s) => s.breakpoints);
  const toggle = useDebug((s) => s.toggleBreakpoint);
  const current = useRef<HTMLTableRowElement | null>(null);
  useEffect(() => current.current?.scrollIntoView({ block: 'nearest' }), [where?.pc, insns]);
  if (!insns.length) return <EmptyState title="No disassembly">Halt the core to see its instructions.</EmptyState>;
  const bps = new Set(breakpoints.filter((b) => b.kind === 'code').map((b) => b.pc));
  return (
    <div className="wb-debug__scroll">
      <table className="wb-debug__table wb-debug__dis" aria-label="Disassembly">
        <tbody>
          {insns.map((i) => {
            const isPc = where?.pc === i.address;
            return (
              <tr key={i.address} ref={isPc ? current : undefined} className={isPc ? 'is-pc' : undefined}>
                <td className="wb-debug__gutter">
                  <button
                    type="button"
                    className={bps.has(i.address) ? 'wb-debug__bp is-set' : 'wb-debug__bp'}
                    aria-label={`${bps.has(i.address) ? 'Remove' : 'Set'} breakpoint at ${hex32(i.address)}`}
                    onClick={() => void toggle(i.address)}
                  />
                </td>
                <td className="wb-debug__addr">{hex32(i.address)}</td>
                <td className="wb-debug__text">
                  {i.label && (
                    <span className="wb-debug__label" title={i.label}>
                      {demangle(i.label)}:
                    </span>
                  )}
                  {i.text}
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}

function RegistersPanel() {
  const regs = useDebug((s) => s.regs);
  const previous = useDebug((s) => s.previous);
  const setRegister = useDebug((s) => s.setRegister);
  const [editing, setEditing] = useState<string | null>(null);
  const [text, setText] = useState('');
  if (!regs.length) return <EmptyState title="No registers">Halt the core to read them.</EmptyState>;
  return (
    <div className="wb-debug__scroll">
      <table className="wb-debug__table" aria-label="Registers">
        <tbody>
          {regs.map((r) => {
            const changed = previous[r.name] !== undefined && previous[r.name] !== r.value;
            return (
              <tr key={r.name} className={changed ? 'is-changed' : undefined}>
                <th scope="row">{r.name}</th>
                <td>
                  {editing === r.name ? (
                    <form
                      onSubmit={(e) => {
                        e.preventDefault();
                        setEditing(null);
                        void setRegister(r.name, text);
                      }}
                    >
                      <Input aria-label={`${r.name} value`} mono autoFocus value={text} onChange={(e) => setText(e.target.value)} onBlur={() => setEditing(null)} />
                    </form>
                  ) : (
                    <button
                      type="button"
                      className="wb-debug__value"
                      title="Edit"
                      onClick={() => {
                        setEditing(r.name);
                        setText(hexReg(r.value, r.size));
                      }}
                    >
                      {hexReg(r.value, r.size)}
                    </button>
                  )}
                </td>
                <td className="wb-debug__dec">{r.value >>> 0}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}

function BreakpointsPanel() {
  const breakpoints = useDebug((s) => s.breakpoints);
  const add = useDebug((s) => s.addBreakpoint);
  const remove = useDebug((s) => s.removeBreakpoint);
  const [text, setText] = useState('');
  return (
    <div className="wb-debug__pane">
      <form
        className="es-row"
        onSubmit={(e) => {
          e.preventDefault();
          void add(text);
          setText('');
        }}
      >
        <Input aria-label="Breakpoint: a function or an address" placeholder="main, EXTI0_IRQHandler, 0x080002f4" mono value={text} onChange={(e) => setText(e.target.value)} />
        <Button size="sm" type="submit" disabled={!text.trim()}>
          Add
        </Button>
      </form>
      {breakpoints.length === 0 ? (
        <div className="wb-debug__hint">No breakpoints. Click the gutter of the disassembly, or name a function.</div>
      ) : (
        <ul className="wb-debug__list">
          {breakpoints.map((b, k) => (
            <li key={k}>
              <code title={b.function}>{b.kind === 'code' ? `${b.function ? `${demangle(b.function)}+${b.offset ?? 0} ` : ''}${hex32(b.pc ?? 0)}` : `${b.kind} ${hex32(b.address ?? 0)} (${b.length})`}</code>
              <Button size="sm" variant="ghost" onClick={() => void remove(b)} aria-label="Remove breakpoint">
                Remove
              </Button>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}

function MemoryPanel() {
  const memory = useDebug((s) => s.memory);
  const address = useDebug((s) => s.memAddress);
  const load = useDebug((s) => s.loadMemory);
  const where = useDebug((s) => s.where);
  const [text, setText] = useState(`0x${address.toString(16).padStart(8, '0')}`);
  const chosen = useDebug((s) => s.memChosen);
  useEffect(() => {
    if (!chosen) setText(`0x${address.toString(16).padStart(8, '0')}`);
  }, [address, chosen]);
  useEffect(() => {
    void load();
  }, [load, where?.pc, where?.ram]);
  const rows: { at: number; bytes: number[] }[] = [];
  if (memory) for (let i = 0; i < memory.length; i += 16) rows.push({ at: address + i, bytes: Array.from(memory.slice(i, i + 16)) });
  return (
    <div className="wb-debug__pane">
      <form
        className="es-row"
        onSubmit={(e) => {
          e.preventDefault();
          void load(text);
        }}
      >
        <Input aria-label="Memory address or symbol" mono value={text} onChange={(e) => setText(e.target.value)} />
        <Button size="sm" type="submit">
          Read
        </Button>
      </form>
      <div className="wb-debug__scroll">
        <pre className="wb-debug__hex" aria-label="Memory">
          {rows
            .map(
              (r) =>
                `${hex32(r.at)}  ${r.bytes.map((b) => b.toString(16).padStart(2, '0')).join(' ')}  ${r.bytes
                  .map((b) => (b >= 32 && b < 127 ? String.fromCharCode(b) : '.'))
                  .join('')}`,
            )
            .join('\n')}
        </pre>
      </div>
    </div>
  );
}

function PeripheralsPanel() {
  const peripherals = useDebug((s) => s.peripherals);
  const load = useDebug((s) => s.loadPeripherals);
  const where = useDebug((s) => s.where);
  const [filter, setFilter] = useState('');
  const [open, setOpen] = useState<string | null>('RCC');
  useEffect(() => {
    void load();
  }, [load, where?.pc]);
  const shown = peripherals.filter((p) => p.name.toLowerCase().includes(filter.toLowerCase()));
  return (
    <div className="wb-debug__pane">
      <Input aria-label="Filter peripherals" placeholder="Filter: GPIO, USART1, TIM2" value={filter} onChange={(e) => setFilter(e.target.value)} />
      <div className="wb-debug__scroll">
        {shown.map((p) => (
          <div key={p.name} className="wb-debug__periph">
            <button type="button" className="wb-debug__periph-head" aria-expanded={open === p.name} onClick={() => setOpen(open === p.name ? null : p.name)}>
              <strong>{p.name}</strong> <code>{hex32(p.base)}</code> <span className="wb-debug__model">{p.model}</span>
              {p.reset && <Badge tone="warning">in reset</Badge>}
              {!p.clock && <Badge tone="warning">clock off</Badge>}
            </button>
            {open === p.name && (
              <table className="wb-debug__table" aria-label={`${p.name} registers`}>
                <tbody>
                  {p.registers.map((r) => (
                    <tr key={r.name}>
                      <th scope="row">{r.name}</th>
                      <td className="wb-debug__dec">+{r.offset.toString(16).padStart(2, '0')}</td>
                      <td>{hex32(r.value)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </div>
        ))}
      </div>
    </div>
  );
}

function ConsolePanel() {
  const boards = useDebugBoard();
  const board = useDebug((s) => s.board) ?? boards[0]?.id;
  const info = boards.find((b) => b.id === board);
  const outputs = useSession((s) => s.outputs);
  const text = info?.component ? String(outputs[`${info.component}.rx_text`] ?? '') : '';
  const end = useRef<HTMLDivElement | null>(null);
  useEffect(() => end.current?.scrollIntoView({ block: 'end' }), [text]);
  return (
    <div className="wb-debug__scroll">
      <pre className="wb-debug__console" aria-label="Console" aria-live="polite">
        {text || 'The console (the MCU’s default UART) is empty.'}
      </pre>
      <div ref={end} />
    </div>
  );
}
