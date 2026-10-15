// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Wire types of SPEC-008. Times are decimal picosecond strings on the wire
// (`PsString`); the client converts them with `parsePs` at the boundary.

/** A decimal string of picoseconds as carried by the protocol. */
export type PsString = string;
/** A duration in a request: picoseconds or a unit string such as "10ms". */
export type DurationString = string;

export type Policy = 'functional' | 'timed' | 'electrical';
export type Severity = 'info' | 'warning' | 'error' | 'fatal';
export type LogicLevel = '0' | '1' | 'Z' | 'X';
export type IslandKind = 'digital' | 'analog' | 'mixed';
export type FidelityTier = 'functional' | 'timed' | 'electrical';
export type TimingTier = 'cycle' | 'instruction' | 'functional' | 'none';
export type ProbeKind = 'digital' | 'analog' | 'serial' | 'value';
export type SampleEncoding = 'u8' | 'f32' | 'f64' | 'bytes';

export type Capability =
  | 'analog'
  | 'lua'
  | 'gdb'
  | 'debug.dwarf'
  | 'streams.binary'
  | 'toolchain'
  | 'record'
  | 'scenario'
  | (string & {});

export interface DiagnosticRecord {
  severity: Severity;
  code: string;
  message: string;
  time_ps?: PsString;
  subject?: { kind: string; name: string };
  location?: { file?: string; line?: number; node?: string };
}

// --- project (SPEC-007) ---------------------------------------------------

export interface Layout {
  x: number;
  y: number;
  rotation?: number;
  mirror?: boolean;
}

export interface ProjectBoard {
  id: string;
  board: string;
  firmware?: { image?: string; format?: 'elf' | 'hex'; toolchain?: string; sources?: string[] };
  layout?: Layout;
}

export interface ProjectComponent {
  id: string;
  component: string;
  version?: string;
  params?: Record<string, number | string | boolean>;
  layout?: Layout;
}

export interface Wire {
  net: string;
  points: [number, number][];
}

export interface NetLabel {
  net: string;
  x: number;
  y: number;
}

export interface ProjectProbe {
  id: string;
  target: string;
  kind: ProbeKind;
  rate_hz?: number;
  decode?: { protocol: string; params?: Record<string, unknown> };
}

export interface Project {
  schema_version: 1;
  kind: 'project';
  name: string;
  description?: string;
  libraries?: string[];
  policy?: Policy;
  boards?: ProjectBoard[];
  components: ProjectComponent[];
  nets: Record<string, string[]>;
  layout?: { wires?: Wire[]; labels?: NetLabel[]; sheet?: { width: number; height: number } };
  probes?: ProjectProbe[];
  scenarios?: string[];
  settings?: Record<string, unknown>;
}

// --- catalog (SPEC-005) ---------------------------------------------------

export interface PinModel {
  kind: string;
  [k: string]: unknown;
}

export interface PinDescription {
  name: string;
  number?: number;
  model?: PinModel;
  aliases?: string[];
  doc?: string;
  required?: boolean;
  power_ref?: string;
}

export interface ParamDescription {
  type: 'number' | 'integer' | 'boolean' | 'string' | 'enum' | 'table';
  unit?: string;
  default?: number | string | boolean | [number, number][];
  min?: number;
  max?: number;
  enum?: string[];
  doc?: string;
  advanced?: boolean;
}

export interface InputDescription {
  name: string;
  type: 'boolean' | 'number' | 'enum' | 'string' | 'bytes';
  default?: unknown;
  min?: number;
  max?: number;
  unit?: string;
  enum?: string[];
  doc?: string;
}

export interface OutputDescription {
  name: string;
  type: 'boolean' | 'number' | 'bitmap' | 'string' | 'pattern';
  doc?: string;
}

export interface SymbolDescription {
  svg: string;
  width: number;
  height: number;
  pins: Record<string, { x: number; y: number; side: 'left' | 'right' | 'top' | 'bottom' }>;
}

export type HotspotGesture = 'hold' | 'toggle' | 'drag';

export interface Hotspot {
  input: string;
  selector: string;
  gesture: HotspotGesture;
  axis?: 'x' | 'y' | 'angle';
  min?: number;
  max?: number;
  step?: number;
  label?: string;
}

export interface ArtworkState {
  output: string;
  selector: string;
  attr?: string;
  class?: string;
  true?: string;
  false?: string;
  scale?: [number, number];
}

export interface ArtworkDescription {
  svg: string;
  width?: number;
  height?: number;
  hotspots?: Hotspot[];
  states?: ArtworkState[];
}

export interface FidelityDeclaration {
  claims: string;
  omissions?: string[];
  range?: Record<string, [number, number]>;
  validation?: string[];
}

export interface Provenance {
  author?: string;
  license?: string;
  sources?: { title: string; url?: string; date?: string }[];
}

export interface ComponentDescription {
  schema_version: 1;
  kind: 'component' | 'board';
  id: string;
  version: string;
  name: string;
  category: string;
  description?: string;
  keywords?: string[];
  pins: PinDescription[];
  params?: Record<string, ParamDescription>;
  inputs?: InputDescription[];
  outputs?: OutputDescription[];
  model?: Record<string, unknown>;
  functional?: Record<string, unknown>;
  symbol: SymbolDescription;
  artwork?: ArtworkDescription;
  fidelity: Partial<Record<FidelityTier, FidelityDeclaration | null>>;
  provenance: Provenance;
  tests?: string[];
}

export interface CatalogItem {
  id: string;
  version: string;
  name: string;
  category: string;
  fidelity_tiers: FidelityTier[];
  broken?: boolean;
}

// --- session ---------------------------------------------------------------

export interface SessionOpenParams {
  client: string;
  protocol: string;
  streams: 'binary' | 'base64' | 'pipe';
}

export interface SessionOpenResult {
  protocol: string;
  engine: string;
  abi: number;
  capabilities: Capability[];
}

export interface BoardInfo {
  id: string;
  board: string;
  mcu?: string;
  timing_tier: TimingTier;
  image?: string;
}

export interface SessionInfo {
  project: string | null;
  policy: Policy;
  now_ps: PsString;
  running: boolean;
  speed: number;
  boards: BoardInfo[];
  islands: number;
  limits?: Record<string, number>;
  timing_tiers?: Record<string, TimingTier>;
}

// --- topology / observation ----------------------------------------------

export interface NetState {
  /** Logic level of a digital or mixed net. */
  logic?: LogicLevel;
  /** Voltage of an analog node, volts. */
  voltage?: number;
  /** Driver conflict present. */
  contention?: boolean;
}

export interface TopologyNet {
  name: string;
  island: string;
  pins: string[];
  state?: NetState;
}

export interface TopologyIsland {
  id: string;
  kind: IslandKind;
  nets: string[];
}

export interface Topology {
  nets: TopologyNet[];
  islands: TopologyIsland[];
}

export type OutputValue = boolean | number | string;

export interface Snapshot {
  now_ps: PsString;
  nets?: Record<string, NetState>;
  components?: Record<string, { params?: Record<string, unknown>; inputs?: Record<string, unknown> }>;
  boards?: Record<string, { running?: boolean; pc?: string; halted?: boolean }>;
  /** Outputs keyed by "INSTANCE.output". */
  outputs?: Record<string, OutputValue>;
  probes?: ProbeInfo[];
}

export interface ObserveUpdate {
  now_ps: PsString;
  nets?: Record<string, NetState>;
  outputs?: Record<string, OutputValue>;
  boards?: Record<string, { running?: boolean; pc?: string; halted?: boolean }>;
}

export interface ControlState {
  running: boolean;
  now_ps: PsString;
  reason?: 'run' | 'pause' | 'stop' | 'end' | 'breakpoint' | 'fatal' | 'until';
}

// --- inputs / probes -----------------------------------------------------

export interface InputEntry {
  target: string;
  input: string;
  type: 'boolean' | 'number' | 'enum' | 'string' | 'bytes';
  value: unknown;
  min?: number;
  max?: number;
  unit?: string;
  enum?: string[];
  label?: string;
}

export interface ProbeInfo {
  probe: string;
  target: string;
  kind: ProbeKind;
  rate_hz?: number;
  stream?: string;
  decode?: { protocol: string; params?: Record<string, unknown> };
}

export interface WaveformSamples {
  stream: string;
  seq: number;
  first_time_ps: PsString;
  encoding: SampleEncoding;
  /** base64 payload. */
  data: string;
  /** For resampled analog streams: the sample period. */
  period_ps?: PsString;
  /** Probe id this stream belongs to (mirrors probe.add's stream mapping). */
  probe?: string;
  gap?: boolean;
}

// --- debug -----------------------------------------------------------------

export interface Register {
  name: string;
  value: string;
}

export interface Frame {
  pc: string;
  function?: string;
  file?: string;
  line?: number;
}

export interface Peripheral {
  name: string;
  base: string;
  registers: {
    name: string;
    offset: number;
    value: string;
    fields?: { name: string; bit: number; width: number; value: number; doc?: string }[];
  }[];
}

// --- reports (SPEC-008 §8) --------------------------------------------------

export interface Report {
  engine: string;
  protocol: string;
  project: { name: string; hash: string };
  images: { board: string; path: string; hash: string }[];
  policy: Policy;
  until_ps: PsString;
  ended: { reason: string; now_ps: PsString };
  assertions: { id: string; ok: boolean; observed: unknown; expected: unknown; at_ps: PsString }[];
  diagnostics: DiagnosticRecord[];
  statistics: {
    events: number;
    instructions: Record<string, number>;
    analog_steps: number;
    wall_time_s: number;
  };
}
