// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The method table of SPEC-008 §5. `Methods` gives every method its params
// and result types; `METHOD_NAMES` is the runtime list the client is
// generated from. Adding a method means adding it to both: the type test at
// the bottom fails when they disagree.

import type {
  BoardInfo,
  CatalogItem,
  ComponentDescription,
  DiagnosticRecord,
  DurationString,
  Frame,
  InputEntry,
  Layout,
  ObserveUpdate,
  Peripheral,
  Policy,
  ProbeInfo,
  ProbeKind,
  Project,
  PsString,
  Register,
  Report,
  SessionInfo,
  SessionOpenParams,
  SessionOpenResult,
  Snapshot,
  Topology,
  WaveformSamples,
  ControlState,
} from './types';

export type ProjectEdit =
  | { add_component: { id: string; component: string; version?: string; params?: Record<string, unknown>; layout?: Layout } }
  | { remove_component: { id: string } }
  | { set_param: { id: string; param: string; value: unknown } }
  | { connect: { net: string; pin: string } }
  | { disconnect: { pin: string } }
  | { rename_net: { from: string; to: string } }
  | { add_board: { id: string; board: string; layout?: Layout } }
  | { set_layout: { id?: string; layout?: Layout; wires?: Project['layout'] extends infer L ? (L extends { wires?: infer W } ? W : never) : never; labels?: NonNullable<Project['layout']>['labels'] } };

export interface Methods {
  // session
  'session.open': { params: SessionOpenParams; result: SessionOpenResult };
  'session.close': { params: Record<string, never>; result: Record<string, never> };
  'session.info': { params: Record<string, never>; result: SessionInfo };
  'session.policy': { params: { policy: Policy }; result: { policy: Policy } };

  // project
  'project.load': {
    params: { path: string } | { json: Project; base_dir?: string };
    result: { name: string; diagnostics: DiagnosticRecord[] };
  };
  'project.get': { params: Record<string, never>; result: { project: Project } };
  'project.save': { params: { path?: string }; result: { path: string } };
  'project.validate': { params: { path: string } | { json: Project }; result: { ok: boolean; diagnostics: DiagnosticRecord[] } };
  'project.transaction': {
    params:
      | { op: 'begin' }
      | { op: 'edit'; tx: string; edits: ProjectEdit[] }
      | { op: 'commit'; tx: string }
      | { op: 'abort'; tx: string };
    result: { tx?: string; applied?: number; diagnostics?: DiagnosticRecord[]; islands?: number };
  };
  'project.topology': { params: Record<string, never>; result: Topology };

  // control
  'control.run': { params: { speed?: number; until?: DurationString }; result: { running: true } };
  'control.pause': { params: Record<string, never>; result: { now_ps: PsString } };
  'control.step': { params: { mode: 'event' | 'instruction' | 'delta'; board?: string }; result: { now_ps: PsString } };
  'control.advance': { params: { until: DurationString } | { by: DurationString }; result: { now_ps: PsString } };
  'control.reset': { params: { reload_images?: boolean }; result: Record<string, never> };
  'control.speed': { params: { speed: number }; result: { speed: number } };

  // input
  'input.set': { params: { target: string; input: string; value: unknown; at?: DurationString }; result: { at_ps: PsString; seq: number } };
  'input.press': { params: { target: string }; result: { at_ps: PsString; seq: number } };
  'input.release': { params: { target: string }; result: { at_ps: PsString; seq: number } };
  'input.serial': { params: { target: string; bytes?: string; text?: string }; result: { at_ps: PsString; seq: number } };
  'input.list': { params: Record<string, never>; result: { inputs: InputEntry[] } };

  // probe and waveform
  'probe.add': {
    params: { target: string; kind: ProbeKind; rate_hz?: number; decode?: { protocol: string; params?: Record<string, unknown> } };
    result: { probe: string; stream?: string };
  };
  'probe.remove': { params: { probe: string }; result: Record<string, never> };
  'probe.list': { params: Record<string, never>; result: { probes: ProbeInfo[] } };
  'probe.read': {
    params: { probe: string; from_ps?: PsString; to_ps?: PsString; max?: number };
    result: { samples: [PsString, number][]; gap?: boolean };
  };
  'waveform.subscribe': { params: { probes: string[]; max_rate_hz?: number }; result: { stream: string } };
  'waveform.unsubscribe': { params: { stream: string }; result: Record<string, never> };

  // observe
  'observe.snapshot': {
    params: { include?: ('nets' | 'components' | 'boards' | 'outputs' | 'probes')[] };
    result: Snapshot;
  };
  'observe.subscribe': {
    params: { what: ('nets' | 'outputs' | 'diagnostics' | 'clock')[]; max_rate_hz?: number };
    result: Record<string, never>;
  };

  // debug
  'debug.breakpoint': {
    params: {
      board: string;
      op: 'add' | 'remove';
      kind?: 'code' | 'read' | 'write' | 'access';
      id?: string;
      address?: string;
      symbol?: string;
      file?: string;
      line?: number;
      length?: number;
    };
    result: { id: string };
  };
  'debug.registers': { params: { board: string }; result: { regs: Register[] } };
  'debug.set_register': { params: { board: string; name: string; value: string }; result: Record<string, never> };
  'debug.memory': { params: { board: string; address: string; length: number }; result: { base64: string } };
  'debug.write_memory': { params: { board: string; address: string; base64: string }; result: Record<string, never> };
  'debug.where': { params: { board: string }; result: { pc: string; function?: string; file?: string; line?: number; frames: Frame[] } };
  'debug.variables': { params: { board: string; frame?: number }; result: { variables: { name: string; type: string; value: string }[] } };
  'debug.disassemble': { params: { board: string; address: string; count: number }; result: { insns: { address: string; bytes: string; text: string }[] } };
  'debug.peripherals': { params: { board: string }; result: { peripherals: Peripheral[] } };
  'debug.gdb': { params: { board: string; op: 'start' | 'stop'; port?: number }; result: { port: number } };

  // catalog
  'catalog.list': { params: { kind?: string; category?: string; query?: string }; result: { items: CatalogItem[] } };
  'catalog.get': {
    params: { id: string; version?: string };
    result: { description: ComponentDescription; files: { symbol?: string; artwork?: string } };
  };
  'catalog.validate': { params: { path: string }; result: { ok: boolean; diagnostics: DiagnosticRecord[] } };
  'catalog.libraries': { params: Record<string, never>; result: { libraries: { path: string; name: string; version: string; items: number }[] } };

  // record
  'record.start': { params: { path?: string }; result: Record<string, never> };
  'record.stop': { params: Record<string, never>; result: { path: string; inputs: number; hash: string } };
  'record.replay': { params: { path: string }; result: { ok: boolean; divergence?: { at_ps: PsString; expected: unknown; observed: unknown } } };

  // toolchain
  'toolchain.detect': { params: Record<string, never>; result: { profiles: { name: string; found: boolean; version?: string; path?: string }[] } };
  'toolchain.build': { params: { board: string; profile?: string }; result: { ok: boolean; log: string; image?: string } };

  // scenario
  'scenario.run': { params: { path: string } | { scenario: Record<string, unknown> }; result: { report: Report } };
}

export type MethodName = keyof Methods;
export type Params<M extends MethodName> = Methods[M]['params'];
export type Result<M extends MethodName> = Methods[M]['result'];

/** Every method of SPEC-008 §5, in specification order. The client is generated from it. */
export const METHOD_NAMES = [
  'session.open',
  'session.close',
  'session.info',
  'session.policy',
  'project.load',
  'project.get',
  'project.save',
  'project.validate',
  'project.transaction',
  'project.topology',
  'control.run',
  'control.pause',
  'control.step',
  'control.advance',
  'control.reset',
  'control.speed',
  'input.set',
  'input.press',
  'input.release',
  'input.serial',
  'input.list',
  'probe.add',
  'probe.remove',
  'probe.list',
  'probe.read',
  'waveform.subscribe',
  'waveform.unsubscribe',
  'observe.snapshot',
  'observe.subscribe',
  'debug.breakpoint',
  'debug.registers',
  'debug.set_register',
  'debug.memory',
  'debug.write_memory',
  'debug.where',
  'debug.variables',
  'debug.disassemble',
  'debug.peripherals',
  'debug.gdb',
  'catalog.list',
  'catalog.get',
  'catalog.validate',
  'catalog.libraries',
  'record.start',
  'record.stop',
  'record.replay',
  'toolchain.detect',
  'toolchain.build',
  'scenario.run',
] as const satisfies readonly MethodName[];

// Compile-time check: the runtime list covers every key of `Methods`.
type Listed = (typeof METHOD_NAMES)[number];
type Missing = Exclude<MethodName, Listed>;
const _everyMethodListed: Missing extends never ? true : never = true;
void _everyMethodListed;

/** Notifications of SPEC-008 §6, server to client. */
export interface Notifications {
  diagnostics: { records: DiagnosticRecord[] };
  'observe.update': ObserveUpdate;
  'waveform.samples': WaveformSamples;
  'control.state': ControlState;
  'debug.stopped': { board: string; reason: string; pc: string; breakpoint?: string };
  'serial.data': { target: string; base64: string };
  'project.changed': { tx: string; summary: string };
  'record.input': { at_ps: PsString; seq: number; target: string; input: string };
}

export type NotificationName = keyof Notifications;
export type NotificationParams<N extends NotificationName> = Notifications[N];

export type { BoardInfo };
