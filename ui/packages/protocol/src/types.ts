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
  locked?: boolean;
}

export interface SchematicGroup {
  id: string;
  name: string;
  members: string[];
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
  rotation?: 0 | 90 | 180 | 270;
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
  layout?: { groups?: SchematicGroup[]; wires?: Wire[]; labels?: NetLabel[]; buses?: Bus[]; sheet?: { width: number; height: number }; bench?: BenchLayout };
  probes?: ProjectProbe[];
  scenarios?: string[];
  settings?: Record<string, unknown>;
  world?: WorldSpec;
}

/** The room the circuit sits in (ADR-026): fields every bound sensor reads, bodies distance sensors see. */
export type WorldField = 'temperature' | 'humidity' | 'pressure' | 'illuminance' | 'sound' | 'wind';

export const WORLD_FIELDS: { name: WorldField; label: string; unit: string; min: number; max: number; step: number; value: number }[] = [
  { name: 'temperature', label: 'Temperature', unit: 'degC', min: -40, max: 85, step: 0.1, value: 22 },
  { name: 'humidity', label: 'Humidity', unit: '%', min: 0, max: 100, step: 0.5, value: 50 },
  { name: 'pressure', label: 'Pressure', unit: 'hPa', min: 300, max: 1100, step: 0.25, value: 1013.25 },
  { name: 'illuminance', label: 'Light', unit: 'lx', min: 0, max: 100000, step: 1, value: 300 },
  { name: 'sound', label: 'Sound', unit: 'dB', min: 0, max: 130, step: 1, value: 40 },
  { name: 'wind', label: 'Wind', unit: 'm/s', min: 0, max: 40, step: 0.1, value: 0 },
];

export interface WorldBody {
  id: string;
  name?: string;
  shape?: 'box' | 'sphere';
  /** centre on the bench, mm: x right, y towards the viewer, z up */
  at: [number, number, number];
  size: [number, number, number];
  color?: string;
  /** motion in virtual time from `at`, mm/s */
  velocity_mm_s?: [number, number, number];
  /** with a velocity: back and forth over this distance from `at`, mm (0: straight on) */
  travel_mm?: number;
  /** waypoints (bench mm) walked at speed_mm_s, starting at the first */
  path_mm?: [number, number, number][];
  speed_mm_s?: number;
  /** along the path: round again from the last waypoint (else back the way it came) */
  loop?: boolean;
  /** warmer than the room (a person, an animal): a PIR sees it move */
  warm?: boolean;
}

/** the room around the bench, as the 3D bench draws it (presentation only) */
export interface WorldRoom {
  /** inside width, depth, height, mm */
  size_mm?: [number, number, number];
  /** the way the window looks out, degrees clockwise from north */
  window_azimuth_deg?: number;
}

/** where and when the room is, for the sun's position (presentation only) */
export interface WorldSun {
  latitude_deg?: number;
  /** MM-DD */
  date?: string;
  /** local solar time, hours */
  solar_time_h?: number;
}

export interface WorldSpec {
  fields?: Partial<Record<WorldField, number>>;
  bodies?: WorldBody[];
  room?: WorldRoom;
  sun?: WorldSun;
}

/** world.get: the room as it is now */
/** A board's console (or a terminal part) as a serial port on the host (ADR-031). */
export interface HostPort {
  target: string;
  /** what a program on the host opens: /dev/pts/3 */
  path: string;
  /** a program has the port open (DTR raised) */
  connected: boolean;
  /** the program's line rate, 0 while none has it open */
  baud: number;
  /** the console's rate as the firmware set it, 0 when unknown */
  sketch_baud: number;
  to_host: number;
  from_host: number;
}

export interface WorldState {
  fields: Record<WorldField, { value: number; unit: string; set: boolean }>;
  bodies: WorldBody[];
}

/** Where a part sits on the bench (SPEC-007 §2): its centre in mm and its turn, clockwise. */
export interface BenchPlace {
  x: number;
  y: number;
  rotation?: 0 | 90 | 180 | 270;
}

/** A jumper wire between two terminals (INSTANCE.PIN, a breadboard's hole, a board's port). */
export interface BenchWire {
  from: string;
  to: string;
  color?: string;
  /** Drawing only. Intermediate [X, height, Y] points, in bench millimetres. */
  points?: [number, number, number][];
  /** Automatic routing height above the higher terminal. Ignored with explicit points. */
  rise_mm?: number;
}

export interface BenchNote {
  x: number;
  y: number;
  text: string;
  to?: [number, number];
}

/** The bench: another drawing of the project's nets (ADR-027, SPEC-007 §2). */
export interface BenchLayout {
  parts?: Record<string, BenchPlace>;
  wires?: BenchWire[];
  notes?: BenchNote[];
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
  required?: boolean;
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
  /** the world's field the input reads while it is not set (ADR-026) */
  world?: WorldField | 'distance';
}

export interface OutputDescription {
  name: string;
  type: 'boolean' | 'number' | 'bitmap' | 'string' | 'pattern';
  unit?: string;
  doc?: string;
}

export interface SymbolDescription {
  /** State/interaction bindings in schematic symbol coordinates. */
  states?: ArtworkState[];
  hotspots?: Hotspot[];
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
  range?: [number, number];
  travel?: number;
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

export type BenchPackage = 'breadboard' | 'axial' | 'radial' | 'led' | 'to92' | 'to220' | 'sot23' | 'sot223' | 'dip' | 'tactile' | 'switch' | 'pot' | 'module' | 'board' | 'box';

/** The part as it sits on the bench (SPEC-005 §8.1): millimetres from its top-left corner seen from above. */
export interface LedBody {
  diameter_mm: number;
  flange_mm: number;
  body_height_mm: number;
  standoff_mm: number;
}

export interface IndicatorQualification {
  scope: 'nominal-dc';
  temperature_c: number;
  current_a: [number, number];
  voltage_tolerance_v: number;
  source: { url: string; revision: string; sha256: string; pages: number[] };
  curves: { pin: string; points: { current_a: number; voltage_v: number; role: 'calibration' | 'holdout'; fixture: string }[] }[];
}

export interface BenchDescription {
  package?: BenchPackage;
  size_mm?: [number, number, number];
  pins?: Record<string, [number, number]>;
  wiring_aid?: boolean;
  led?: LedBody;
  color?: string;
  label?: string;
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
  qualification?: IndicatorQualification;
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
  /** Pins joined inside the part (a breadboard's strips, SPEC-005 §8.1). */
  ties?: string[][];
  bench?: BenchDescription;
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
  /** Active digital receivers, including terminals inside board instances. */
  receivers?: string[];
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
  /** read from the world (ADR-026) */
  world?: string;
  source?: 'world';
}

export interface ProbeInfo {
  /** The probe id; the 0.1 server reports a numeric handle here and the id under `id`. */
  probe: string | number;
  id?: string;
  target: string;
  kind: ProbeKind;
  unit?: string;
  rate_hz?: number;
  stream?: string;
  decode?: { protocol: string; params?: Record<string, unknown> };
}

export interface WaveformSamples {
  stream: string | number;
  seq: number;
  encoding: SampleEncoding | 'json';
  /** Producer's completed capture frontier; partial chunks stop at their last sample. */
  through_time_ps?: PsString;
  /** A new subscription/reset replaces the preceding capture for this probe. */
  restart?: boolean;
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

/** One board's stacks (analysis.stack): addresses as the core's stack
 * pointer holds them (a PIC24's W15, which grows up). */
export interface StackReport {
  stacks: {
    core: number;
    /** "msp" or "psp" on a Cortex-M, else "stack". */
    name: string;
    top?: number;
    deepest?: number;
    used?: number;
    /** The link's limit, the end of .data and .bss, or the RAM's end. */
    limit?: number;
    limit_from?: string;
    size?: number;
  }[];
  /** The first time the main stack went past its limit (mcu.stack_overflow). */
  overflow?: { core: number; pc: number; function: string; sp: number; limit: number; limit_from: string };
  /** By depth: the deepest point, the function's own frame and its depth with its callees, in bytes. */
  functions: { name: string; deepest: number; frame: number; incl: number }[];
}

/** One board's faults (analysis.faults) as its cores took them, the first sixteen. */
export interface FaultReport {
  count: number;
  faults: {
    core: number;
    /** The exception's number as the core gives it (absent for a lockup). */
    exception?: number;
    lockup?: boolean;
    /** "HardFault", "load access fault", "address error trap". */
    name: string;
    pc: number;
    /** "fn+0x6 (file.c:12)" */
    where: string;
    /** One line, as the mcu.fault diagnostic gives it. */
    summary: string;
    handler?: string;
    backtrace: { pc: number; where: string }[];
    /** The whole report: registers decoded, the instruction, the backtrace. */
    text: string;
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
  assertions: { id: string; ok: boolean; observed: unknown; expected: unknown; at_ps: PsString; measure?: 'mean' | 'rms' | 'duty_cycle'; unit?: string; window_ps?: { from: PsString; to: PsString } }[];
  diagnostics?: DiagnosticRecord[];
  statistics?: {
    events?: number;
    instructions?: Record<string, number>;
    analog_steps?: number;
    wall_time_s?: number;
  };
  /** With the analyses on: each board's coverage and first functions. */
  coverage?: Record<string, CoverageReport>;
  profile?: Record<string, ProfileReport>;
  stack?: Record<string, StackReport>;
  faults?: Record<string, FaultReport>;
}

/** An input of a scenario (SPEC-007 §2): at a time, a target's input set. */
export interface ScenarioInput {
  at: string;
  target: string;
  input: string;
  value: unknown;
}

/** An assertion of a scenario: when (`at`, `between`, `by`, `always`), on
 * what (`probe`, or `diagnostics.forbid`), and what is expected. */
export interface ScenarioAssertion {
  id: string;
  at?: string;
  between?: [string, string];
  by?: string;
  always?: boolean;
  probe?: string;
  measure?: 'mean' | 'rms' | 'duty_cycle';
  expect?: Record<string, unknown>;
  diagnostics?: { forbid: string[] };
}

export interface Scenario {
  schema_version: number;
  kind: 'scenario';
  name: string;
  description?: string;
  until?: string;
  policy?: Policy;
  inputs: ScenarioInput[];
  assertions: ScenarioAssertion[];
}

/** A recording (SPEC-012 §1), as record.stop returns it without a path. */
export interface Recording {
  schema_version: number;
  kind: 'recording';
  project: { path: string; hash: string };
  inputs: unknown[];
  observations: { at_ps: PsString; kind: string; [k: string]: unknown }[];
  end: { at_ps: PsString; reason: string };
  [k: string]: unknown;
}

/** record.replay's result (SPEC-012 §2). */
export interface ReplayResult {
  ok: boolean;
  compared?: number;
  divergence?: { index: number; expected: unknown; observed: unknown };
  drift?: { what: string; recorded: string; now: string }[];
}
