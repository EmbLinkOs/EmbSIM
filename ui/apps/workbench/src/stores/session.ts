// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The session store: connection state, engine facts, the virtual clock, run
// state and the live mirror of observe.update (nets and outputs). Nothing in
// here is computed from the circuit; it is what the server said last.

import type { BoardInfo, Capability, DiagnosticRecord, InputEntry, NetState, ObserveUpdate, OutputValue, Policy, Time, TimingTier } from '@embsim/protocol';
import { create } from 'zustand';

export type ConnectionStatus = 'idle' | 'connecting' | 'open' | 'closed' | 'error';

export interface SessionState {
  status: ConnectionStatus;
  transportLabel: string;
  isDemo: boolean;
  error: string | null;
  engine: string | null;
  protocol: string | null;
  capabilities: Capability[];
  projectName: string | null;
  now: Time;
  running: boolean;
  speed: number;
  policy: Policy;
  boards: BoardInfo[];
  timingTiers: Record<string, TimingTier>;
  latencyMs: number;
  nets: Record<string, NetState>;
  outputs: Record<string, OutputValue>;
  inputs: InputEntry[];
  diagnostics: DiagnosticRecord[];
  unreadDiagnostics: number;
  lastControlReason: string | null;
}

export interface SessionActions {
  setConnection(patch: Partial<Pick<SessionState, 'status' | 'transportLabel' | 'isDemo' | 'error'>>): void;
  setInfo(patch: Partial<SessionState>): void;
  applyObserve(update: ObserveUpdate): void;
  applyControlState(state: { running: boolean; now_ps: string; reason?: string }): void;
  addDiagnostics(records: DiagnosticRecord[]): void;
  markDiagnosticsRead(): void;
  clearDiagnostics(): void;
  setLatency(ms: number): void;
  setInputs(inputs: InputEntry[]): void;
  setInputValue(target: string, input: string, value: unknown): void;
  hasCapability(c: Capability): boolean;
}

export const useSession = create<SessionState & SessionActions>((set, get) => ({
  status: 'idle',
  transportLabel: '',
  isDemo: false,
  error: null,
  engine: null,
  protocol: null,
  capabilities: [],
  projectName: null,
  now: 0n,
  running: false,
  speed: 1,
  policy: 'electrical',
  boards: [],
  timingTiers: {},
  latencyMs: 0,
  nets: {},
  outputs: {},
  inputs: [],
  diagnostics: [],
  unreadDiagnostics: 0,
  lastControlReason: null,

  setConnection: (patch) => set(patch),
  setInfo: (patch) => set(patch),
  applyObserve: (u) =>
    set((s) => {
      // Outputs arrive flat (`"D1.lit": true`) per SPEC-008 or nested under
      // components (0.1 server); both are mirrored into the flat map.
      let outputs = u.outputs ? { ...s.outputs, ...u.outputs } : s.outputs;
      let inputs = s.inputs;
      if (u.components) {
        outputs = { ...outputs };
        for (const [id, comp] of Object.entries(u.components)) {
          for (const [name, value] of Object.entries(comp.outputs ?? {})) outputs[`${id}.${name}`] = value;
          if (comp.inputs) {
            inputs = inputs.map((i) => (i.target === id && i.input in comp.inputs! && i.value !== comp.inputs![i.input] ? { ...i, value: comp.inputs![i.input] } : i));
          }
        }
      }
      return {
        now: BigInt(u.now_ps),
        nets: u.nets ? { ...s.nets, ...u.nets } : s.nets,
        outputs,
        inputs,
        running: u.running ?? s.running,
        speed: u.speed ?? s.speed,
        policy: u.policy ?? s.policy,
      };
    }),
  applyControlState: (c) => set({ running: c.running, now: BigInt(c.now_ps), lastControlReason: c.reason ?? null }),
  addDiagnostics: (records) =>
    set((s) => ({
      diagnostics: [...s.diagnostics, ...records].slice(-500),
      unreadDiagnostics: s.unreadDiagnostics + records.length,
    })),
  markDiagnosticsRead: () => set({ unreadDiagnostics: 0 }),
  clearDiagnostics: () => set({ diagnostics: [], unreadDiagnostics: 0 }),
  setLatency: (ms) => set((s) => ({ latencyMs: s.latencyMs === 0 ? ms : s.latencyMs * 0.7 + ms * 0.3 })),
  setInputs: (inputs) => set({ inputs }),
  setInputValue: (target, input, value) =>
    // a value set on the part itself: it no longer follows the world
    set((s) => ({ inputs: s.inputs.map((i) => (i.target === target && i.input === input ? { ...i, value, source: undefined } : i)) })),
  hasCapability: (c) => get().capabilities.includes(c),
}));

/** Severity counts for the toolbar badge. */
export function diagnosticSummary(records: DiagnosticRecord[]): { errors: number; warnings: number; infos: number } {
  let errors = 0;
  let warnings = 0;
  let infos = 0;
  for (const r of records) {
    if (r.severity === 'error' || r.severity === 'fatal') errors++;
    else if (r.severity === 'warning') warnings++;
    else infos++;
  }
  return { errors, warnings, infos };
}
