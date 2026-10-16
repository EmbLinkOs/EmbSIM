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
  | 'debug'
  | 'mcu'
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

/** Presentation only (SPEC-007): nets drawn as one line; the first net is bit 0. */
export interface Bus {
  name: string;
  nets: string[];
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
  layout?: { wires?: Wire[]; labels?: NetLabel[]; buses?: Bus[]; sheet?: { width: number; height: number } };
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
  /** Numbers may carry SI suffixes ("1k") as in SPEC-005 §4. */
  min?: number | string;
  max?: number | string;
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
  /** A board's hotspot acts on one of its components ("SW_RESET"), not on the board. */
  target?: string;
  selector: string;
  gesture: HotspotGesture;
  axis?: 'x' | 'y' | 'angle';
  min?: number;
  max?: number;
  step?: number;
  label?: string;
  /** Keyboard equivalent named by the catalog (informational; the client maps gestures itself). */
  key?: string;
}

export interface ArtworkState {
  output: string;
  selector: string;
  attr?: string;
  class?: string;
  true?: string;
  false?: string;
  /** Numeric output mapping: `[lo, hi]` to 0..1, or `{from: [lo, hi], to: [a, b]}` (SPEC-005 §8). */
  scale?: [number, number] | { from: [number, number]; to?: [number, number] };
  /** Enum or integer value -> attribute value. */
  map?: Record<string, string>;
  /** A template whose "{}" takes the (scaled) value; attr "text" sets the element's text. */
  format?: string;
  /** Optional fixed decimal places after scaling, for numeric artwork text. */
  precision?: number;
}

export interface ArtworkDescription {
  svg: string;
  width?: number;
  height?: number;
  width_mm?: number;
  height_mm?: number;
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
  sources?: { title: string; url?: string; date?: string; rev?: string; hash?: string; notes?: string }[];
}

export interface ManufacturerPart {
  manufacturer: string;
  mpn: string;
  package: string;
  datasheet: string;
}

export interface ComponentDescription {
  schema_version: 1;
  kind: 'component' | 'board';
  id: string;
  version: string;
  name: string;
  category: string;
  part?: ManufacturerPart;
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
  kind?: string;
  description?: string;
  library?: string;
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
  /** The MCU description's id. */
  mcu?: string;
  /** The MCU's component (B1.U1), whose outputs carry its state and serial text. */
  component?: string;
  /** The core backend running its firmware, or "none". */
  runtime?: string;
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
  kind?: 'digital' | 'analog' | 'mixed';
  /** Logic level of a digital or mixed net. */
  logic?: LogicLevel;
  /** Voltage of an analog node, volts. */
  voltage?: number;
  /** Driver conflict present. */
  contention?: boolean;
  strength?: string;
  changed_at_ps?: PsString;
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
  components?: Record<string, ComponentObservation>;
  running?: boolean;
  policy?: Policy;
  speed?: number;
  boards?: Record<string, { running?: boolean; pc?: string; halted?: boolean }>;
  /** Outputs keyed by "INSTANCE.output". */
  outputs?: Record<string, OutputValue>;
  probes?: ProbeInfo[];
}

export interface ComponentObservation {
  kind?: string;
  model?: string;
  params?: Record<string, unknown>;
  inputs?: Record<string, unknown>;
  outputs?: Record<string, OutputValue>;
  pins?: Record<string, { net?: string; kind?: string; logic?: LogicLevel }>;
}

/**
 * observe.update: SPEC-008 §6 lists `now_ps, nets?, outputs?, boards?`; the
 * 0.1 server sends snapshot-shaped updates with `components` (outputs
 * nested per instance) and the run state. Both are accepted.
 */
export interface ObserveUpdate {
  now_ps: PsString;
  nets?: Record<string, NetState>;
  outputs?: Record<string, OutputValue>;
  boards?: Record<string, { running?: boolean; pc?: string; halted?: boolean }>;
  components?: Record<string, ComponentObservation>;
  running?: boolean;
  speed?: number;
  policy?: Policy;
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
  /** The probe id; the 0.1 server reports a numeric handle here and the id under `id`. */
  probe: string | number;
  id?: string;
  target: string;
  kind: ProbeKind;
  rate_hz?: number;
  stream?: string;
  decode?: { protocol: string; params?: Record<string, unknown> };
}

export interface WaveformSamples {
  stream: string | number;
  seq: number;
  encoding: SampleEncoding | 'json';
  /** Uniformly sampled frames: start time and base64 payload. */
  first_time_ps?: PsString;
  data?: string;
  /** `json` encoding: explicit [time_ps, value] pairs (event-driven probes). */
  samples?: [PsString, number][];
  kind?: ProbeKind;
  /** For resampled analog streams: the sample period. */
  period_ps?: PsString;
  /** Probe id this stream belongs to (mirrors probe.add's stream mapping). */
  probe?: string;
  gap?: boolean;
}

// --- debug -----------------------------------------------------------------
// Addresses and register values are numbers (32-bit, exact in JSON).

export interface Register {
  name: string;
  /** The register's number in GDB's target description. */
  regnum: number;
  value: number;
  /** Its width in bytes (4 for a Cortex-M's, 1 for an AVR's r0-r31). */
  size?: number;
}

export interface Frame {
  pc: number;
  function?: string;
  file?: string;
  line?: number;
}

/** Where a board's core is (debug.where, and the debug.stopped notification). */
export interface Where {
  board: string;
  pc: number;
  function?: string;
  offset?: number;
  /** The source line, from the image's DWARF. */
  file?: string;
  line?: number;
  /** Why the core is stopped: breakpoint, watchpoint, step, halt; absent while it runs. */
  stopped?: string;
  reason?: string;
  now_ps: string;
  instructions: number;
  cycles: number;
  /** The part's main RAM, where a memory view starts. */
  ram?: number;
  /** On a part with several cores, the one the registers and the program counter are (the one that runs next). */
  core?: number;
}

/** A source file the image's DWARF names (debug.source). */
export interface SourceFile {
  file: string;
  path: string;
  text: string;
  /** The lines that have code: where a breakpoint can go. */
  lines: number[];
}

export interface HitTotal {
  hit: number;
  total: number;
}

/** One board's coverage (analysis.coverage, SPEC-008 §5). */
export interface CoverageReport {
  lines: HitTotal;
  functions: HitTotal;
  insns: number;
  files: {
    path: string;
    lines: HitTotal;
    /** With `detail`: each function's entries; without, the file's counts. */
    functions: { name: string; line: number; count: number }[] | HitTotal;
    /** With `detail`: [line, count] for each line that has code. */
    counts?: [number, number][];
  }[];
  no_lines?: { name: string; count: number }[];
}

/** One board's profile (analysis.profile), by self cycles. */
export interface ProfileReport {
  insns: number;
  cycles: number;
  elapsed_cycles: number;
  functions: {
    name: string;
    self_insns: number;
    self_cycles: number;
    incl_insns: number;
    incl_cycles: number;
    calls: number;
  }[];
}

export interface Instruction {
  address: number;
  size: number;
  encoding: number;
  text: string;
  /** The symbol that starts here. */
  label?: string;
}

export interface Breakpoint {
  kind: 'code' | 'read' | 'write' | 'access';
  pc?: number;
  address?: number;
  length?: number;
  function?: string;
  offset?: number;
}

export interface Peripheral {
  name: string;
  base: number;
  /** The behavioural model bound to it, or "registers" for the register file alone. */
  model: string;
  /** Its clock is enabled (an access while it is off reads 0). */
  clock: boolean;
  /** It is held in reset (an access reads its reset values, a write is ignored). */
  reset?: boolean;
  registers: { name: string; offset: number; value: number }[];
}

// --- reports (SPEC-008 §8) --------------------------------------------------

export interface Report {
  engine: string;
  protocol: string;
  project: { name: string; hash?: string; path?: string };
  images?: { board: string; path: string; hash: string }[];
  policy: Policy;
  until_ps?: PsString;
  ended: { reason: string; now_ps: PsString };
  assertions: { id: string; ok: boolean; observed: unknown; expected: unknown; at_ps: PsString }[];
  diagnostics?: DiagnosticRecord[];
  statistics?: {
    events?: number;
    instructions?: Record<string, number>;
    analog_steps?: number;
    wall_time_s?: number;
  };
}
