// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A Saleae Logic 8 on the bench (docs/engineering/bench-instruments.md), shown
// as Saleae's Logic 2 software shows it: the capture settings (sample rate,
// duration, trigger), Start and Stop, the eight channel rows in the leads'
// colours, followed live while the capture runs, and the export to Logic 2's
// CSV or a VCD.

import { Button, Select, useToast } from '@embsim/design';
import { captureToCsv, captureToVcd, LogicAnalyzer, LogicCaptureRecords, type Cursors, type LogicChannel, type TimeWindow } from '@embsim/instruments';
import { formatSi, parseSi } from '@embsim/protocol';
import { Download, Play, Square } from 'lucide-react';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { errorMessage } from '../../actions';
import { getClient } from '../../connection';
import { platform } from '../../platform/platform';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';

export const LOGIC8 = 'emblink/saleae_logic8';

/** the Logic 8's leads, coloured as the resistor code its harness uses (0 is black, drawn grey on screen) */
export const LEAD_COLORS = ['#9aa3ad', '#8b5a2b', '#d9534f', '#f0975a', '#e8c547', '#5cb85c', '#4a90d9', '#9b6bd1'];

const RATES = [100e6, 50e6, 40e6, 25e6, 20e6, 10e6, 8e6, 5e6, 4e6, 2e6, 1e6];
const EDGES = ['rising', 'falling', 'both'] as const;

export function Logic8Panel({ part }: { part: string }) {
  const component = useProject((s) => s.project?.components.find((c) => c.id === part));
  const setParam = useProject((s) => s.setParam);
  const outputs = useSession((s) => s.outputs);
  const now = useSession((s) => s.now);
  const running = useSession((s) => s.running);
  const { toast } = useToast();
  const recRef = useRef<LogicCaptureRecords>(new LogicCaptureRecords(part));
  const [revision, setRevision] = useState(0);
  const [win, setWin] = useState<TimeWindow>({ start: 0n, span: 1_000_000_000n, follow: false });
  const [cursors, setCursors] = useState<Cursors>({ a: null, b: null });
  const followRef = useRef(true);
  // Logic 2's Async Serial analyzer on one channel (an app setting, not the device's)
  const [serial, setSerial] = useState<{ channel: number; baud: number } | null>(null);

  const params = component?.params ?? {};
  const rate = parseSi(params.sample_rate as string | number | undefined) ?? 10e6;
  const duration = parseSi(params.duration as string | number | undefined) ?? 1;
  const trigCh = Number(params.trigger_channel ?? -1);
  const trigEdge = (params.trigger_edge as (typeof EDGES)[number] | undefined) ?? 'rising';
  const state = (outputs[`${part}.state`] as string | undefined) ?? 'idle';
  const live = state === 'armed' || state === 'capturing';

  const pull = useCallback(async () => {
    const rec = recRef.current;
    let changed = false;
    for (let i = 0; i < 16; i++) {
      const cap = await getClient().instrument.read({ part, since: rec.next });
      if (cap.kind !== 'logic') break;
      changed = rec.ingest(cap) || changed;
      if (rec.needsRestart) continue; // the capture moved on: read it again from its first record
      if (cap.next === cap.since || cap.transitions.length === 0) break;
    }
    if (changed) {
      setRevision((r) => r + 1);
      // nothing to frame until the capture holds something (waiting for a trigger, it shows nothing)
      if (rec.state === 'armed' || rec.edges.length === 0) return;
      // follow the capture: its start to now (or its end)
      const start = rec.start ?? 0n;
      const end = rec.end ?? (rec.edges.length ? rec.edges[rec.edges.length - 1]!.t : start + 1n);
      // the view follows the capture until the user pans or zooms it
      setWin((w) => (followRef.current ? { start, span: end > start ? end - start : 1n, follow: false } : w));
    }
  }, [part]);

  // a capture under way is read as the circuit runs; a finished one once more for its tail
  useEffect(() => {
    if (!live && !running) return;
    const id = window.setInterval(() => void pull().catch(() => undefined), 150);
    return () => window.clearInterval(id);
  }, [live, running, pull]);
  const atReset = now === 0n;
  useEffect(() => {
    void pull().catch(() => undefined);
  }, [state, atReset, pull]);

  const run = (on: boolean) => {
    if (on) followRef.current = true;
    return (
      getClient()
      .input.set({ target: part, input: 'run', value: on })
      .then(() => pull())
      .catch((e: unknown) => toast({ kind: 'error', title: on ? 'Could not start the capture' : 'Could not stop the capture', description: errorMessage(e) }))
    );
  };
  const set = (param: string, value: unknown) =>
    setParam(part, param, value).catch((e: unknown) => toast({ kind: 'error', title: `Could not set ${param}`, description: errorMessage(e) }));

  const rec = recRef.current;
  const channels: LogicChannel[] = useMemo(
    () =>
      rec.records
        .map((record, n): LogicChannel => ({ record, color: LEAD_COLORS[n], visible: Boolean((rec.channels >> n) & 1), decode: serial?.channel === n ? { protocol: 'uart', params: { baud: serial.baud } } : undefined }))
        .filter((c) => c.visible),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [rec, revision, serial],
  );

  const save = async (kind: 'csv' | 'vcd') => {
    const text = kind === 'csv' ? captureToCsv(rec) : captureToVcd(rec);
    const name = `${part}-capture.${kind}`;
    const path = await (await platform()).saveTextFile(name, text, kind === 'csv' ? 'text/csv' : 'text/x-vcd');
    if (path) toast({ kind: 'success', title: `Saved ${path}` });
  };

  const transitions = Number(outputs[`${part}.transitions`] ?? rec.edges.length);
  return (
    <section className="wb-logic8" aria-label={`${part} logic analyzer`}>
      <div className="wb-logic8__bar" role="toolbar" aria-label={`${part} capture settings`}>
        {live ? (
          <Button size="sm" variant="primary" onClick={() => void run(false)}><Square /> Stop</Button>
        ) : (
          <Button size="sm" variant="primary" onClick={() => void run(true)}><Play /> Start</Button>
        )}
        <Select
          size="sm"
          label="Sample rate"
          value={String(RATES.find((r) => r <= rate * 1.0001) ?? 10e6)}
          onValueChange={(v) => void set('sample_rate', Number(v))}
          options={RATES.map((r) => ({ value: String(r), label: `${r / 1e6} MS/s` }))}
        />
        <label className="wb-logic8__field">
          Duration
          <input
            type="text"
            aria-label="Capture duration"
            defaultValue={formatSi(duration, 's')}
            key={duration}
            size={7}
            onBlur={(e) => {
              const v = parseSi(e.target.value.replace(/s$/, ''));
              if (v && v > 0 && v !== duration) void set('duration', v);
            }}
          />
        </label>
        <Select
          size="sm"
          label="Trigger channel"
          value={String(trigCh)}
          onValueChange={(v) => void set('trigger_channel', Number(v))}
          options={[{ value: '-1', label: 'No trigger' }, ...Array.from({ length: 8 }, (_, n) => ({ value: String(n), label: `Trigger on D${n}` }))]}
        />
        <Select
          size="sm"
          label="Trigger edge"
          disabled={trigCh < 0}
          value={trigEdge}
          onValueChange={(v) => void set('trigger_edge', v)}
          options={EDGES.map((e) => ({ value: e, label: e === 'both' ? 'Either edge' : `${e[0]!.toUpperCase()}${e.slice(1)} edge` }))}
        />
        <Select
          size="sm"
          label="Analyzer"
          value={serial ? String(serial.channel) : 'none'}
          onValueChange={(v) => setSerial(v === 'none' ? null : { channel: Number(v), baud: serial?.baud ?? 9600 })}
          options={[{ value: 'none', label: 'No analyzer' }, ...Array.from({ length: 8 }, (_, n) => ({ value: String(n), label: `Async Serial on D${n}` }))]}
        />
        {serial && (
          <label className="wb-logic8__field">
            Baud
            <input
              type="number"
              aria-label="Async Serial bit rate"
              min={1}
              defaultValue={serial.baud}
              key={serial.baud}
              onBlur={(e) => {
                const v = Number(e.target.value);
                if (v > 0) setSerial({ ...serial, baud: v });
              }}
            />
          </label>
        )}
        <span className="wb-logic8__status" role="status">
          {state === 'armed' ? 'Waiting for the trigger' : state === 'capturing' ? 'Capturing' : state === 'done' ? 'Capture complete' : 'Ready'}
          {' · '}
          {formatSi(Number(outputs[`${part}.rate`] ?? rec.sampleRate ?? rate), 'S/s')}
          {live && state === 'armed' ? '' : ` · ${transitions} ${transitions === 1 ? 'transition' : 'transitions'}`}
          {rec.full ? ' · memory full' : ''}
        </span>
        <Button size="sm" variant="ghost" disabled={!rec.edges.length} onClick={() => void save('csv').catch((e: unknown) => toast({ kind: 'error', title: 'Could not export', description: errorMessage(e) }))}><Download /> CSV</Button>
        <Button size="sm" variant="ghost" disabled={!rec.edges.length} onClick={() => void save('vcd').catch((e: unknown) => toast({ kind: 'error', title: 'Could not export', description: errorMessage(e) }))}><Download /> VCD</Button>
      </div>
      <LogicAnalyzer
        label={`${part} capture`}
        channels={channels}
        window={win}
        onWindowChange={(w) => {
          followRef.current = false;
          setWin(w);
        }}
        cursors={cursors}
        onCursorsChange={setCursors}
        now={rec.end ?? now}
        revision={revision}
      />
    </section>
  );
}
