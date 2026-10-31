// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A Nordic Power Profiler Kit II on the bench (docs/engineering/bench-instruments.md),
// shown as Nordic's Power Profiler app shows it: the mode (source meter or
// ampere meter) and the supply voltage, Start and Stop, the current at
// 100 kS/s followed live, the window's (or the A-B selection's) average, peak
// and charge, and the logic port's channels underneath.

import { Button, Select, useToast } from '@embsim/design';
import { LogicAnalyzer, LogicCaptureRecords, Oscilloscope, PowerCaptureRecords, type Cursors, type LogicChannel, type TimeWindow } from '@embsim/instruments';
import { formatSi, parseSi } from '@embsim/protocol';
import { Download, Play, Square } from 'lucide-react';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { errorMessage } from '../../actions';
import { getClient } from '../../connection';
import { platform } from '../../platform/platform';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';
import { LEAD_COLORS } from './Logic8Panel';

export const PPK2 = 'emblink/nordic_ppk2';

/** how much the live view shows: the last 50 ms */
const LIVE_SPAN = 50_000_000_000n;

const amps = (a: number) => formatSi(a, 'A');

export function Ppk2Panel({ part }: { part: string }) {
  const project = useProject((s) => s.project);
  const component = project?.components.find((c) => c.id === part);
  const setParam = useProject((s) => s.setParam);
  const outputs = useSession((s) => s.outputs);
  const running = useSession((s) => s.running);
  const now = useSession((s) => s.now);
  const { toast } = useToast();
  const meter = `${part}.METER`;
  const logicPart = `${part}.LOGIC`;
  const powerRef = useRef(new PowerCaptureRecords(meter));
  const logicRef = useRef(new LogicCaptureRecords(logicPart));
  const followRef = useRef(true);
  const [revision, setRevision] = useState(0);
  const [win, setWin] = useState<TimeWindow>({ start: 0n, span: LIVE_SPAN, follow: false });
  const [cursors, setCursors] = useState<Cursors>({ a: null, b: null });

  const params = component?.params ?? {};
  const mode = (params.mode as 'source' | 'ampere' | undefined) ?? 'source';
  const voltage = parseSi(params.voltage as string | number | undefined) ?? 3.3;
  const sampling = outputs[`${meter}.state`] === 'sampling';

  // the logic port's channels wired to something
  const wired = useMemo(
    () => Array.from({ length: 8 }, (_, n) => n).filter((n) => Object.values(project?.nets ?? {}).some((pins) => pins.includes(`${part}.D${n}`))),
    [project?.nets, part],
  );

  const pull = useCallback(async () => {
    const power = powerRef.current;
    const logic = logicRef.current;
    let changed = false;
    for (let i = 0; i < 8; i++) {
      const cap = await getClient().instrument.read({ part: meter, since: power.next });
      if (cap.kind !== 'power') break;
      changed = power.ingest(cap) || changed;
      if (cap.next === cap.since || !cap.samples.length) break;
    }
    if (wired.length) {
      for (let i = 0; i < 8; i++) {
        const cap = await getClient().instrument.read({ part: logicPart, since: logic.next });
        if (cap.kind !== 'logic') break;
        changed = logic.ingest(cap) || changed;
        if (logic.needsRestart) continue;
        if (cap.next === cap.since || !cap.transitions.length) break;
      }
    }
    if (changed) {
      setRevision((r) => r + 1);
      if (followRef.current && power.length) {
        const end = power.timeOf(power.length);
        const start = end - LIVE_SPAN > (power.start ?? 0n) ? end - LIVE_SPAN : (power.start ?? 0n);
        setWin({ start, span: end - start > 0n ? end - start : 1n, follow: false });
      }
    }
  }, [meter, logicPart, wired.length]);

  useEffect(() => {
    if (!sampling && !running) return;
    const id = window.setInterval(() => void pull().catch(() => undefined), 150);
    return () => window.clearInterval(id);
  }, [sampling, running, pull]);
  const atReset = now === 0n;
  useEffect(() => {
    void pull().catch(() => undefined);
  }, [sampling, atReset, pull]);

  const run = async (on: boolean) => {
    if (on) followRef.current = true;
    try {
      await getClient().input.set({ target: meter, input: 'run', value: on });
      await getClient().input.set({ target: logicPart, input: 'run', value: on });
      await pull();
    } catch (e) {
      toast({ kind: 'error', title: on ? 'Could not start sampling' : 'Could not stop sampling', description: errorMessage(e) });
    }
  };
  const set = (param: string, value: unknown) =>
    setParam(part, param, value).catch((e: unknown) => toast({ kind: 'error', title: `Could not set ${param}`, description: errorMessage(e) }));

  const power = powerRef.current;
  const logic = logicRef.current;
  // the A-B selection when both cursors are set, else the window
  const sel = cursors.a !== null && cursors.b !== null;
  const from = sel ? (cursors.a! < cursors.b! ? cursors.a! : cursors.b!) : win.start;
  const to = sel ? (cursors.a! < cursors.b! ? cursors.b! : cursors.a!) : win.start + win.span;
  // eslint-disable-next-line react-hooks/exhaustive-deps
  const stats = useMemo(() => power.window(from, to), [power, from, to, revision]);
  const traces = useMemo(() => [{ record: power.record, color: '#4a90d9' }], [power]);
  const channels: LogicChannel[] = useMemo(
    () => wired.map((n) => ({ record: logic.records[n]!, color: LEAD_COLORS[n] })),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [logic, wired, revision],
  );
  const onWindow = (w: TimeWindow) => {
    followRef.current = false;
    setWin(w);
  };

  const save = async () => {
    const path = await (await platform()).saveTextFile(`${part}-current.csv`, power.toCsv(), 'text/csv');
    if (path) toast({ kind: 'success', title: `Saved ${path}` });
  };

  return (
    <section className="wb-ppk2" aria-label={`${part} power profiler`}>
      <div className="wb-logic8__bar" role="toolbar" aria-label={`${part} settings`}>
        {sampling ? (
          <Button size="sm" variant="primary" onClick={() => void run(false)}><Square /> Stop</Button>
        ) : (
          <Button size="sm" variant="primary" onClick={() => void run(true)}><Play /> Start</Button>
        )}
        <Select
          size="sm"
          label="Mode"
          value={mode}
          onValueChange={(v) => void set('mode', v)}
          options={[{ value: 'source', label: 'Source meter' }, { value: 'ampere', label: 'Ampere meter' }]}
        />
        {mode === 'source' && (
          <label className="wb-logic8__field">
            Supply
            <input
              type="number"
              aria-label="Supply voltage"
              min={0.8}
              max={5}
              step={0.1}
              defaultValue={voltage}
              key={voltage}
              onBlur={(e) => {
                const v = Number(e.target.value);
                if (v >= 0.8 && v <= 5 && v !== voltage) void set('voltage', v);
              }}
            />
            V
          </label>
        )}
        <span className="wb-logic8__status" role="status">
          {sampling ? 'Sampling' : 'Stopped'} · 100 kS/s{outputs[`${meter}.limited`] === true ? ' · current limit (600 mA)' : ''}
        </span>
        <Button size="sm" variant="ghost" disabled={!power.length} onClick={() => void save().catch((e: unknown) => toast({ kind: 'error', title: 'Could not export', description: errorMessage(e) }))}><Download /> CSV</Button>
      </div>
      <dl className="wb-ppk2__readouts" aria-label={sel ? 'Selection' : 'Window'}>
        <div><dt>{sel ? 'Selection' : 'Window'} average</dt><dd>{amps(stats.average)}</dd></div>
        <div><dt>Max</dt><dd>{amps(stats.max)}</dd></div>
        <div><dt>Charge</dt><dd>{formatSi(stats.charge, 'C')}</dd></div>
        <div><dt>Time</dt><dd>{formatSi(Number(to - from) * 1e-12, 's')}</dd></div>
        <div><dt>Average since Start</dt><dd>{amps(Number(outputs[`${meter}.average`] ?? 0))}</dd></div>
      </dl>
      <div className="wb-ppk2__plots">
        <Oscilloscope label={`${part} current`} traces={traces} window={win} onWindowChange={onWindow} cursors={cursors} onCursorsChange={setCursors} now={power.length ? power.timeOf(power.length) : now} revision={revision} />
        {channels.length > 0 && (
          <LogicAnalyzer label={`${part} logic port`} channels={channels} window={win} onWindowChange={onWindow} cursors={cursors} onCursorsChange={setCursors} now={power.length ? power.timeOf(power.length) : now} revision={revision} />
        )}
      </div>
    </section>
  );
}
