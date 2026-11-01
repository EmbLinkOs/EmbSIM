// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A Rigol DS1054Z on the bench (docs/engineering/bench-instruments.md): its
// screen and its front panel's controls, the acquisitions followed live as
// the circuit runs. Run/Stop and Single; Auto, Normal or Single sweep; the
// timebase in its 1-2-5 steps; each channel's on, volts a division, offset,
// probe and coupling; the edge trigger's source, level and slope; and the
// automatic measurements on one channel.

import { Button, Select, useToast } from '@embsim/design';
import { SCOPE_COLORS, ScopeScreen, scopeMeasure, type ScopeChannelView } from '@embsim/instruments';
import { formatSi, parseSi, type ScopeCapture } from '@embsim/protocol';
import { useCallback, useEffect, useMemo, useState } from 'react';
import { errorMessage } from '../../actions';
import { getClient } from '../../connection';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';
import { t as tr } from '../../i18n';

export const DS1054Z = 'emblink/rigol_ds1054z';

/** 1-2-5 steps from `lo` to `hi` */
function steps(lo: number, hi: number): number[] {
  const out: number[] = [];
  for (let d = 1e-12; d <= hi * 1.0001; d *= 10)
    for (const m of [1, 2, 5]) {
      const v = Number((d * m).toPrecision(3));
      if (v >= lo * 0.9999 && v <= hi * 1.0001) out.push(v);
    }
  return out;
}
const TIMEBASES = steps(5e-9, 50);
const VDIVS = steps(1e-3, 10);

export function Ds1054zPanel({ part }: { part: string }) {
  const component = useProject((s) => s.project?.components.find((c) => c.id === part));
  const setParam = useProject((s) => s.setParam);
  const outputs = useSession((s) => s.outputs);
  const running = useSession((s) => s.running);
  const now = useSession((s) => s.now);
  const { toast } = useToast();
  const [capture, setCapture] = useState<ScopeCapture | null>(null);
  const [measureCh, setMeasureCh] = useState(0);

  const p = useMemo(() => component?.params ?? {}, [component?.params]);
  const num = (k: string, d: number) => parseSi(p[k] as string | number | undefined) ?? d;
  const str = <T extends string>(k: string, d: T) => (p[k] as T | undefined) ?? d;
  const timebase = num('timebase', 1e-3);
  const sweep = str<'auto' | 'normal' | 'single'>('sweep', 'auto');
  const trigSource = Number(p.trig_source ?? 1);
  const trigLevel = num('trig_level', 0);
  const trigSlope = str<'rising' | 'falling' | 'either'>('trig_slope', 'rising');
  const chans: (ScopeChannelView & { probe: '1x' | '10x'; coupling: string })[] = useMemo(
    () =>
      [1, 2, 3, 4].map((n) => ({
        on: p[`ch${n}_on`] === undefined ? n === 1 : Boolean(p[`ch${n}_on`]),
        scale: parseSi(p[`ch${n}_scale`] as string | number | undefined) ?? 1,
        offset: parseSi(p[`ch${n}_offset`] as string | number | undefined) ?? 0,
        probe: (p[`ch${n}_probe`] as '1x' | '10x' | undefined) ?? '10x',
        coupling: (p[`ch${n}_coupling`] as string | undefined) ?? 'dc',
      })),
    [p],
  );

  // the next acquisition (seq above the one shown); a stopped scope keeps its screen
  const pull = useCallback(async () => {
    const r = await getClient().instrument.read({ part, since: capture?.seq ?? 0 });
    if (r.kind !== 'scope') return;
    setCapture((c) => (r.channels.length || !c ? r : { ...c, status: r.status, seq: c.seq }));
  }, [part, capture?.seq]);
  useEffect(() => {
    if (!running) return;
    const id = window.setInterval(() => void pull().catch(() => undefined), 100);
    return () => window.clearInterval(id);
  }, [running, pull]);
  const atReset = now === 0n;
  useEffect(() => {
    void pull().catch(() => undefined);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [atReset, running]);

  const set = (param: string, value: unknown) =>
    setParam(part, param, value).catch((e: unknown) => toast({ kind: 'error', title: `Could not set ${param}`, description: errorMessage(e) }));
  const press = async (input: string, value: boolean) => {
    try {
      await getClient().input.set({ target: part, input, value });
      await pull();
    } catch (e) {
      toast({ kind: 'error', title: tr('inst.scopeRefused'), description: errorMessage(e) });
    }
  };
  const status = (outputs[`${part}.status`] as string | undefined) ?? capture?.status ?? 'STOP';
  const stopped = status === 'STOP';
  const single = async () => {
    if (sweep !== 'single') await set('sweep', 'single');
    await press('run', false);
    await press('run', true);
  };

  const shown = capture?.channels[measureCh];
  const m = useMemo(
    () => (shown?.points && capture?.dx_ps ? scopeMeasure(shown.points, Number(capture.dx_ps) * 1e-12) : null),
    [shown, capture?.dx_ps],
  );
  const fmt = (v: number | undefined, unit: string) => (v === undefined || Number.isNaN(v) ? '—' : formatSi(v, unit));

  return (
    <section className="wb-ds1054z" aria-label={`${part} oscilloscope`}>
      <div className="wb-ds1054z__body">
        <div className="wb-ds1054z__screen">
          <div className="wb-ds1054z__top">
            <span>{formatSi(timebase, 's')}/div</span>
            <span>{capture?.sample_rate ? `${formatSi(capture.sample_rate, 'Sa/s')}` : '—'}</span>
            <span>{capture?.memory_points ? `${formatSi(capture.memory_points, 'pts')}` : ''}</span>
            <span>Trig CH{trigSource} {trigSlope === 'rising' ? '↑' : trigSlope === 'falling' ? '↓' : '↕'} {formatSi(trigLevel, 'V')}</span>
          </div>
          <ScopeScreen label={`${part} screen`} capture={capture} channels={chans} trigger={{ source: trigSource, level: trigLevel }} />
          <div className="wb-ds1054z__chips">
            {chans.map((c, k) =>
              c.on ? (
                <span key={k} style={{ color: SCOPE_COLORS[k] }}>
                  CH{k + 1} {formatSi(c.scale, 'V')} {c.coupling.toUpperCase()} {c.probe}
                </span>
              ) : null,
            )}
          </div>
        </div>
        <div className="wb-ds1054z__panel" role="group" aria-label={`${part} front panel`}>
          <div className="wb-ds1054z__row">
            <Button size="sm" variant={stopped ? 'danger' : 'primary'} onClick={() => void press('run', stopped)}>{stopped ? 'Stopped' : 'Run'}</Button>
            <Button size="sm" variant="ghost" onClick={() => void single()}>Single</Button>
            <Select size="sm" label="Sweep" value={sweep} onValueChange={(v) => void set('sweep', v)} options={[{ value: 'auto', label: 'Auto' }, { value: 'normal', label: 'Normal' }, { value: 'single', label: 'Single' }]} />
          </div>
          <div className="wb-ds1054z__row">
            <Select size="sm" label="Timebase" value={String(TIMEBASES.find((t) => t >= timebase * 0.9999) ?? 1e-3)} onValueChange={(v) => void set('timebase', Number(v))} options={TIMEBASES.map((t) => ({ value: String(t), label: `${formatSi(t, 's')}/div` }))} />
          </div>
          {chans.map((c, k) => {
            const n = k + 1;
            const factor = c.probe === '10x' ? 10 : 1;
            return (
              <fieldset key={n} className="wb-ds1054z__channel" style={{ borderColor: c.on ? SCOPE_COLORS[k] : undefined }}>
                <legend>
                  <label>
                    <input type="checkbox" aria-label={`CH${n} on`} checked={c.on} onChange={(e) => void set(`ch${n}_on`, e.target.checked)} /> CH{n}
                  </label>
                </legend>
                <Select size="sm" label={`CH${n} volts a division`} disabled={!c.on} value={String(VDIVS.map((v) => v * factor).find((v) => v >= c.scale * 0.9999) ?? factor)} onValueChange={(v) => void set(`ch${n}_scale`, Number(v))} options={VDIVS.map((v) => ({ value: String(v * factor), label: `${formatSi(v * factor, 'V')}/div` }))} />
                <label className="wb-logic8__field">
                  Offset
                  <input type="number" step={0.01} aria-label={`CH${n} offset, V`} disabled={!c.on} defaultValue={c.offset} key={c.offset} onBlur={(e) => { const v = Number(e.target.value); if (Number.isFinite(v) && v !== c.offset) void set(`ch${n}_offset`, v); }} />
                </label>
                <Select size="sm" label={`CH${n} probe`} value={c.probe} onValueChange={(v) => void set(`ch${n}_probe`, v)} options={[{ value: '10x', label: '10x' }, { value: '1x', label: '1x' }]} />
                <Select size="sm" label={`CH${n} coupling`} value={c.coupling} onValueChange={(v) => void set(`ch${n}_coupling`, v)} options={[{ value: 'dc', label: 'DC' }, { value: 'ac', label: 'AC' }, { value: 'gnd', label: 'GND' }]} />
              </fieldset>
            );
          })}
          <fieldset className="wb-ds1054z__channel">
            <legend>Trigger</legend>
            <Select size="sm" label="Trigger source" value={String(trigSource)} onValueChange={(v) => void set('trig_source', Number(v))} options={[1, 2, 3, 4].map((n) => ({ value: String(n), label: `CH${n}` }))} />
            <label className="wb-logic8__field">
              Level
              <input type="number" step={0.05} aria-label="Trigger level, V" defaultValue={trigLevel} key={trigLevel} onBlur={(e) => { const v = Number(e.target.value); if (Number.isFinite(v) && v !== trigLevel) void set('trig_level', v); }} />
            </label>
            <Select size="sm" label="Trigger slope" value={trigSlope} onValueChange={(v) => void set('trig_slope', v)} options={[{ value: 'rising', label: 'Rising' }, { value: 'falling', label: 'Falling' }, { value: 'either', label: 'Either' }]} />
          </fieldset>
        </div>
      </div>
      <div className="wb-ds1054z__measure" aria-label={`${part} measurements`}>
        <Select size="sm" label="Measure channel" value={String(measureCh)} onValueChange={(v) => setMeasureCh(Number(v))} options={[0, 1, 2, 3].map((k) => ({ value: String(k), label: `Measure CH${k + 1}` }))} />
        <dl className="wb-ppk2__readouts">
          <div><dt>Freq</dt><dd>{fmt(m?.freq, 'Hz')}</dd></div>
          <div><dt>Period</dt><dd>{fmt(m?.period, 's')}</dd></div>
          <div><dt>Vpp</dt><dd>{fmt(m?.vpp, 'V')}</dd></div>
          <div><dt>Vmax</dt><dd>{fmt(m?.max, 'V')}</dd></div>
          <div><dt>Vmin</dt><dd>{fmt(m?.min, 'V')}</dd></div>
          <div><dt>Vavg</dt><dd>{fmt(m?.avg, 'V')}</dd></div>
          <div><dt>Vrms</dt><dd>{fmt(m?.rms, 'V')}</dd></div>
          <div><dt>Duty</dt><dd>{m && !Number.isNaN(m.duty) ? `${(m.duty * 100).toFixed(1)} %` : '—'}</dd></div>
          {/* measured on the screen's points: an edge faster than their spacing reads as below it */}
          <div><dt>Rise</dt><dd>{m && capture?.dx_ps && m.rise <= 1.5 * Number(capture.dx_ps) * 1e-12 ? `< ${formatSi(1.5 * Number(capture.dx_ps) * 1e-12, 's')}` : fmt(m?.rise, 's')}</dd></div>
        </dl>
      </div>
    </section>
  );
}
