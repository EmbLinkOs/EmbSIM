// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The debugger's store: one board's core as the engine last reported it
// (debug.where, debug.registers, debug.disassemble, debug.breakpoints) and
// the actions that move it. Every value comes from the server; nothing is
// decoded or simulated here.

import type { Breakpoint, Instruction, Peripheral, Register, SourceFile, Where } from '@embsim/protocol';
import { create } from 'zustand';
import { tryClient } from '../connection';
import { useSession } from './session';

export interface DebugState {
  board: string | null;
  where: Where | null;
  regs: Register[];
  /** The values before the last stop, to show what changed. */
  previous: Record<string, number>;
  insns: Instruction[];
  breakpoints: Breakpoint[];
  memAddress: number;
  /** the memory view's address was chosen, not the part's RAM */
  memChosen: boolean;
  memory: Uint8Array | null;
  peripherals: Peripheral[];
  /** The source file at the PC, when the image has DWARF. */
  source: SourceFile | null;
  /** Line breakpoints set from the source view, as "file:line". */
  lineBreakpoints: string[];
  busy: boolean;
  error: string | null;
}

export interface DebugActions {
  setBoard(board: string | null): void;
  refresh(): Promise<void>;
  step(count?: number): Promise<void>;
  resume(): Promise<void>;
  halt(): Promise<void>;
  toggleBreakpoint(address: number): Promise<void>;
  addBreakpoint(spec: string): Promise<void>;
  removeBreakpoint(bp: Breakpoint): Promise<void>;
  setRegister(name: string, value: string): Promise<void>;
  loadMemory(address?: number | string): Promise<void>;
  loadPeripherals(): Promise<void>;
  toggleLineBreakpoint(file: string, line: number): Promise<void>;
}

export const hex32 = (v: number) => '0x' + (v >>> 0).toString(16).padStart(8, '0');
/** a register's value at its own width: 0x2a for an AVR's r16, 0x0800002a for a word */
export const hexReg = (v: number, size = 4) => '0x' + (v >>> 0).toString(16).padStart(2 * size, '0');

/** "0x0800_02f4", "134218484" or a symbol name. */
export function parseAddress(text: string): { address?: number; symbol?: string } {
  const t = text.trim();
  const n = t.replace(/_/g, ''); // digit grouping in numbers only: symbols keep theirs
  if (/^0x[0-9a-f]+$/i.test(n)) return { address: parseInt(n, 16) };
  if (/^[0-9]+$/.test(n)) return { address: parseInt(n, 10) };
  return t ? { symbol: t } : {};
}

function errorText(e: unknown): string {
  return e instanceof Error ? e.message : String(e);
}

export const useDebug = create<DebugState & DebugActions>((set, get) => {
  let selection = 0;
  let pending = 0;
  type Update = (state: Partial<DebugState>) => void;

  /** Runs a request against the selected board, keeping the error for the panels. */
  /** `action`: a user's request, whose failure stays shown until the next one. */
  async function withBoard<T>(fn: (board: string, update: Update) => Promise<T>, action = true): Promise<T | undefined> {
    const board = get().board ?? useSession.getState().boards.find((b) => b.runtime && b.runtime !== 'none')?.id ?? null;
    if (!board || !tryClient()) return undefined;
    if (board !== get().board) get().setBoard(board);
    const epoch = selection;
    const current = () => selection === epoch && get().board === board;
    const update: Update = (state) => { if (current()) set(state); };
    pending++;
    try {
      set(action ? { busy: true, error: null } : { busy: true });
      return await fn(board, update);
    } catch (e) {
      update({ error: errorText(e) });
      return undefined;
    } finally {
      if (current()) {
        pending--;
        set({ busy: pending > 0 });
      }
    }
  }

  return {
    board: null,
    where: null,
    regs: [],
    previous: {},
    insns: [],
    breakpoints: [],
    memAddress: 0x20000000,
    memChosen: false,
    memory: null,
    peripherals: [],
    source: null,
    lineBreakpoints: [],
    busy: false,
    error: null,

    setBoard: (board) => {
      if (board === get().board) return;
      selection++;
      pending = 0;
      set({ board, where: null, regs: [], previous: {}, insns: [], breakpoints: [],
        memChosen: false, memory: null, peripherals: [], source: null,
        lineBreakpoints: [], busy: false, error: null });
    },

    refresh: async () => {
      await withBoard(async (board, update) => {
        const c = tryClient()!;
        const where = await c.debug.where({ board });
        // disassemble from the function's start when it is near, so the
        // listing starts on an instruction boundary
        const start = where.offset !== undefined && where.offset < 96 ? where.pc - where.offset : where.pc;
        const [regs, dis, bps] = await Promise.all([
          c.debug.registers({ board }),
          c.debug.disassemble({ board, address: start, count: 40 }),
          c.debug.breakpoints({ board }),
        ]);
        if (get().board !== board) return;
        // what changed since the core last moved (a refresh at the same
        // instruction keeps the comparison)
        let previous = get().previous;
        if (get().where?.instructions !== where.instructions) {
          previous = {};
          for (const r of get().regs) previous[r.name] = r.value;
        }
        update({ where, regs: regs.regs, previous, insns: dis.insns, breakpoints: bps.breakpoints });
        if (!get().memChosen && where.ram !== undefined && where.ram !== get().memAddress) update({ memAddress: where.ram });
        if (!where.file) update({ source: null });
        if (where.file && where.file !== get().source?.file) {
          try {
            update({ source: await c.debug.source({ board, file: where.file }) });
          } catch {
            update({ source: null }); // the file is not on this machine
          }
        }
      }, false);
    },

    toggleLineBreakpoint: async (file, line) => {
      const key = `${file}:${line}`;
      const has = get().lineBreakpoints.includes(key);
      await withBoard(async (board, update) => {
        const c = tryClient()!;
        await c.debug.breakpoint({ board, op: has ? 'remove' : 'add', file, line });
        const bps = await c.debug.breakpoints({ board });
        update({
          breakpoints: bps.breakpoints,
          lineBreakpoints: has ? get().lineBreakpoints.filter((k) => k !== key) : [...get().lineBreakpoints, key],
        });
      });
    },

    step: async (count = 1) => {
      await withBoard(async (board) => {
        await tryClient()!.debug.step({ board, count });
      });
      await get().refresh();
    },

    resume: async () => {
      await withBoard(async () => {
        await tryClient()!.control.run({});
      });
    },

    halt: async () => {
      await withBoard(async (board) => {
        const c = tryClient()!;
        await c.control.pause({});
        await c.debug.halt({ board });
      });
      await get().refresh();
    },

    toggleBreakpoint: async (address) => {
      const has = get().breakpoints.some((b) => b.kind === 'code' && b.pc === address);
      await withBoard(async (board, update) => {
        await tryClient()!.debug.breakpoint({ board, op: has ? 'remove' : 'add', kind: 'code', address });
        const bps = await tryClient()!.debug.breakpoints({ board });
        update({ breakpoints: bps.breakpoints });
      });
    },

    addBreakpoint: async (spec) => {
      const { address, symbol } = parseAddress(spec);
      if (address === undefined && !symbol) return;
      await withBoard(async (board, update) => {
        await tryClient()!.debug.breakpoint({ board, op: 'add', kind: 'code', ...(symbol ? { symbol } : { address }) });
        const bps = await tryClient()!.debug.breakpoints({ board });
        update({ breakpoints: bps.breakpoints });
      });
    },

    removeBreakpoint: async (bp) => {
      await withBoard(async (board, update) => {
        const address = bp.kind === 'code' ? bp.pc : bp.address;
        await tryClient()!.debug.breakpoint({ board, op: 'remove', kind: bp.kind, address, length: bp.length });
        const bps = await tryClient()!.debug.breakpoints({ board });
        update({ breakpoints: bps.breakpoints });
      });
    },

    setRegister: async (name, value) => {
      await withBoard(async (board, update) => {
        const r = await tryClient()!.debug.set_register({ board, name, value: value.trim() });
        update({ regs: r.regs });
      });
    },

    loadMemory: async (address) => {
      const a = address === undefined ? get().memAddress : typeof address === 'number' ? address : parseAddress(address);
      await withBoard(async (board, update) => {
        const params = typeof a === 'number' ? { address: a } : a;
        const r = await tryClient()!.debug.memory({ board, ...params, length: 256 });
        const bytes = Uint8Array.from(atob(r.base64), (ch) => ch.charCodeAt(0));
        update({ memAddress: r.address, memory: bytes, memChosen: get().memChosen || address !== undefined });
      }, typeof address === 'string');
    },

    loadPeripherals: async () => {
      await withBoard(async (board, update) => {
        const r = await tryClient()!.debug.peripherals({ board });
        update({ peripherals: r.peripherals });
      }, false);
    },
  };
});
