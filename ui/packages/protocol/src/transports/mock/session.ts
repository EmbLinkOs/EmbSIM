// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The fake session behind MockTransport. It is not a simulator: it holds a
// scripted response of the demo circuit (a few operating points joined by
// exponentials) so the workbench can be developed and demonstrated before
// `embsim serve` exists. Everything the UI shows still arrives through the
// protocol, as observe.update and waveform.samples notifications.

import { bytesToBase64 } from '../../jsonrpc';
import { MS, US, parseDuration, type Time } from '../../time';
import { hierarchyOf } from '../../hierarchy';
import type {
  ControlState,
  DiagnosticRecord,
  InputEntry,
  NetState,
  ObserveUpdate,
  OutputValue,
  Policy,
  ProbeInfo,
  Project,
  ProjectComponent,
  Snapshot,
  Topology,
  WaveformSamples,
  WorldBody,
  WorldField,
  WorldState,
} from '../../types';
import { WORLD_FIELDS } from '../../types';
import type { ProjectEdit } from '../../methods';
import { demoProject } from './project';

export interface Notify {
  (method: 'observe.update', params: ObserveUpdate): void;
  (method: 'control.state', params: ControlState): void;
  (method: 'waveform.samples', params: WaveformSamples): void;
  (method: 'diagnostics', params: { records: DiagnosticRecord[] }): void;
  (method: 'project.changed', params: { tx: string; summary: string }): void;
  (method: 'record.input', params: { at_ps: string; seq: number; target: string; input: string }): void;
}

/** Operating points of the demo circuit. */
const POINT = {
  released: { btn: 3.3, q1b: 0.72, coll: 0.15, ic: 5.7e-3 },
  pressed: { btn: 0, q1b: 0, coll: 3.3, ic: 0 },
} as const;
type Quantity = keyof typeof POINT.released;
const TAU: Record<Quantity, number> = { btn: 2e-7, q1b: 5e-6, coll: 2e-5, ic: 2e-5 }; // seconds

interface Edge {
  t: Time;
  pressed: boolean;
  /** Analog values at the instant of the edge (continuity of the exponentials). */
  at: Record<Quantity, number>;
}

interface Stream {
  id: string;
  probes: string[];
  cursor: Time;
  seq: number;
  maxRateHz: number;
}

const TICK_MS = 33;
const MAX_SAMPLES_PER_FRAME = 60_000;

export class MockSession {
  project: Project = demoProject();
  now: Time = 0n;
  running = false;
  speed = 1;
  policy: Policy = 'electrical';
  until: Time | null = null;
  private edges: Edge[] = [{ t: 0n, pressed: false, at: { ...POINT.released } }];
  private pressed = false;
  private inputSeq = 0;
  private probes = new Map<string, ProbeInfo>();
  private streams = new Map<string, Stream>();
  private nextStream = 1;
  private txs = new Map<string, Project>();
  private nextTx = 1;
  private observeWhat = new Set<string>();
  private observeRateHz = 30;
  private lastObserve: { nets: Record<string, NetState>; outputs: Record<string, OutputValue> } = { nets: {}, outputs: {} };
  private lastObserveWall = 0;
  private timer: ReturnType<typeof setInterval> | null = null;
  private lastTickWall = 0;
  readonly notify: Notify;

  constructor(notify: Notify) {
    this.notify = notify;
    for (const p of this.project.probes ?? []) {
      this.probes.set(p.id, { probe: p.id, target: p.target, kind: p.kind, rate_hz: p.rate_hz });
    }
  }

  dispose(): void {
    this.stopTimer();
  }

  // --- model -----------------------------------------------------------------

  private edgeAt(t: Time): Edge {
    // Edges are sorted; the list is short (a few per press), so scan backwards.
    for (let i = this.edges.length - 1; i >= 0; i--) {
      const e = this.edges[i]!;
      if (e.t <= t) return e;
    }
    return this.edges[0]!;
  }

  private pressedAt(t: Time): boolean {
    return this.edgeAt(t).pressed;
  }

  private valueAt(q: Quantity, t: Time): number {
    const e = this.edgeAt(t);
    const target = (e.pressed ? POINT.pressed : POINT.released)[q];
    const dt = Number(t - e.t) * 1e-12;
    return target + (e.at[q] - target) * Math.exp(-dt / TAU[q]);
  }

  private addEdge(t: Time, pressed: boolean): void {
    const at: Record<Quantity, number> = { btn: 0, q1b: 0, coll: 0, ic: 0 };
    for (const q of Object.keys(at) as Quantity[]) at[q] = this.valueAt(q, t);
    // Drop any scheduled future edges past t (a new input supersedes a bounce burst).
    while (this.edges.length > 1 && this.edges[this.edges.length - 1]!.t > t) this.edges.pop();
    this.edges.push({ t, pressed, at });
  }

  private setPressed(at: Time, pressed: boolean): void {
    if (this.pressed === pressed && this.edgeAt(at).pressed === pressed) return;
    this.pressed = pressed;
    const sw = this.project.components.find((c) => c.id === 'SW1');
    const bounce = sw?.params?.['bounce'] === true;
    const bounceMs = Number(sw?.params?.['bounce_ms'] ?? 2);
    if (bounce && bounceMs > 0) {
      const spacing = BigInt(Math.max(1, Math.round((bounceMs * 1e9) / 8)));
      let t = at;
      let state = pressed;
      for (let i = 0; i < 7; i++) {
        this.addEdge(t, state);
        state = !state;
        t += spacing;
      }
      this.addEdge(t, pressed);
    } else {
      this.addEdge(at, pressed);
    }
  }

  // --- control ---------------------------------------------------------------

  run(speed?: number, until?: string): void {
    if (speed !== undefined) this.speed = speed;
    this.until = until !== undefined ? this.now + parseDuration(until) : null;
    if (this.running) return;
    this.running = true;
    this.lastTickWall = Date.now();
    this.timer = setInterval(() => this.tick(), TICK_MS);
    this.notify('control.state', { running: true, now_ps: this.now.toString(), reason: 'run' });
  }

  pause(reason: ControlState['reason'] = 'pause'): Time {
    if (this.running) {
      this.running = false;
      this.stopTimer();
      this.notify('control.state', { running: false, now_ps: this.now.toString(), reason });
    }
    this.emitObserve(true);
    return this.now;
  }

  private stopTimer(): void {
    if (this.timer) clearInterval(this.timer);
    this.timer = null;
  }

  private tick(): void {
    const wall = Date.now();
    const dtMs = Math.min(wall - this.lastTickWall, 250);
    this.lastTickWall = wall;
    const factor = this.speed > 0 ? this.speed : 20;
    let step = BigInt(Math.round(dtMs * factor * 1e6)) * 1000n; // ms → ps
    if (step > 200n * MS) step = 200n * MS;
    let target = this.now + step;
    if (this.until !== null && target >= this.until) {
      target = this.until;
      this.advanceTo(target);
      this.pause('until');
      return;
    }
    this.advanceTo(target);
    this.emitObserve(false);
  }

  advanceTo(t: Time): void {
    if (t < this.now) throw new EngineError(-32022, 'cannot advance into the past');
    this.now = t;
    this.flushStreams();
  }

  step(mode: 'event' | 'instruction' | 'delta'): Time {
    const by = mode === 'delta' ? 1n : mode === 'instruction' ? 14n * 1000n : 1n * US;
    this.advanceTo(this.now + by);
    this.emitObserve(true);
    return this.now;
  }

  advance(params: { until?: string; by?: string }): Time {
    const target = params.until !== undefined ? parseDuration(params.until) : this.now + parseDuration(params.by ?? '0');
    if (target < this.now) throw new EngineError(-32022, `until ${target} ps is in the past (now ${this.now} ps)`);
    // Advance in slices so observations stream during long advances.
    const slice = 50n * MS;
    let t = this.now;
    while (t < target) {
      t = t + slice < target ? t + slice : target;
      this.advanceTo(t);
      this.emitObserve(false);
    }
    this.emitObserve(true);
    return this.now;
  }

  reset(): void {
    this.pause('stop');
    this.now = 0n;
    this.probeStarts.clear();
    for (const p of this.probes.values()) p.capture_epoch = (p.capture_epoch ?? 0) + 1;
    this.pressed = false;
    this.edges = [{ t: 0n, pressed: false, at: { ...POINT.released } }];
    for (const s of this.streams.values()) {
      s.cursor = 0n;
      s.seq = 0;
    }
    this.lastObserve = { nets: {}, outputs: {} };
    this.notify('control.state', { running: false, now_ps: '0', reason: 'stop' });
    this.emitObserve(true);
  }

  setSpeed(speed: number): number {
    this.speed = speed;
    return speed;
  }

  setPolicy(policy: Policy): Policy {
    if (this.running) throw new EngineError(-32016, 'policy can only change while paused');
    this.policy = policy;
    this.project.policy = policy;
    return policy;
  }

  // --- inputs ----------------------------------------------------------------

  listInputs(): InputEntry[] {
    const entries: InputEntry[] = [];
    for (const c of this.project.components) {
      if (c.component === 'emblink/push_button') {
        entries.push({ target: c.id, input: 'pressed', type: 'boolean', value: c.id === 'SW1' ? this.pressed : false, label: `${c.id} pressed` });
      }
    }
    return entries;
  }

  setInput(target: string, input: string, value: unknown, at?: string): { at_ps: string; seq: number } {
    const t = at !== undefined ? parseDuration(at) : this.now;
    if (t < this.now) throw new EngineError(-32022, `input at ${t} ps is in the past`);
    if (target === 'SW1' && input === 'pressed') {
      this.setPressed(t, Boolean(value));
    } else if (target === 'world' || target.startsWith('world.')) {
      this.setWorld(target, input, value);
    } else if (!this.project.components.some((c) => c.id === target)) {
      throw new EngineError(-32002, `no component ${target}`);
    }
    const seq = ++this.inputSeq;
    this.notify('record.input', { at_ps: t.toString(), seq, target, input });
    this.emitObserve(true);
    return { at_ps: t.toString(), seq };
  }

  // --- the world (ADR-026) ------------------------------------------------------

  private world: WorldState = initialWorld();

  resetWorld(): void {
    this.world = initialWorld(this.project.world);
  }

  private setWorld(target: string, input: string, value: unknown): void {
    if (target === 'world') {
      const f = this.world.fields[input as WorldField];
      if (!f) throw new EngineError(-32002, `the world has no field ${input}`);
      if (value === null) f.set = false;
      else if (typeof value === 'number') Object.assign(f, { value, set: true });
      else throw new EngineError(-32602, 'a world field takes a number');
      return;
    }
    if (input !== 'body') throw new EngineError(-32002, `a world body's input is body`);
    const id = target.slice('world.'.length);
    this.world.bodies = this.world.bodies.filter((b) => b.id !== id);
    if (value && typeof value === 'object') this.world.bodies.push({ ...(value as WorldBody), id });
  }

  getWorld(): WorldState {
    return structuredClone(this.world);
  }

  // --- observation -----------------------------------------------------------

  private netStates(t: Time): Record<string, NetState> {
    const pressed = this.pressedAt(t);
    const digital = this.policy === 'functional';
    const btnV = this.valueAt('btn', t);
    const states: Record<string, NetState> = {
      '3V3': digital ? { logic: '1' } : { logic: '1', voltage: 3.3 },
      GND: digital ? { logic: '0' } : { logic: '0', voltage: 0 },
      BTN: digital ? { logic: pressed ? '0' : '1' } : { logic: btnV > 1.65 ? '1' : '0', voltage: round(btnV) },
      Q1B: digital ? { logic: pressed ? '0' : '1' } : { voltage: round(this.valueAt('q1b', t)) },
      COLL: digital ? { logic: pressed ? '1' : '0' } : { voltage: round(this.valueAt('coll', t)) },
      LED_K: digital ? { logic: pressed ? '1' : '0' } : { voltage: round(pressed ? 3.3 : 3.3 - 220 * this.valueAt('ic', t)) },
    };
    // Nets added by the user through transactions exist but float.
    for (const name of Object.keys(this.project.nets)) if (!(name in states)) states[name] = { logic: 'Z' };
    return states;
  }

  private outputs(t: Time): Record<string, OutputValue> {
    const ic = this.valueAt('ic', t);
    const lum = Math.max(0, Math.min(1, ic / 0.0057));
    return {
      'SW1.closed': this.pressedAt(t),
      'D1.lit': lum > 0.05,
      'D1.luminance': round(lum),
      'Q1.ic': round(ic, 6),
    };
  }

  snapshot(include?: string[]): Snapshot {
    const want = (k: string) => !include || include.includes(k);
    const snap: Snapshot = { now_ps: this.now.toString() };
    if (want('nets')) snap.nets = this.netStates(this.now);
    if (want('outputs')) snap.outputs = this.outputs(this.now);
    if (want('components')) {
      snap.components = {};
      for (const c of this.project.components) {
        snap.components[c.id] = { params: c.params ?? {}, inputs: c.id === 'SW1' ? { pressed: this.pressed } : {} };
      }
    }
    if (want('boards')) snap.boards = {};
    if (want('probes')) snap.probes = [...this.probes.values()];
    return snap;
  }

  subscribeObserve(what: string[], maxRateHz?: number): void {
    for (const w of what) this.observeWhat.add(w);
    if (maxRateHz) this.observeRateHz = Math.max(1, Math.min(60, maxRateHz));
    this.emitObserve(true);
  }

  private emitObserve(force: boolean): void {
    if (this.observeWhat.size === 0) return;
    const wall = Date.now();
    if (!force && wall - this.lastObserveWall < 1000 / this.observeRateHz) return;
    this.lastObserveWall = wall;
    const update: ObserveUpdate = { now_ps: this.now.toString() };
    if (this.observeWhat.has('nets')) {
      const nets = this.netStates(this.now);
      const changed: Record<string, NetState> = {};
      for (const [k, v] of Object.entries(nets)) {
        const prev = this.lastObserve.nets[k];
        if (!prev || prev.logic !== v.logic || prev.voltage !== v.voltage || prev.contention !== v.contention) changed[k] = v;
      }
      if (Object.keys(changed).length) update.nets = changed;
      this.lastObserve.nets = nets;
    }
    if (this.observeWhat.has('outputs')) {
      const outputs = this.outputs(this.now);
      const changed: Record<string, OutputValue> = {};
      for (const [k, v] of Object.entries(outputs)) if (this.lastObserve.outputs[k] !== v) changed[k] = v;
      if (Object.keys(changed).length) update.outputs = changed;
      this.lastObserve.outputs = outputs;
    }
    this.notify('observe.update', update);
  }

  topology(): Topology {
    const kind = this.policy === 'functional' ? 'digital' : 'analog';
    const states = this.netStates(this.now);
    const nets = Object.entries(this.project.nets).map(([name, pins]) => ({ name, island: 'I1', pins, state: states[name] }));
    return { nets, islands: [{ id: 'I1', kind, nets: nets.map((n) => n.name) }] };
  }

  // --- probes and waveforms ----------------------------------------------------

  addProbe(target: string, kind: ProbeInfo['kind'], rateHz?: number): ProbeInfo {
    const base = target.replace(/^[a-z]+:/, '').replace(/[^A-Za-z0-9_]/g, '_').toLowerCase();
    let id = base;
    let n = 2;
    while (this.probes.has(id)) id = `${base}_${n++}`;
    const info: ProbeInfo = { probe: id, target, kind, rate_hz: rateHz ?? 200_000 };
    this.probeStarts.set(id, this.now);
    this.probes.set(id, info);
    return info;
  }

  private probeStarts = new Map<string, Time>();

  moveProbe(id: string, target: string) {
    const info = this.probes.get(id);
    if (!info) throw new EngineError(-32002, `no probe ${id}`);
    if (!['analog', 'digital'].includes(info.kind) || !/^(net|pin):/.test(info.target)) throw new EngineError(-32602, 'Only net/pin voltage or logic probes can move');
    const valid = target.startsWith('net:') ? Object.hasOwn(this.project.nets, target.slice(4)) : target.startsWith('pin:') && Object.values(this.project.nets).some((pins) => pins.includes(target.slice(4)));
    if (!valid) throw new EngineError(-32602, 'Unknown connected net or pin');
    if (target !== info.target) { info.target = target; info.capture_epoch = (info.capture_epoch ?? 0) + 1; this.probeStarts.set(id, this.now); }
    return { id, target, capture_epoch: info.capture_epoch ?? 0, now_ps: this.now.toString() };
  }

  removeProbe(id: string): void {
    if (!this.probes.delete(id)) throw new EngineError(-32002, `no probe ${id}`);
    for (const s of this.streams.values()) s.probes = s.probes.filter((p) => p !== id);
  }

  listProbes(): ProbeInfo[] {
    return [...this.probes.values()];
  }

  private quantityOf(info: ProbeInfo): Quantity | 'pressed' | null {
    const m = /^(net|pin|device|output):(.+)$/.exec(info.target);
    if (!m) return null;
    const name = m[2]!;
    if (m[1] === 'net') {
      if (name === 'BTN') return 'btn';
      if (name === 'COLL') return 'coll';
      if (name === 'Q1B') return 'q1b';
      return null;
    }
    if (m[1] === 'pin') {
      const pin = name;
      for (const [net, pins] of Object.entries(this.project.nets)) {
        if (pins.includes(pin)) return this.quantityOf({ ...info, target: `net:${net}` });
      }
      return null;
    }
    if (name === 'Q1.ic') return 'ic';
    if (name === 'SW1.closed' || name === 'SW1.pressed') return 'pressed';
    return null;
  }

  private sampleAt(q: Quantity | 'pressed' | null, kind: ProbeInfo['kind'], t: Time): number {
    if (q === null) return kind === 'digital' ? 2 : 0; // Z for unknown digital, 0 V otherwise
    if (q === 'pressed') return this.pressedAt(t) ? 1 : 0;
    const v = this.valueAt(q, t);
    if (kind === 'digital') return v > 1.65 ? 1 : 0;
    return v;
  }

  readProbe(id: string, fromPs?: string, toPs?: string, max = 2000): { samples: [string, number][] } {
    const info = this.probes.get(id);
    if (!info) throw new EngineError(-32002, `no probe ${id}`);
    const requested = fromPs ? BigInt(fromPs) : 0n;
    const start = this.probeStarts.get(id) ?? 0n;
    const from = requested > start ? requested : start;
    const to = toPs ? BigInt(toPs) : this.now;
    const period = BigInt(Math.max(1, Math.round(1e12 / (info.rate_hz ?? 200_000))));
    const q = this.quantityOf(info);
    const samples: [string, number][] = [];
    let step = period;
    const count = (to - from) / period;
    if (count > BigInt(max)) step = (to - from) / BigInt(max);
    for (let t = from; t <= to && samples.length < max; t += step) samples.push([t.toString(), round(this.sampleAt(q, info.kind, t), 6)]);
    return { samples };
  }

  subscribe(probes: string[], maxRateHz?: number): string {
    for (const p of probes) if (!this.probes.has(p)) throw new EngineError(-32002, `no probe ${p}`);
    const id = `s${this.nextStream++}`;
    this.streams.set(id, { id, probes, cursor: this.now, seq: 0, maxRateHz: maxRateHz ?? 200_000 });
    return id;
  }

  unsubscribe(id: string): void {
    if (!this.streams.delete(id)) throw new EngineError(-32002, `no stream ${id}`);
  }

  private flushStreams(): void {
    for (const s of this.streams.values()) {
      for (const probeId of s.probes) {
        const info = this.probes.get(probeId);
        if (!info) continue;
        const rate = Math.min(info.rate_hz ?? 200_000, s.maxRateHz);
        const period = BigInt(Math.max(1, Math.round(1e12 / rate)));
        const start = this.probeStarts.get(probeId) ?? 0n;
        let first = s.cursor > start ? s.cursor : start;
        let n = Number((this.now - first) / period);
        if (n <= 0) continue;
        let gap = false;
        if (n > MAX_SAMPLES_PER_FRAME) {
          // The client fell behind: drop the oldest part and report a gap.
          first = this.now - BigInt(MAX_SAMPLES_PER_FRAME) * period;
          n = MAX_SAMPLES_PER_FRAME;
          gap = true;
        }
        const q = this.quantityOf(info);
        let bytes: Uint8Array;
        let encoding: WaveformSamples['encoding'];
        if (info.kind === 'digital') {
          encoding = 'u8';
          bytes = new Uint8Array(n);
          for (let i = 0; i < n; i++) bytes[i] = this.sampleAt(q, 'digital', first + BigInt(i) * period);
        } else {
          encoding = 'f32';
          const f = new Float32Array(n);
          for (let i = 0; i < n; i++) f[i] = this.sampleAt(q, info.kind, first + BigInt(i) * period);
          bytes = new Uint8Array(f.buffer);
        }
        const frame: WaveformSamples = {
          stream: s.id,
          seq: s.seq++,
          first_time_ps: first.toString(),
          encoding,
          data: bytesToBase64(bytes),
          period_ps: period.toString(),
          probe: probeId,
          capture_epoch: info.capture_epoch ?? 0,
          through_time_ps: this.now.toString(),
        };
        if (gap) frame.gap = true;
        this.notify('waveform.samples', frame);
      }
      s.cursor = this.now;
    }
  }

  // --- project -------------------------------------------------------------------

  beginTx(): string {
    const tx = `tx${this.nextTx++}`;
    this.txs.set(tx, structuredClone(this.project));
    return tx;
  }

  editTx(tx: string, edits: ProjectEdit[]): void {
    const staged = this.txs.get(tx);
    if (!staged) throw new EngineError(-32002, `no transaction ${tx}`);
    for (const edit of edits) applyEdit(staged, edit);
  }

  commitTx(tx: string): { applied: number; diagnostics: DiagnosticRecord[]; islands: number } {
    const staged = this.txs.get(tx);
    if (!staged) throw new EngineError(-32002, `no transaction ${tx}`);
    this.txs.delete(tx);
    hierarchyOf(staged);
    const diagnostics = validateProject(staged);
    const worldChanged = JSON.stringify(this.project.world ?? {}) !== JSON.stringify(staged.world ?? {});
    this.project = staged;
    // the project's room as it now reads, applied live (the engine records it as inputs)
    if (worldChanged) this.resetWorld();
    this.notify('project.changed', { tx, summary: `${staged.components.length} components, ${Object.keys(staged.nets).length} nets` });
    this.emitObserve(true);
    return { applied: 1, diagnostics, islands: 1 };
  }

  abortTx(tx: string): void {
    if (!this.txs.delete(tx)) throw new EngineError(-32002, `no transaction ${tx}`);
  }

  loadProject(json?: Project): { name: string; diagnostics: DiagnosticRecord[] } {
    this.pause('stop');
    this.project = json ? structuredClone(json) : demoProject();
    this.resetWorld();
    this.probes.clear();
    for (const p of this.project.probes ?? []) this.probes.set(p.id, { probe: p.id, target: p.target, kind: p.kind, rate_hz: p.rate_hz });
    this.streams.clear();
    this.reset();
    return { name: this.project.name, diagnostics: validateProject(this.project) };
  }

  // --- dev hooks -------------------------------------------------------------------

  raiseContention(): void {
    this.notify('diagnostics', {
      records: [
        {
          severity: 'warning',
          code: 'net.contention',
          message: 'two drivers of equal strength disagree on BTN (SW1.2 drives 0, VCC1.+ drives 1)',
          time_ps: this.now.toString(),
          subject: { kind: 'net', name: 'BTN' },
        },
      ],
    });
  }
}

export class EngineError extends Error {
  constructor(
    readonly code: number,
    message: string,
    readonly data?: unknown,
  ) {
    super(message);
  }
}

function round(v: number, digits = 4): number {
  const f = 10 ** digits;
  return Math.round(v * f) / f;
}

function applyEdit(p: Project, edit: ProjectEdit): void {
  if ('set_circuit' in edit) { Object.assign(p, structuredClone(edit.set_circuit)); return; }
  if ('add_component' in edit) {
    const e = edit.add_component;
    if (p.components.some((c) => c.id === e.id)) throw new EngineError(-32017, `component ${e.id} exists`);
    const c: ProjectComponent = { id: e.id, component: e.component };
    if (e.params) c.params = e.params as ProjectComponent['params'];
    if (e.layout) c.layout = e.layout;
    p.components.push(c);
  } else if ('remove_component' in edit) {
    const id = edit.remove_component.id;
    p.components = p.components.filter((c) => c.id !== id);
    if (p.layout?.groups) p.layout.groups = p.layout.groups.map((g) => ({ ...g, members: g.members.filter((m) => m !== id) })).filter((g) => g.members.length);
    for (const [net, pins] of Object.entries(p.nets)) {
      p.nets[net] = pins.filter((pin) => !pin.startsWith(`${id}.`));
      if (p.nets[net]!.length === 0) delete p.nets[net];
    }
    if (p.layout?.wires) p.layout.wires = p.layout.wires.filter((w) => w.net in p.nets);
    pruneBench(p, id);
  } else if ('set_param' in edit) {
    const c = p.components.find((x) => x.id === edit.set_param.id);
    if (!c) throw new EngineError(-32002, `no component ${edit.set_param.id}`);
    c.params = { ...(c.params ?? {}), [edit.set_param.param]: edit.set_param.value as string | number | boolean };
  } else if ('connect' in edit) {
    const { net, pin } = edit.connect;
    for (const pins of Object.values(p.nets)) {
      const i = pins.indexOf(pin);
      if (i >= 0) pins.splice(i, 1);
    }
    (p.nets[net] ??= []).push(pin);
    for (const name of Object.keys(p.nets)) if (p.nets[name]!.length === 0) delete p.nets[name];
  } else if ('disconnect' in edit) {
    for (const name of Object.keys(p.nets)) {
      p.nets[name] = p.nets[name]!.filter((x) => x !== edit.disconnect.pin);
      if (p.nets[name]!.length === 0) delete p.nets[name];
    }
  } else if ('rename_net' in edit) {
    const { from, to } = edit.rename_net;
    if (!(from in p.nets)) throw new EngineError(-32002, `no net ${from}`);
    if (to in p.nets) throw new EngineError(-32017, `net ${to} exists`);
    p.nets[to] = p.nets[from]!;
    delete p.nets[from];
    for (const w of p.layout?.wires ?? []) if (w.net === from) w.net = to;
    for (const l of p.layout?.labels ?? []) if (l.net === from) l.net = to;
    for (const pr of p.probes ?? []) if (pr.target === `net:${from}`) pr.target = `net:${to}`;
  } else if ('add_board' in edit) {
    (p.boards ??= []).push({ id: edit.add_board.id, board: edit.add_board.board, layout: edit.add_board.layout });
  } else if ('remove_board' in edit) {
    const id = edit.remove_board.id;
    p.boards = (p.boards ?? []).filter((b) => b.id !== id);
    pruneBench(p, id);
    if (p.layout?.groups) p.layout.groups = p.layout.groups.map((g) => ({ ...g, members: g.members.filter((m) => m !== id) })).filter((g) => g.members.length);
    for (const net of Object.keys(p.nets)) {
      p.nets[net] = p.nets[net]!.filter((pin) => pin.split('.')[0] !== id);
      if (p.nets[net]!.length === 0) delete p.nets[net];
    }
  } else if ('set_world' in edit) {
    p.world = { ...(p.world ?? {}), ...structuredClone(edit.set_world) };
  } else if ('set_layout' in edit) {
    const e = edit.set_layout;
    if (e.id) {
      const c = p.components.find((x) => x.id === e.id) ?? p.boards?.find((x) => x.id === e.id);
      if (c && e.layout) {
        if (c.layout?.locked && e.layout.locked !== false && (c.layout.x !== e.layout.x || c.layout.y !== e.layout.y || (c.layout.rotation ?? 0) !== (e.layout.rotation ?? 0) || !!c.layout.mirror !== !!e.layout.mirror)) throw new EngineError(-32602, `placement is locked for ${c.id}`);
        c.layout = { ...e.layout, locked: e.layout.locked ?? c.layout?.locked };
      }
    }
    p.layout ??= {};
    if (e.wires) p.layout.wires = e.wires;
    if (e.labels) p.layout.labels = e.labels;
    if (e.buses) p.layout.buses = e.buses;
    if (e.groups) p.layout.groups = structuredClone(e.groups);
    if (e.bench) p.layout.bench = structuredClone(e.bench);
  }
}

function initialWorld(spec?: Project['world']): WorldState {
  const fields = Object.fromEntries(WORLD_FIELDS.map((f) => [f.name, { value: spec?.fields?.[f.name] ?? f.value, unit: f.unit, set: spec?.fields?.[f.name] !== undefined }])) as WorldState['fields'];
  return { fields, bodies: structuredClone(spec?.bodies ?? []) };
}

/** A removed instance leaves the bench: its place and the jumpers to it (SPEC-007 §2). */
function pruneBench(p: Project, id: string): void {
  const bench = p.layout?.bench;
  if (!bench) return;
  if (bench.parts) delete bench.parts[id];
  if (bench.wires) bench.wires = bench.wires.filter((w) => !w.from.startsWith(`${id}.`) && !w.to.startsWith(`${id}.`));
}

function validateProject(p: Project): DiagnosticRecord[] {
  const diagnostics: DiagnosticRecord[] = [];
  const connected = new Set(Object.values(p.nets).flat());
  const pinsOf: Record<string, string[]> = {
    'emblink/resistor': ['1', '2'],
    'emblink/capacitor': ['1', '2'],
    'emblink/push_button': ['1', '2'],
    'emblink/led': ['A', 'K'],
    'emblink/bjt_npn': ['B', 'C', 'E'],
    'emblink/vcc': ['+'],
    'emblink/gnd': ['GND'],
  };
  for (const c of p.components) {
    for (const pin of pinsOf[c.component] ?? []) {
      if (!connected.has(`${c.id}.${pin}`)) {
        diagnostics.push({
          severity: 'warning',
          code: 'net.unconnected_required_pin',
          message: `${c.id}.${pin} is not connected`,
          subject: { kind: 'pin', name: `${c.id}.${pin}` },
        });
      }
    }
  }
  return diagnostics;
}
