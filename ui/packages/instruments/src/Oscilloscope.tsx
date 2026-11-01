// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Oscilloscope: analog traces on a 2D canvas over the shared time window,
// min/max decimated per pixel column, with auto-range, cursors and a
// measurement strip (period, frequency, min, max, mean).

import { formatFrequency, formatTime, type Time } from '@embsim/protocol';
import { useEffect, useMemo, useRef } from 'react';
import { useTimeInteraction, type Cursors } from './interaction';
import { axisLabels, formatMeterValue } from './Meter';
import { ticks, timeToX, unitLabel, type TimeWindow } from './timeAxis';
import { cssVar, useCanvas } from './useCanvas';
import type { WaveformRecord } from './waveform';
import { uiText } from '@embsim/design';

export interface ScopeTrace {
  record: WaveformRecord;
  color: string;
  /** Fixed vertical range; omitted means auto-range over the window. */
  range?: [number, number];
  visible?: boolean;
}

export interface OscilloscopeProps {
  traces: ScopeTrace[];
  window: TimeWindow;
  onWindowChange: (w: TimeWindow) => void;
  cursors: Cursors;
  onCursorsChange: (c: Cursors) => void;
  now?: Time;
  /** Which trace the measurements describe. */
  selected?: string | null;
  onSelect?: (id: string) => void;
  /** Bumped by the owner when records receive samples. */
  revision?: number;
  label?: string;
  className?: string;
}

const PLOT_LEFT = 52;
const PLOT_BOTTOM = 22;
const PLOT_TOP = 8;

function niceStep(range: number, target: number): number {
  const rough = range / Math.max(1, target);
  const mag = 10 ** Math.floor(Math.log10(rough || 1));
  for (const n of [1, 2, 5, 10]) if (n * mag >= rough) return n * mag;
  return 10 * mag;
}

function fmt(v: number, unit: string): string {
  return formatMeterValue(v, unit, 4);
}

export function Oscilloscope({ traces, window: win, onWindowChange, cursors, onCursorsChange, now, selected, onSelect, revision = 0, label = 'Oscilloscope', className }: OscilloscopeProps) {
  const wrapRef = useRef<HTMLDivElement>(null);
  const enabledTraces = traces.filter((t) => t.visible !== false);
  const current = enabledTraces.find((t) => t.record.id === selected) ?? enabledTraces[0] ?? null;
  // One vertical axis has one unit. Selecting a voltage/current trace switches
  // the visible group instead of plotting volts on an axis labelled amperes.
  const visibleTraces = enabledTraces.filter((t) => t.record.unit === current?.record.unit);
  const end = win.start + win.span;

  const measurements = useMemo(() => {
    if (!current) return null;
    const r = current.record;
    const stats = r.stats(win.start, end);
    const weighted = r.measurements(win.start, end, now ?? r.end ?? win.start);
    const pf = r.periodAndFrequency(win.start, end);
    const frontier = r.captureEnd ?? win.start;
    const through = now !== undefined && now < frontier ? now : frontier;
    const ca = cursors.a !== null && cursors.a <= through ? r.valueAt(cursors.a) : NaN;
    const cb = cursors.b !== null && cursors.b <= through ? r.valueAt(cursors.b) : NaN;
    return { stats, weighted, pf, ca, cb };
    // revision drives recomputation as samples arrive
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [current, win.start, end, cursors.a, cursors.b, revision, now]);

  const { ref, size, redraw } = useCanvas((ctx, { width, height }) => {
    const el = wrapRef.current;
    const colors = {
      bg: cssVar(el, '--surface-sunken', '#0a0e17'),
      grid: cssVar(el, '--line', '#28324a'),
      text: cssVar(el, '--text-muted', '#9aa5b8'),
      cursorA: cssVar(el, '--accent', '#5fe3b1'),
      cursorB: cssVar(el, '--warning', '#e9bd55'),
      now: cssVar(el, '--text-faint', '#6b7689'),
    };
    const mono = `11px ${cssVar(el, '--font-mono', 'monospace')}`;
    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = colors.bg;
    ctx.fillRect(0, 0, width, height);
    const pw = width - PLOT_LEFT - 8;
    const ph = height - PLOT_TOP - PLOT_BOTTOM;
    if (pw <= 10 || ph <= 10) return;

    // Vertical range: auto over the window across visible traces unless fixed.
    let lo = Infinity;
    let hi = -Infinity;
    for (const t of visibleTraces) {
      if (t.range) {
        lo = Math.min(lo, t.range[0]);
        hi = Math.max(hi, t.range[1]);
      } else {
        const s = t.record.stats(win.start, end);
        const held = t.record.valueAt(win.start);
        if (Number.isFinite(held) && (t.record.captureEnd ?? win.start) >= win.start) {
          lo = Math.min(lo, held);
          hi = Math.max(hi, held);
        }
        if (Number.isFinite(s.min)) {
          lo = Math.min(lo, s.min);
          hi = Math.max(hi, s.max);
        }
      }
    }
    if (!Number.isFinite(lo)) {
      lo = 0;
      hi = 1;
    }
    if (hi - lo < 1e-6) {
      hi = lo + 1;
    }
    const pad = (hi - lo) * 0.08;
    lo -= pad;
    hi += pad;
    const yOf = (v: number) => PLOT_TOP + ph - ((v - lo) / (hi - lo)) * ph;

    // Grid: time ticks and voltage ticks.
    ctx.strokeStyle = colors.grid;
    ctx.lineWidth = 1;
    ctx.font = mono;
    ctx.fillStyle = colors.text;
    const tk = ticks(win, pw);
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    for (const t of tk.ticks) {
      const x = PLOT_LEFT + timeToX(t.t, win, pw);
      ctx.beginPath();
      ctx.moveTo(Math.round(x) + 0.5, PLOT_TOP);
      ctx.lineTo(Math.round(x) + 0.5, PLOT_TOP + ph);
      ctx.stroke();
      if (x < PLOT_LEFT + pw - 30) ctx.fillText(t.label, x, PLOT_TOP + ph + 5); // the unit sits at the right end
    }
    ctx.textAlign = 'left';
    ctx.fillText(unitLabel(tk.unit), PLOT_LEFT + pw - 18, PLOT_TOP + ph + 5);
    const vStep = niceStep(hi - lo, ph / 36);
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    const unit = current?.record.unit ?? '';
    const levels: number[] = [];
    for (let v = Math.ceil(lo / vStep) * vStep; v <= hi + vStep * 1e-9; v += vStep) levels.push(v);
    const labels = axisLabels(levels, vStep, unit);
    levels.forEach((v, i) => {
      const y = Math.round(yOf(v)) + 0.5;
      ctx.beginPath();
      ctx.moveTo(PLOT_LEFT, y);
      ctx.lineTo(PLOT_LEFT + pw, y);
      ctx.stroke();
      ctx.fillText(labels[i]!, PLOT_LEFT - 6, y);
    });

    // Traces: one min/max column per pixel.
    const buckets = Math.floor(pw);
    for (const t of visibleTraces) {
      const frontier = t.record.captureEnd;
      if (frontier === null || frontier < win.start) continue;
      const through = now !== undefined && now < frontier ? now : frontier;
      const extent = Math.max(0, Math.min(pw, timeToX(through, win, pw)));
      if (extent <= 0) continue;
      const d = t.record.decimate(win.start, end, buckets);
      ctx.save();
      ctx.beginPath();
      ctx.rect(PLOT_LEFT, PLOT_TOP, extent, ph);
      ctx.clip();
      ctx.strokeStyle = t.color;
      ctx.lineWidth = 1.4;
      ctx.lineJoin = 'round';
      ctx.beginPath();
      const held = t.record.valueAt(win.start);
      let pen = Number.isFinite(held);
      let lastY = pen ? yOf(held) : 0;
      if (pen) ctx.moveTo(PLOT_LEFT, lastY);
      for (let i = 0; i < Math.min(buckets, Math.ceil(extent) + 1); i++) {
        if (d.count[i] === 0) {
          // Carry the last value across empty columns inside the record (sparse sampling).
          if (pen) {
            ctx.lineTo(PLOT_LEFT + i, lastY);
          }
          continue;
        }
        const x = PLOT_LEFT + i;
        if (![d.min[i], d.max[i], d.first[i], d.last[i]].every(Number.isFinite)) { pen = false; continue; }
        const yMin = yOf(d.min[i]!);
        const yMax = yOf(d.max[i]!);
        const yFirst = yOf(d.first[i]!);
        const yLast = yOf(d.last[i]!);
        if (!pen) {
          ctx.moveTo(x, yFirst);
          pen = true;
        } else ctx.lineTo(x, yFirst);
        if (yMin !== yMax) {
          ctx.lineTo(x, yMax);
          ctx.lineTo(x, yMin);
        }
        ctx.lineTo(x, yLast);
        lastY = yLast;
      }
      ctx.stroke();
      ctx.restore();
    }

    // Cursors and the virtual clock.
    const drawCursor = (t: Time | null, color: string, name: string) => {
      if (t === null || t < win.start || t > end) return;
      const x = Math.round(PLOT_LEFT + timeToX(t, win, pw)) + 0.5;
      ctx.strokeStyle = color;
      ctx.setLineDash([4, 3]);
      ctx.beginPath();
      ctx.moveTo(x, PLOT_TOP);
      ctx.lineTo(x, PLOT_TOP + ph);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = color;
      ctx.textAlign = 'left';
      ctx.textBaseline = 'top';
      ctx.fillText(name, x + 3, PLOT_TOP + 2);
    };
    drawCursor(cursors.a, colors.cursorA, 'A');
    drawCursor(cursors.b, colors.cursorB, 'B');
    if (now !== undefined && now >= win.start && now <= end) {
      const x = Math.round(PLOT_LEFT + timeToX(now, win, pw)) + 0.5;
      ctx.strokeStyle = colors.now;
      ctx.beginPath();
      ctx.moveTo(x, PLOT_TOP);
      ctx.lineTo(x, PLOT_TOP + ph);
      ctx.stroke();
    }
  });

  // Redraw whenever inputs change.
  useEffect(() => {
    redraw();
  }, [redraw, win, cursors, now, revision, traces, selected]);

  const handlers = useTimeInteraction(wrapRef, { window: win, onWindowChange, cursors, onCursorsChange, plotLeft: PLOT_LEFT, width: size.width });

  const summary = measurements
    ? `${current?.record.label ?? ''}: min ${fmt(measurements.stats.min, current?.record.unit ?? '')}, max ${fmt(measurements.stats.max, current?.record.unit ?? '')}, mean ${fmt(measurements.weighted.mean, current?.record.unit ?? '')}, RMS ${fmt(measurements.weighted.rms, current?.record.unit ?? '')}${
        measurements.pf.frequencyHz ? `, ${formatFrequency(measurements.pf.frequencyHz)}` : ''
      }`
    : 'no trace';

  return (
    <div className={`es-instrument ${className ?? ''}`.trim()}>
      <div
        ref={wrapRef}
        className="es-instrument__plot"
        role="img"
        aria-label={`${label}. Window ${formatTime(win.start, { padZeros: false })} to ${formatTime(end, { padZeros: false })}. ${summary}. Arrow keys pan, plus and minus zoom, A and B place cursors.`}
        tabIndex={0}
        {...handlers}
      >
        <canvas ref={ref} className="es-instrument__canvas" />
        {visibleTraces.length === 0 && <div className="es-instrument__empty">{uiText('scope.noProbes', 'No analog probes subscribed')}</div>}
      </div>
      <div className="es-instrument__strip">
        <div className="es-instrument__traces" role="radiogroup" aria-label={uiText('scope.measured', 'Measured trace')}>
          {traces.map((t) => (
            <button
              key={t.record.id}
              type="button"
              role="radio"
              aria-checked={current?.record.id === t.record.id}
              className="es-instrument__trace"
              onClick={() => onSelect?.(t.record.id)}
            >
              <span className="es-instrument__swatch" style={{ background: t.color }} aria-hidden="true" />
              {t.record.label}
            </button>
          ))}
        </div>
        {measurements && current && (
          <dl className="es-instrument__measurements">
            <Measure label={uiText('scope.min', 'min')} value={fmt(measurements.stats.min, current.record.unit)} />
            <Measure label={uiText('scope.max', 'max')} value={fmt(measurements.stats.max, current.record.unit)} />
            <Measure label={uiText('scope.mean', 'mean')} value={fmt(measurements.weighted.mean, current.record.unit)} />
            <Measure label="RMS" value={fmt(measurements.weighted.rms, current.record.unit)} />
            {current.record.kind === 'digital' && <Measure label="duty" value={Number.isFinite(measurements.weighted.dutyCycle) ? `${(100 * measurements.weighted.dutyCycle).toFixed(2)} %` : '—'} />}
            <Measure label="coverage" value={`${(100 * measurements.weighted.coverage).toFixed(1)} %`} />
            <Measure label="period" value={measurements.pf.period !== null ? formatTime(measurements.pf.period, { padZeros: false }) : '—'} />
            <Measure label="freq" value={measurements.pf.frequencyHz !== null ? formatFrequency(measurements.pf.frequencyHz) : '—'} />
            {cursors.a !== null && <Measure label="A" value={`${formatTime(cursors.a, { padZeros: false })} · ${fmt(measurements.ca, current.record.unit)}`} />}
            {cursors.b !== null && <Measure label="B" value={`${formatTime(cursors.b, { padZeros: false })} · ${fmt(measurements.cb, current.record.unit)}`} />}
            {cursors.a !== null && cursors.b !== null && <Measure label="Δ" value={formatTime(cursors.b - cursors.a, { padZeros: false })} />}
          </dl>
        )}
      </div>
    </div>
  );
}

export function Measure({ label, value }: { label: string; value: string }) {
  return (
    <div className="es-measure">
      <dt>{label}</dt>
      <dd>{value}</dd>
    </div>
  );
}
