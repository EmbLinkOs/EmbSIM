// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Instruments store: waveform records fed by waveform.samples, the shared
// time window and cursors, probe list, serial lines. Records are mutable
// buffers; `revision` tells renderers that samples arrived.

import { defaultWindow, WaveformRecord, type Cursors, type SerialLine, type TimeWindow } from '@embsim/instruments';
import type { ProbeInfo, WaveformSamples } from '@embsim/protocol';
import { create } from 'zustand';

export interface InstrumentsState {
  probes: ProbeInfo[];
  records: Map<string, WaveformRecord>;
  revision: number;
  window: TimeWindow;
  cursors: Cursors;
  selectedScope: string | null;
  selectedLogic: string | null;
  streams: (string | number)[];
  streamProbes: string[];
  serialLines: SerialLine[];
  serialTarget: string | null;
  frames: number;
  gaps: number;
  setProbes(probes: ProbeInfo[]): void;
  recordFor(probe: ProbeInfo): WaveformRecord;
  appendFrame(frame: WaveformSamples): void;
  setWindow(w: TimeWindow): void;
  setCursors(c: Cursors): void;
  setSelectedScope(id: string | null): void;
  setSelectedLogic(id: string | null): void;
  setStreams(streams: (string | number)[], probes: string[]): void;
  addSerialLine(line: SerialLine): void;
  setSerialTarget(target: string | null): void;
  clearRecords(): void;
}

let pendingBump: number | null = null;

/** The 0.1 server reports a numeric handle in `probe` and the id in `id`; the UI keys everything by the string id. */
export function probeId(p: ProbeInfo): string {
  return p.id ?? String(p.probe);
}

function normalizeProbe(p: ProbeInfo): ProbeInfo {
  return { ...p, probe: probeId(p) };
}

export const useInstruments = create<InstrumentsState>((set, get) => ({
  probes: [],
  records: new Map(),
  revision: 0,
  window: defaultWindow(),
  cursors: { a: null, b: null },
  selectedScope: null,
  selectedLogic: null,
  streams: [],
  streamProbes: [],
  serialLines: [],
  serialTarget: null,
  frames: 0,
  gaps: 0,

  setProbes: (raw) => {
    const probes = raw.map(normalizeProbe);
    const records = new Map(get().records);
    for (const p of probes) {
      const id = p.probe as string;
      if (!records.has(id)) {
        const unit = p.target.startsWith('device:') && p.target.endsWith('.ic') ? 'A' : p.kind === 'analog' ? 'V' : '';
        records.set(id, new WaveformRecord(id, p.kind === 'digital' ? 'digital' : 'analog', id, unit));
      }
    }
    for (const id of [...records.keys()]) if (!probes.some((p) => p.probe === id)) records.delete(id);
    set({ probes, records });
  },
  recordFor: (raw) => {
    const probe = normalizeProbe(raw);
    const existing = get().records.get(probe.probe as string);
    if (existing) return existing;
    get().setProbes([...get().probes.filter((p) => p.probe !== probe.probe), probe]);
    return get().records.get(probe.probe as string)!;
  },
  appendFrame: (frame) => {
    const id = frame.probe ?? String(frame.stream);
    const record = get().records.get(id);
    if (!record) return;
    record.appendFrame(frame);
    const s = get();
    s.frames++;
    if (frame.gap) s.gaps++;
    if (pendingBump === null) {
      pendingBump = requestAnimationFrame(() => {
        pendingBump = null;
        set((st) => ({ revision: st.revision + 1, frames: st.frames, gaps: st.gaps }));
      });
    }
  },
  setWindow: (window) => set({ window }),
  setCursors: (cursors) => set({ cursors }),
  setSelectedScope: (selectedScope) => set({ selectedScope }),
  setSelectedLogic: (selectedLogic) => set({ selectedLogic }),
  setStreams: (streams, streamProbes) => set({ streams, streamProbes }),
  addSerialLine: (line) => set((s) => ({ serialLines: [...s.serialLines, line].slice(-2000) })),
  setSerialTarget: (serialTarget) => set({ serialTarget }),
  clearRecords: () => {
    for (const r of get().records.values()) r.clear();
    set((s) => ({ revision: s.revision + 1, cursors: { a: null, b: null }, window: defaultWindow(), serialLines: [] }));
  },
}));
