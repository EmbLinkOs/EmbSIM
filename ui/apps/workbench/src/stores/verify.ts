// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The Verify perspective's store: the scenario being edited (from a file of
// the project, or new, or made from a recording), the last report, and the
// recordings made in this session with what replaying each found.

import type { Recording, ReplayResult, Report, Scenario, ScenarioAssertion, ScenarioInput } from '@embsim/protocol';
import { create } from 'zustand';

export interface Draft {
  /** The scenario's file, relative to the project's folder; null until saved. */
  path: string | null;
  scenario: Scenario;
  dirty: boolean;
}

export interface RecordingEntry {
  id: number;
  at: string;
  recording: Recording;
  replay?: ReplayResult;
}

interface VerifyState {
  draft: Draft | null;
  report: Report | null;
  /** The report's assertions by id, for the editor's rows. */
  results: Record<string, { ok: boolean; observed: unknown }>;
  recordings: RecordingEntry[];
  /** analysis.start was called and not stopped (the Coverage panel's) */
  analysisOn: boolean;
  setAnalysisOn(on: boolean): void;
  /** record.start was called and record.stop not yet */
  recordingOn: boolean;
  setRecordingOn(on: boolean): void;
  openDraft(path: string | null, scenario: Scenario): void;
  closeDraft(): void;
  editDraft(fn: (s: Scenario) => Scenario): void;
  markSaved(path: string): void;
  setReport(report: Report | null): void;
  addRecording(recording: Recording): void;
  setReplay(id: number, replay: ReplayResult): void;
}

let nextRecording = 1;

export function emptyScenario(name = 'new-scenario'): Scenario {
  return { schema_version: 1, kind: 'scenario', name, until: '50ms', inputs: [], assertions: [] };
}

/** A scenario as a file holds it, with the lists the editor needs present. */
export function normalizeScenario(s: Partial<Scenario>): Scenario {
  return {
    ...emptyScenario(),
    ...s,
    schema_version: s.schema_version ?? 1,
    kind: 'scenario',
    name: s.name ?? 'scenario',
    inputs: (s.inputs ?? []) as ScenarioInput[],
    assertions: (s.assertions ?? []) as ScenarioAssertion[],
  };
}

export const useVerify = create<VerifyState>((set) => ({
  draft: null,
  report: null,
  results: {},
  recordings: [],
  analysisOn: false,
  setAnalysisOn: (analysisOn) => set({ analysisOn }),
  recordingOn: false,
  setRecordingOn: (recordingOn) => set({ recordingOn }),
  openDraft: (path, scenario) => set({ draft: { path, scenario: normalizeScenario(scenario), dirty: path === null }, results: {} }),
  closeDraft: () => set({ draft: null, results: {} }),
  editDraft: (fn) => set((st) => (st.draft ? { draft: { ...st.draft, scenario: fn(st.draft.scenario), dirty: true } } : {})),
  markSaved: (path) => set((st) => (st.draft ? { draft: { ...st.draft, path, dirty: false } } : {})),
  setReport: (report) =>
    set({
      report,
      results: Object.fromEntries((report?.assertions ?? []).map((a) => [a.id, { ok: a.ok, observed: a.observed }])),
    }),
  addRecording: (recording) =>
    set((st) => ({ recordings: [{ id: nextRecording++, at: new Date().toLocaleTimeString(), recording }, ...st.recordings] })),
  setReplay: (id, replay) => set((st) => ({ recordings: st.recordings.map((r) => (r.id === id ? { ...r, replay } : r)) })),
}));

/** A value as the editor shows it: strings as they are, the rest as JSON. */
export function formatValue(v: unknown): string {
  return typeof v === 'string' ? v : JSON.stringify(v);
}

/** What the editor's text means: true, false, a number, JSON, else the text. */
export function parseValue(text: string): unknown {
  const t = text.trim();
  if (t === 'true') return true;
  if (t === 'false') return false;
  if (t !== '' && !Number.isNaN(Number(t))) return Number(t);
  if (/^[[{"]/.test(t)) {
    try {
      return JSON.parse(t) as unknown;
    } catch {
      return text;
    }
  }
  return text;
}

export type AssertionWhen = 'at' | 'between' | 'by' | 'always';

export function whenOf(a: ScenarioAssertion): AssertionWhen {
  if (a.always) return 'always';
  if (a.between) return 'between';
  if (a.by !== undefined) return 'by';
  return 'at';
}

/** The assertion with its timing changed to `when`, keeping what applies. */
export function withWhen(a: ScenarioAssertion, when: AssertionWhen): ScenarioAssertion {
  const { at, between, by, always, diagnostics, ...rest } = a;
  const t = at ?? by ?? between?.[0] ?? '10ms';
  switch (when) {
    case 'at':
      return { ...rest, at: t };
    case 'by':
      return { ...rest, by: t };
    case 'between':
      return { ...rest, between: between ?? [t, t] };
    default:
      return { id: a.id, always: always ?? true, diagnostics: diagnostics ?? { forbid: [] } };
  }
}
