// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The method table of SPEC-008 §5. `Methods` gives every method its params
// and result types; `METHOD_NAMES` is the runtime list the client is
// generated from. Adding a method means adding it to both: the type test at
// the bottom fails when they disagree.

import type {
  ProjectBoard,
  BoardInfo,
  Breakpoint,
  CatalogItem,
  ComponentDescription,
  CoverageReport,
  DiagnosticRecord,
  DurationString,
  InputEntry,
  Instruction,
  Layout,
  ObserveUpdate,
  Peripheral,
  Policy,
  ProbeInfo,
  ProbeKind,
  ProfileReport,
  StackReport,
  FaultReport,
  Project,
  Recording,
  ReplayResult,
  PsString,
  Register,
  Report,
  Scenario,
  SessionInfo,
  SessionOpenParams,
  SessionOpenResult,
  Snapshot,
  SourceFile,
  Topology,
  WaveformSamples,
  Where,
  ControlState,
  WorldState,
  HostPort,
} from './types';

export type ProjectEdit =
  | { add_component: { id: string; component: string; version?: string; params?: Record<string, unknown>; layout?: Layout } }
  | { remove_component: { id: string } }
  | { set_param: { id: string; param: string; value: unknown } }
  | { connect: { net: string; pin: string } }
  | { disconnect: { pin: string } }
  | { rename_net: { from: string; to: string } }
  | { add_board: { id: string; board: string; layout?: Layout; firmware?: ProjectBoard['firmware'] } }
  | { remove_board: { id: string } }
  | { set_world: { fields?: NonNullable<Project['world']>['fields']; bodies?: NonNullable<Project['world']>['bodies']; room?: NonNullable<Project['world']>['room']; sun?: NonNullable<Project['world']>['sun'] } }
  | { set_layout: { id?: string; layout?: Layout; wires?: Project['layout'] extends infer L ? (L extends { wires?: infer W } ? W : never) : never; labels?: NonNullable<Project['layout']>['labels']; buses?: NonNullable<Project['layout']>['buses']; groups?: NonNullable<Project['layout']>['groups']; bench?: NonNullable<Project['layout']>['bench'] } };

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
    result: { tx?: string; applied?: number | boolean; restarted?: boolean; diagnostics?: DiagnosticRecord[]; islands?: number };
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

  // the world (ADR-026)
  'world.get': { params: Record<string, never>; result: WorldState };

  // a board's serial port on the host (ADR-031)
  'port.open': { params: { target: string }; result: HostPort };
  'port.close': { params: { target: string }; result: Record<string, never> };
  'port.list': { params: Record<string, never>; result: { ports: HostPort[] } };

  // probe and waveform
  'probe.add': {
    params: { target: string; kind: ProbeKind; rate_hz?: number; decode?: { protocol: string; params?: Record<string, unknown> } };
    result: { probe: string | number; id?: string; stream?: string };
  };
  'probe.remove': { params: { probe: string }; result: Record<string, never> };
  'probe.list': { params: Record<string, never>; result: { probes: ProbeInfo[] } };
  'probe.read': {
    params: { probe: string; from_ps?: PsString; to_ps?: PsString; max?: number };
    result: { samples: [PsString, number][]; gap?: boolean; dropped?: number; kind?: ProbeKind };
  };
  /** SPEC-008 gives `{stream}`; the 0.1 server answers `{streams: [...], encoding}`. */
  'waveform.subscribe': { params: { probes: string[]; max_rate_hz?: number }; result: { stream?: string | number; streams?: (string | number)[]; encoding?: string } };
  'waveform.unsubscribe': { params: { stream: string | number }; result: Record<string, never> };

  // observe
  'observe.snapshot': {
    params: { include?: ('nets' | 'components' | 'boards' | 'outputs' | 'probes')[] };
    result: Snapshot;
  };
  'observe.subscribe': {
    params: { what: ('nets' | 'outputs' | 'diagnostics' | 'clock')[]; max_rate_hz?: number };
    result: Record<string, never>;
  };

  // debug (SPEC-008 §5, SPEC-009 §10)
  'debug.breakpoint': {
    params: {
      board: string;
      op: 'add' | 'remove';
      kind?: 'code' | 'read' | 'write' | 'access';
      address?: number | string;
      symbol?: string;
      file?: string;
      line?: number;
      length?: number;
    };
    result: { id: string; address: number; addresses?: number[] };
  };
  'debug.breakpoints': { params: { board: string }; result: { breakpoints: Breakpoint[] } };
  'debug.where': { params: { board: string }; result: Where };
  'debug.halt': { params: { board: string }; result: Where };
  'debug.continue': { params: { board: string }; result: Where };
  'debug.step': { params: { board: string; count?: number }; result: Where };
  'debug.registers': { params: { board: string }; result: { regs: Register[] } };
  'debug.set_register': { params: { board: string; name: string; value: number | string }; result: { regs: Register[] } };
  'debug.memory': { params: { board: string; address?: number | string; symbol?: string; length: number }; result: { address: number; base64: string } };
  'debug.write_memory': { params: { board: string; address: number | string; base64: string }; result: { written: number } };
  'debug.disassemble': { params: { board: string; address?: number | string; symbol?: string; count?: number }; result: { insns: Instruction[] } };
  'debug.peripherals': { params: { board: string; name?: string }; result: { peripherals: Peripheral[] } };
  'debug.source': { params: { board: string; file: string }; result: SourceFile };
  'debug.symbol': { params: { board: string; symbol?: string; address?: number | string }; result: { address: number; pc: number; function?: string; offset?: number } };
  'debug.variables': { params: { board: string; frame?: number }; result: { variables: { name: string; type: string; value: string }[] } };
  'debug.gdb': { params: { board: string; op: 'start' | 'stop'; port?: number }; result: { port: number } };

  // catalog
  'catalog.list': { params: { kind?: string; category?: string; query?: string }; result: { items: CatalogItem[] } };
  'catalog.get': {
    params: { id: string; version?: string };
    result: { description: ComponentDescription; files: { symbol?: string; artwork?: string; model3d?: string } };
  };
  'catalog.validate': { params: { path: string }; result: { ok: boolean; diagnostics: DiagnosticRecord[] } };
  'catalog.libraries': { params: Record<string, never>; result: { libraries: { path: string; name: string; version: string; items: number }[] } };

  // analysis: the firmware's coverage, profile, stacks and faults (SPEC-008 §5)
  'analysis.start': {
    params: { board?: string; coverage?: boolean; profile?: boolean; stack?: boolean; faults?: boolean };
    result: { coverage: boolean; profile: boolean; stack: boolean; faults: boolean };
  };
  'analysis.stop': { params: Record<string, never>; result: Record<string, never> };
  'analysis.coverage': {
    params: { board?: string; format?: 'json' | 'text' | 'lcov'; detail?: boolean };
    result: { boards?: Record<string, CoverageReport>; text?: string };
  };
  'analysis.profile': {
    params: { board?: string; format?: 'json' | 'text' | 'collapsed' | 'collapsed-insns'; detail?: boolean };
    result: { boards?: Record<string, ProfileReport>; text?: string };
  };
  'analysis.stack': {
    params: { board?: string; format?: 'json' | 'text'; detail?: boolean };
    result: { boards?: Record<string, StackReport>; text?: string };
  };
  'analysis.faults': {
    params: { board?: string; format?: 'json' | 'text' };
    result: { boards?: Record<string, FaultReport>; text?: string };
  };

  // record
  'record.start': { params: { path?: string }; result: { from_ps?: PsString } };
  'record.stop': {
    params: Record<string, never>;
    result: { path?: string; inputs?: number | unknown[]; hash?: string; count?: number; observations?: number; recording?: Recording };
  };
  'record.replay': { params: ({ path: string } | { recording: Recording }) & { allow_drift?: boolean }; result: ReplayResult };
  'record.to_scenario': {
    params: ({ path: string } | { recording: Recording }) & { assert?: ('probes' | 'serial')[]; name?: string };
    result: { scenario: Scenario };
  };

  // toolchain
  'toolchain.detect': { params: Record<string, never>; result: { profiles: { name: string; found: boolean; version?: string; path?: string }[] } };
  'toolchain.build': {
    params: { board?: string; profile?: string };
    result: { ok: boolean; board: string; profile: string; log: string; image: string; steps: { argv: string[]; exit: number; output?: string }[] };
  };

  // scenario
  /** SPEC-008 wraps the report in `{report}`; the 0.1 server returns the report itself. */
  'scenario.run': { params: ({ path: string } | { scenario: Scenario | Record<string, unknown> }) & { until?: string }; result: { report: Report } | Report };
  'scenario.get': { params: { path: string }; result: { path: string; scenario: Scenario } };
  'scenario.save': { params: { path: string; scenario: Scenario; add?: boolean }; result: { path: string } };
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
  'world.get',
  'port.open',
  'port.close',
  'port.list',
  'probe.add',
  'probe.remove',
  'probe.list',
  'probe.read',
  'waveform.subscribe',
  'waveform.unsubscribe',
  'observe.snapshot',
  'observe.subscribe',
  'debug.breakpoint',
  'debug.breakpoints',
  'debug.where',
  'debug.halt',
  'debug.continue',
  'debug.step',
  'debug.symbol',
  'debug.source',
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
  'analysis.start',
  'analysis.stop',
  'analysis.coverage',
  'analysis.profile',
  'analysis.stack',
  'analysis.faults',
  'record.start',
  'record.stop',
  'record.replay',
  'record.to_scenario',
  'toolchain.detect',
  'toolchain.build',
  'scenario.run',
  'scenario.get',
  'scenario.save',
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
  'debug.stopped': Where;
  'serial.data': { target: string; probe?: string; base64: string };
  'project.changed': { tx: string; summary: string };
  'record.input': { at_ps: PsString; seq: number; target: string; input: string };
  'port.state': HostPort;
}

export type NotificationName = keyof Notifications;
export type NotificationParams<N extends NotificationName> = Notifications[N];

export type { BoardInfo };
