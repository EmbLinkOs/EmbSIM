// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Logic analyzer: stacked digital channels over the shared time window.
// Levels: 0 low, 1 high, Z drawn dashed at mid level, X drawn as a hatched
// band. Decimation keeps every transition visible (a column with both 0 and
// 1 draws as a full-height edge).

import { formatFrequency, formatTime, type Time } from '@embsim/protocol';
import { useEffect, useMemo, useRef } from 'react';
import { useTimeInteraction, type Cursors } from './interaction';
import { Measure } from './Oscilloscope';
import { ticks, timeToX, unitLabel, type TimeWindow } from './timeAxis';
import { cssVar, useCanvas } from './useCanvas';
import { decode, type Annotation, type DecoderSpec, type RecordLookup } from './decoders';
import { DIGITAL_X, DIGITAL_Z, type WaveformRecord } from './waveform';

export interface LogicChannel {
  record: WaveformRecord;
  color?: string;
  visible?: boolean;
  /** A protocol decoder whose annotations show under the channel. */
  decode?: DecoderSpec;
}

export interface LogicAnalyzerProps {
  channels: LogicChannel[];
  window: TimeWindow;
  onWindowChange: (w: TimeWindow) => void;
  cursors: Cursors;
  onCursorsChange: (c: Cursors) => void;
  now?: Time;
  selected?: string | null;
  onSelect?: (id: string) => void;
  revision?: number;
  label?: string;
  className?: string;
  /** The other records a decoder reads (SPI data, I2C SDA), by probe id. */
  lookup?: RecordLookup;
}

const ANN = 18; // the annotation row under a decoded channel
const PLOT_LEFT = 72;
const PLOT_BOTTOM = 22;
const PLOT_TOP = 6;
const ROW = 30;

export function LogicAnalyzer({ channels, window: win, onWindowChange, cursors, onCursorsChange, now, selected, onSelect, revision = 0, label = 'Logic analyzer', className, lookup }: LogicAnalyzerProps) {
  const wrapRef = useRef<HTMLDivElement>(null);
  const visible = channels.filter((c) => c.visible !== false);
  const current = channels.find((c) => c.record.id === selected) ?? visible[0] ?? null;
  const end = win.start + win.span;

  const measurements = useMemo(() => {
    if (!current) return null;
    const pf = current.record.periodAndFrequency(win.start, end);
    const edges = current.record.risingEdges(win.start, end).length;
    const la = cursors.a !== null ? current.record.valueAt(cursors.a) : NaN;
    const lb = cursors.b !== null ? current.record.valueAt(cursors.b) : NaN;
    return { pf, edges, la, lb };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [current, win.start, end, cursors.a, cursors.b, revision]);

  // decoded frames in the window, per decoded channel
  const annotations = useMemo(() => {
    const out = new Map<string, Annotation[]>();
    const find: RecordLookup = lookup ?? ((id) => channels.find((c) => c.record.id === id)?.record);
    for (const c of visible) if (c.decode) out.set(c.record.id, decode(c.decode, c.record, win.start, end, find));
    return out;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [visible.map((c) => c.record.id + (c.decode?.protocol ?? '')).join(), win.start, end, revision, lookup]);

  const { ref, size, redraw } = useCanvas((ctx, { width, height }) => {
    const el = wrapRef.current;
    const colors = {
      bg: cssVar(el, '--surface-sunken', '#0a0e17'),
      grid: cssVar(el, '--line', '#28324a'),
      text: cssVar(el, '--text-muted', '#9aa5b8'),
      high: cssVar(el, '--signal-high', '#5fe3b1'),
      low: cssVar(el, '--signal-low', '#7591d8'),
      z: cssVar(el, '--signal-z', '#d9a441'),
      x: cssVar(el, '--signal-x', '#f27d84'),
      cursorA: cssVar(el, '--accent', '#5fe3b1'),
      cursorB: cssVar(el, '--warning', '#e9bd55'),
      now: cssVar(el, '--text-faint', '#6b7689'),
      selected: cssVar(el, '--accent-muted', 'rgba(95,227,177,0.14)'),
    };
    const mono = `11px ${cssVar(el, '--font-mono', 'monospace')}`;
    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = colors.bg;
    ctx.fillRect(0, 0, width, height);
    const pw = width - PLOT_LEFT - 8;
    const ph = height - PLOT_TOP - PLOT_BOTTOM;
    if (pw <= 10 || ph <= 10) return;
    ctx.font = mono;

    // Time grid.
    ctx.strokeStyle = colors.grid;
    ctx.lineWidth = 1;
    ctx.fillStyle = colors.text;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    const tk = ticks(win, pw);
    for (const t of tk.ticks) {
      const x = Math.round(PLOT_LEFT + timeToX(t.t, win, pw)) + 0.5;
      ctx.beginPath();
      ctx.moveTo(x, PLOT_TOP);
      ctx.lineTo(x, PLOT_TOP + ph);
      ctx.stroke();
      ctx.fillText(t.label, x, PLOT_TOP + ph + 5);
    }
    ctx.textAlign = 'left';
    ctx.fillText(unitLabel(tk.unit), PLOT_LEFT + pw - 18, PLOT_TOP + ph + 5);

    const drawAnnotations = (c: CanvasRenderingContext2D, anns: Annotation[], y: number, plotWidth: number) => {
      c.textBaseline = 'middle';
      c.textAlign = 'center';
      for (const a of anns) {
        const x0 = PLOT_LEFT + Math.max(0, timeToX(a.start, win, plotWidth));
        const x1 = PLOT_LEFT + Math.min(plotWidth, timeToX(a.end, win, plotWidth));
        if (x1 < PLOT_LEFT || x0 > PLOT_LEFT + plotWidth) continue;
        const tone = a.kind === 'error' ? colors.x : a.kind === 'start' || a.kind === 'stop' ? colors.z : a.kind === 'address' ? colors.cursorB : colors.high;
        if (a.kind === 'start' || a.kind === 'stop') {
          c.fillStyle = tone;
          c.fillText(a.text, x0, y + ANN / 2);
          continue;
        }
        const w = Math.max(1, x1 - x0);
        c.strokeStyle = tone;
        c.lineWidth = 1;
        c.strokeRect(Math.round(x0) + 0.5, y + 2.5, Math.max(1, Math.round(w) - 1), ANN - 5);
        if (c.measureText(a.text).width + 4 <= w) {
          c.fillStyle = tone;
          c.fillText(a.text, x0 + w / 2, y + ANN / 2);
        }
      }
    };
    const buckets = Math.floor(pw);
    let nextTop = PLOT_TOP;
    visible.forEach((ch) => {
      const top = nextTop;
      const anns = annotations.get(ch.record.id);
      nextTop += ROW + (anns ? ANN : 0);
      if (top + ROW > PLOT_TOP + ph + 2) return;
      if (anns) drawAnnotations(ctx, anns, top + ROW - 2, pw);
      const yHigh = top + 5;
      const yLow = top + ROW - 7;
      const yMid = (yHigh + yLow) / 2;
      if (current?.record.id === ch.record.id) {
        ctx.fillStyle = colors.selected;
        ctx.fillRect(0, top, PLOT_LEFT - 4, ROW - 2);
      }
      ctx.fillStyle = colors.text;
      ctx.textAlign = 'right';
      ctx.textBaseline = 'middle';
      ctx.fillText(ch.record.label, PLOT_LEFT - 8, top + ROW / 2 - 1);

      const d = ch.record.decimate(win.start, end, buckets);
      const color = ch.color ?? colors.high;
      ctx.lineWidth = 1.5;
      // a probe reports changes only: the level before the window holds
      // until its first change, and nothing is drawn past the present
      const held = ch.record.valueAt(win.start);
      let prevLevel: number | null = Number.isFinite(held) ? held : null;
      const stop = now !== undefined && now < end ? Math.max(0, Math.floor(timeToX(now, win, pw))) : buckets;
      for (let i = 0; i < Math.min(buckets, stop + 1); i++) {
        if (d.count[i] === 0) {
          if (prevLevel === null) continue;
          // Extend the previous level across unsampled columns.
          const y = prevLevel === 1 ? yHigh : prevLevel === 0 ? yLow : yMid;
          ctx.strokeStyle = prevLevel === DIGITAL_Z ? colors.z : prevLevel === DIGITAL_X ? colors.x : color;
          ctx.beginPath();
          ctx.moveTo(PLOT_LEFT + i, y);
          ctx.lineTo(PLOT_LEFT + i + 1, y);
          ctx.stroke();
          continue;
        }
        const x = PLOT_LEFT + i;
        const mn = d.min[i]!;
        const mx = d.max[i]!;
        const last = d.last[i]!;
        if (mx >= DIGITAL_Z) {
          // Z or X present in this column.
          const isX = mx === DIGITAL_X || mn === DIGITAL_X;
          ctx.strokeStyle = isX ? colors.x : colors.z;
          ctx.setLineDash(isX ? [] : [2, 2]);
          ctx.beginPath();
          if (isX) {
            ctx.moveTo(x, yHigh);
            ctx.lineTo(x, yLow);
          } else {
            ctx.moveTo(x, yMid);
            ctx.lineTo(x + 1, yMid);
          }
          ctx.stroke();
          ctx.setLineDash([]);
        } else {
          ctx.strokeStyle = color;
          ctx.beginPath();
          if (mn !== mx || (prevLevel !== null && prevLevel <= 1 && prevLevel !== last)) {
            ctx.moveTo(x + 0.5, yHigh);
            ctx.lineTo(x + 0.5, yLow);
          }
          const y = last === 1 ? yHigh : yLow;
          ctx.moveTo(x, y);
          ctx.lineTo(x + 1, y);
          ctx.stroke();
        }
        prevLevel = last;
      }
    });

    const drawCursor = (t: Time | null, c: string, name: string) => {
      if (t === null || t < win.start || t > end) return;
      const x = Math.round(PLOT_LEFT + timeToX(t, win, pw)) + 0.5;
      ctx.strokeStyle = c;
      ctx.setLineDash([4, 3]);
      ctx.beginPath();
      ctx.moveTo(x, PLOT_TOP);
      ctx.lineTo(x, PLOT_TOP + ph);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = c;
      ctx.textAlign = 'left';
      ctx.textBaseline = 'top';
      ctx.fillText(name, x + 3, PLOT_TOP + 1);
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

  useEffect(() => {
    redraw();
  }, [redraw, win, cursors, now, revision, channels, selected, annotations]);
  const handlers = useTimeInteraction(wrapRef, { window: win, onWindowChange, cursors, onCursorsChange, plotLeft: PLOT_LEFT, width: size.width });

  const level = (v: number) => (Number.isNaN(v) ? '—' : v === DIGITAL_Z ? 'Z' : v === DIGITAL_X ? 'X' : String(v));

  return (
    <div className={`es-instrument ${className ?? ''}`.trim()}>
      <div
        ref={wrapRef}
        className="es-instrument__plot"
        role="img"
        aria-label={`${label}. ${visible.length} channels. Window ${formatTime(win.start, { padZeros: false })} to ${formatTime(end, { padZeros: false })}.${
          measurements && current ? ` ${current.record.label}: ${measurements.edges} rising edges${measurements.pf.frequencyHz ? `, ${formatFrequency(measurements.pf.frequencyHz)}` : ''}.` : ''
        } Arrow keys pan, plus and minus zoom, A and B place cursors.`}
        tabIndex={0}
        {...handlers}
      >
        <canvas ref={ref} className="es-instrument__canvas" />
        {visible.length === 0 && <div className="es-instrument__empty">No digital probes subscribed</div>}
      </div>
      <div className="es-instrument__strip">
        <div className="es-instrument__traces" role="radiogroup" aria-label="Measured channel">
          {channels.map((c) => (
            <button key={c.record.id} type="button" role="radio" aria-checked={current?.record.id === c.record.id} className="es-instrument__trace" onClick={() => onSelect?.(c.record.id)}>
              {c.record.label}
            </button>
          ))}
        </div>
        {measurements && current && (
          <dl className="es-instrument__measurements">
            <Measure label="edges" value={String(measurements.edges)} />
            <Measure label="period" value={measurements.pf.period !== null ? formatTime(measurements.pf.period, { padZeros: false }) : '—'} />
            <Measure label="freq" value={measurements.pf.frequencyHz !== null ? formatFrequency(measurements.pf.frequencyHz) : '—'} />
            {cursors.a !== null && <Measure label="A" value={`${formatTime(cursors.a, { padZeros: false })} · ${level(measurements.la)}`} />}
            {cursors.b !== null && <Measure label="B" value={`${formatTime(cursors.b, { padZeros: false })} · ${level(measurements.lb)}`} />}
            {cursors.a !== null && cursors.b !== null && <Measure label="Δ" value={formatTime(cursors.b - cursors.a, { padZeros: false })} />}
          </dl>
        )}
      </div>
    </div>
  );
}
