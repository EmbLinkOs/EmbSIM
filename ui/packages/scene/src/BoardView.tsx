// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// BoardView: board or component artwork with hotspots (SPEC-005 §8,
// SPEC-011 §5). Pointer gestures act on the artwork element; an HTML overlay
// control positioned over each hotspot gives the keyboard equivalent and the
// ARIA state (Space holds, Enter toggles, arrows drag). State bindings map
// outputs to attributes or classes of artwork elements.

import type { ArtworkState, Hotspot, OutputValue } from '@embsim/protocol';
import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState, type CSSProperties } from 'react';
import { parseSvg, scopeArtwork } from './svg';

export interface BoardItem {
  id: string;
  /** Human label ("SW1 Push button"). */
  label: string;
  svg: string;
  x: number;
  y: number;
  width: number;
  height: number;
  hotspots?: Hotspot[];
  states?: ArtworkState[];
  /** Current input values of this item (mirrors the session). */
  inputs?: Record<string, unknown>;
  /** Current outputs of this item, keyed by output name. */
  outputs?: Record<string, OutputValue>;
  /** CSS custom properties set on the item, which its artwork may read
   * (`--led-color` for an LED's colour). */
  vars?: Record<string, string>;
}

/** One end of a bench wire: a pin of an item, found as `#pin-<name>` in its
 * artwork, or else spread along the artwork's bottom edge by `order` of `count`. */
export interface WireEnd {
  item: string;
  pin: string;
  order?: number;
  count?: number;
}

/** A net drawn on the bench: wires joining its ends, in one colour. */
export interface BenchWire {
  net: string;
  color: string;
  ends: WireEnd[];
}

interface Point {
  x: number;
  y: number;
}

/** Where a pin leaves its part: up, down, left or right of it. */
type Side = 'up' | 'down' | 'left' | 'right';

interface Anchor extends Point {
  side: Side;
}

interface Rect {
  x: number;
  y: number;
  width: number;
  height: number;
}

/** A vertical run at `x` from `y0` to `y1` passes through `r`. */
function crosses(r: Rect, x: number, y0: number, y1: number): boolean {
  const lo = Math.min(y0, y1);
  const hi = Math.max(y0, y1);
  return x > r.x + 1 && x < r.x + r.width - 1 && hi > r.y + 1 && lo < r.y + r.height - 1;
}

/**
 * The wire from `a` to `b`: out of each part by a stub, then square across.
 * A vertical run that would cross a part on the bench (`obstacles`) goes
 * round it instead, down the nearer side, as a wire laid on a desk does.
 */
export function routeWire(a: Anchor, b: Anchor, lane: number, obstacles: Rect[] = []): string {
  const stub = 10 + lane * 3;
  const out = (p: Anchor): Point => ({
    x: p.side === 'left' ? p.x - stub : p.side === 'right' ? p.x + stub : p.x,
    y: p.side === 'up' ? p.y - stub : p.side === 'down' ? p.y + stub : p.y,
  });
  const a2 = out(a);
  const b2 = out(b);
  const vertical = a.side === 'up' || a.side === 'down';
  let mid: Point[] = [vertical ? { x: b2.x, y: a2.y } : { x: a2.x, y: b2.y }];
  const runX = vertical ? b2.x : a2.x;
  const hit = obstacles.find((r) => crosses(r, runX, a2.y, b2.y));
  if (hit) {
    const left = hit.x - 8 - lane * 4;
    const right = hit.x + hit.width + 8 + lane * 4;
    const x = Math.abs(runX - left) <= Math.abs(runX - right) ? left : right;
    mid = [{ x, y: a2.y }, { x, y: b2.y }];
  }
  const pts = [a, a2, ...mid, b2, b].map((p) => `${p.x.toFixed(1)} ${p.y.toFixed(1)}`);
  return `M ${pts.join(' L ')}`;
}

export interface BoardViewProps {
  items: BoardItem[];
  /** Nets drawn as wires between the items' pins (the bench). */
  wires?: BenchWire[];
  /** Overall drawing size in artwork units; default fits the items. */
  width?: number;
  height?: number;
  onInput?: (target: string, input: string, value: unknown) => void;
  disabled?: boolean;
  label?: string;
  className?: string;
}

interface Overlay {
  key: string;
  item: BoardItem;
  hotspot: Hotspot;
  rect: { left: number; top: number; width: number; height: number };
}

interface StateOverlay {
  key: string;
  text: string;
  rect: { left: number; top: number; width: number; height: number };
}

/** The key of a hotspot's input in an item's inputs: "pressed", or "SW_RESET.pressed" on a board. */
const inputKey = (h: Hotspot) => (h.target ? `${h.target}.${h.input}` : h.input);

/** CSS.escape with a fallback for environments without it (jsdom). */
function cssEscape(s: string): string {
  if (typeof CSS !== 'undefined' && typeof CSS.escape === 'function') return CSS.escape(s);
  return s.replace(/[^a-zA-Z0-9_-]/g, (ch) => `\\${ch}`);
}

/** Sets an attribute, or the element's text for the pseudo-attribute "text"; an empty value removes the attribute. */
function setValue(el: Element, attr: string, text: string): void {
  if (attr === 'text') el.textContent = text;
  else if (text === '') el.removeAttribute(attr);
  else el.setAttribute(attr, text);
}

/**
 * Applies one artwork state (SPEC-005 §8): a class toggled by truth; an
 * enum or integer looked up in `map`; a value put into the `format`
 * template ("rotate({} 30 30)", "{}" for a colour, a path or text), after
 * `scale` for numbers; a number scaled into the attribute; a number as
 * opacity; otherwise `true`/`false` strings.
 */
export function applyState(root: Element, state: ArtworkState, value: OutputValue | undefined): void {
  const el = root.querySelector(state.selector);
  if (!el) return;
  if (state.class) {
    el.classList.toggle(state.class, Boolean(value));
    return;
  }
  const attr = state.attr ?? 'opacity';
  if (state.map && value !== undefined) {
    const m = state.map[String(value)];
    if (m !== undefined) {
      setValue(el, attr, m);
      return;
    }
  }
  let v: OutputValue | undefined = value;
  if (typeof value === 'number' && state.scale) {
    const [lo, hi] = Array.isArray(state.scale) ? state.scale : state.scale.from;
    const [a, b] = Array.isArray(state.scale) ? [0, 1] : (state.scale.to ?? [0, 1]);
    const t = hi === lo ? 0 : Math.max(0, Math.min(1, (value - lo) / (hi - lo)));
    v = a + t * (b - a);
    if (state.format === undefined) {
      setValue(el, attr, attr === 'opacity' ? v.toFixed(3) : String(v));
      return;
    }
  }
  if (state.format !== undefined) {
    const text = typeof v === 'number' && Number.isFinite(v) && state.precision !== undefined ? v.toFixed(Math.max(0, Math.min(6, state.precision))) : String(v);
    setValue(el, attr, value === undefined ? '' : state.format.split('{}').join(text));
    return;
  }
  if (typeof value === 'number' && attr === 'opacity') {
    el.setAttribute(attr, Math.max(0, Math.min(1, value)).toFixed(3));
    return;
  }
  setValue(el, attr, value ? state.true ?? '1' : state.false ?? '');
}

function describeOutput(name: string, value: OutputValue | undefined): string {
  if (typeof value === 'boolean') return `${name} ${value ? 'on' : 'off'}`;
  if (typeof value === 'number') return `${name} ${Math.round(value * 100)}%`;
  return `${name} ${String(value ?? '')}`;
}

export function BoardView({ items, wires, width, height, onInput, disabled, label = 'Board', className }: BoardViewProps) {
  const svgRef = useRef<SVGSVGElement>(null);
  const wrapRef = useRef<HTMLDivElement>(null);
  const [overlays, setOverlays] = useState<Overlay[]>([]);
  const [stateOverlays, setStateOverlays] = useState<StateOverlay[]>([]);
  const [dragValue, setDragValue] = useState<Record<string, number>>({});
  const [announcement, setAnnouncement] = useState('');
  const [wirePaths, setWirePaths] = useState<{ key: string; d: string; color: string; ends: Point[] }[]>([]);

  const bounds = useMemo(() => {
    if (width && height) return { width, height };
    const maxX = Math.max(40, ...items.map((i) => i.x + i.width));
    const maxY = Math.max(40, ...items.map((i) => i.y + i.height));
    return { width: maxX + 20, height: maxY + 20 };
  }, [items, width, height]);

  const parsed = useMemo(() => items.map((i) => parseSvg(i.svg)), [items]);

  // Apply state bindings whenever outputs change.
  useEffect(() => {
    const svg = svgRef.current;
    if (!svg) return;
    for (const item of items) {
      const root = svg.querySelector(`[data-board-item="${cssEscape(item.id)}"]`);
      if (!root) continue;
      // an output the session does not report (an LED's `lit` under the
      // electrical tier) leaves the artwork to the outputs it does report
      for (const s of item.states ?? []) {
        const v = item.outputs?.[s.output];
        if (v !== undefined) applyState(root, s, v);
      }
    }
  }, [items]);

  // Position overlay controls over hotspots (and state elements for announcements).
  const measure = useCallback(() => {
    const svg = svgRef.current;
    const wrap = wrapRef.current;
    if (!svg || !wrap) return;
    const wrapRect = wrap.getBoundingClientRect();
    const next: Overlay[] = [];
    const nextStates: StateOverlay[] = [];
    for (const item of items) {
      const root = svg.querySelector(`[data-board-item="${cssEscape(item.id)}"]`);
      if (!root) continue;
      for (const h of item.hotspots ?? []) {
        const el = root.querySelector(h.selector);
        if (!el) continue;
        const r = el.getBoundingClientRect();
        next.push({ key: `${item.id}:${h.target ?? ''}:${h.input}`, item, hotspot: h, rect: { left: r.left - wrapRect.left, top: r.top - wrapRect.top, width: r.width, height: r.height } });
      }
      const seen = new Set<string>();
      for (const s of item.states ?? []) {
        if (seen.has(s.output)) continue;
        seen.add(s.output);
        const el = root.querySelector(s.selector);
        const r = (el ?? root).getBoundingClientRect();
        nextStates.push({ key: `${item.id}:${s.output}`, text: `${item.label}: ${describeOutput(s.output, item.outputs?.[s.output])}`, rect: { left: r.left - wrapRect.left, top: r.top - wrapRect.top, width: Math.max(r.width, 4), height: Math.max(r.height, 4) } });
      }
    }
    setOverlays(next);
    setStateOverlays(nextStates);
  }, [items]);

  // The wires: each end's anchor in the drawing's own units, then each end
  // joined to the nearest end already wired.
  useLayoutEffect(() => {
    const svg = svgRef.current;
    if (!svg || !wires?.length) {
      setWirePaths([]);
      return;
    }
    const byId = new Map(items.map((i) => [i.id, i]));
    const rootCtm = typeof svg.getCTM === 'function' ? svg.getCTM() : null;
    const anchor = (end: WireEnd): Anchor | null => {
      const item = byId.get(end.item);
      if (!item) return null;
      const root = svg.querySelector(`[data-board-item="${cssEscape(item.id)}"]`);
      const el = root?.querySelector(`#pin-${cssEscape(end.pin.toLowerCase())}`) as SVGGraphicsElement | null;
      let p: Point | null = null;
      if (el && rootCtm && typeof el.getBBox === 'function' && typeof el.getCTM === 'function') {
        const b = el.getBBox();
        const m = el.getCTM();
        if (m) {
          const pt = new DOMPoint(b.x + b.width / 2, b.y + b.height / 2).matrixTransform(m).matrixTransform(rootCtm.inverse());
          p = { x: pt.x, y: pt.y };
        }
      }
      if (!p) {
        const n = Math.max(1, end.count ?? 1);
        p = { x: item.x + (item.width * ((end.order ?? 0) + 1)) / (n + 1), y: item.y + item.height };
      }
      const fx = (p.x - item.x) / (item.width || 1);
      const fy = (p.y - item.y) / (item.height || 1);
      const side: Side = fy <= 0.3 ? 'up' : fy >= 0.7 ? 'down' : fx < 0.5 ? 'left' : 'right';
      return { ...p, side };
    };
    const paths: { key: string; d: string; color: string; ends: Point[] }[] = [];
    wires.forEach((w, lane) => {
      const pts = w.ends.map(anchor).filter((a): a is Anchor => a !== null);
      if (pts.length < 2) return;
      const done: Anchor[] = [pts[0]!];
      const segs: string[] = [];
      for (const p of pts.slice(1)) {
        let best = done[0]!;
        for (const q of done) if (Math.abs(q.x - p.x) + Math.abs(q.y - p.y) < Math.abs(best.x - p.x) + Math.abs(best.y - p.y)) best = q;
        segs.push(routeWire(best, p, lane % 6, items));
        done.push(p);
      }
      paths.push({ key: w.net, d: segs.join(' '), color: w.color, ends: pts });
    });
    setWirePaths(paths);
  }, [items, wires]);

  useLayoutEffect(() => {
    measure();
    const wrap = wrapRef.current;
    if (!wrap) return;
    const ro = new ResizeObserver(() => measure());
    ro.observe(wrap);
    return () => ro.disconnect();
  }, [measure]);

  const set = useCallback(
    (item: BoardItem, hotspot: Hotspot, value: unknown, say?: string) => {
      if (disabled) return;
      onInput?.(hotspot.target ? `${item.id}.${hotspot.target}` : item.id, hotspot.input, value);
      if (say) setAnnouncement(say);
    },
    [disabled, onInput],
  );

  const style: CSSProperties = { aspectRatio: `${bounds.width} / ${bounds.height}` };

  return (
    <div ref={wrapRef} className={`es-board ${className ?? ''}`.trim()} style={style}>
      <svg ref={svgRef} className="es-board__svg" viewBox={`0 0 ${bounds.width} ${bounds.height}`} role="img" aria-label={label} preserveAspectRatio="xMidYMid meet">
        {items.map((item, i) => {
          const p = parsed[i]!;
          const vb = p.viewBox ?? { x: 0, y: 0, width: item.width, height: item.height };
          const sx = item.width / (vb.width || 1);
          const sy = item.height / (vb.height || 1);
          return (
            <g
              key={item.id}
              data-board-item={item.id}
              style={item.vars as CSSProperties | undefined}
              transform={`translate(${item.x} ${item.y}) scale(${sx} ${sy}) translate(${-vb.x} ${-vb.y})`}
            >
              <g dangerouslySetInnerHTML={{ __html: scopeArtwork(p.inner, item.id) }} />
            </g>
          );
        })}
        <g className="es-board__wires" aria-hidden="true">
          {wirePaths.map((w) => (
            <g key={w.key} data-net={w.key}>
              <path d={w.d} fill="none" stroke="rgba(0,0,0,0.45)" strokeWidth={4} strokeLinecap="round" strokeLinejoin="round" />
              <path d={w.d} fill="none" stroke={w.color} strokeWidth={2.4} strokeLinecap="round" strokeLinejoin="round" />
              {w.ends.map((p, i) => (
                <circle key={i} cx={p.x} cy={p.y} r={2.2} fill={w.color} stroke="rgba(0,0,0,0.5)" strokeWidth={0.8} />
              ))}
            </g>
          ))}
        </g>
        {items.map((item) => (
          <text key={`${item.id}-label`} className="es-board__label" x={item.x} y={item.y - 6}>
            {item.label}
          </text>
        ))}
      </svg>
      {overlays.map((o) => {
        const { item, hotspot: h } = o;
        const pos: CSSProperties = { left: o.rect.left, top: o.rect.top, width: o.rect.width, height: o.rect.height };
        const name = `${item.label} ${h.label ?? (h.target ? `${h.target} ${h.input}` : h.input)}`;
        if (h.gesture === 'drag') {
          const min = h.min ?? 0;
          const max = h.max ?? 1;
          const step = h.step ?? (max - min) / 100;
          const current = dragValue[o.key] ?? Number(item.inputs?.[inputKey(h)] ?? min);
          const commit = (v: number) => {
            const clamped = Math.max(min, Math.min(max, v));
            setDragValue((d) => ({ ...d, [o.key]: clamped }));
            set(item, h, clamped, `${name} ${clamped.toFixed(2)}`);
          };
          return (
            <div
              key={o.key}
              className="es-board__hotspot es-board__hotspot--drag"
              style={pos}
              role="slider"
              tabIndex={disabled ? -1 : 0}
              aria-label={name}
              aria-valuemin={min}
              aria-valuemax={max}
              aria-valuenow={current}
              aria-valuetext={current.toFixed(2)}
              aria-disabled={disabled}
              onKeyDown={(e) => {
                const up = e.key === 'ArrowUp' || e.key === 'ArrowRight';
                const down = e.key === 'ArrowDown' || e.key === 'ArrowLeft';
                if (up || down) {
                  e.preventDefault();
                  commit(current + (up ? step : -step) * (e.shiftKey ? 10 : 1));
                } else if (e.key === 'Home') commit(min);
                else if (e.key === 'End') commit(max);
              }}
              onPointerDown={(e) => {
                if (disabled) return;
                e.preventDefault();
                e.currentTarget.setPointerCapture(e.pointerId);
                const start = h.axis === 'y' ? e.clientY : e.clientX;
                const startValue = current;
                const extent = h.axis === 'y' ? o.rect.height : o.rect.width;
                const move = (ev: PointerEvent) => {
                  const d = (h.axis === 'y' ? ev.clientY : ev.clientX) - start;
                  const sign = h.axis === 'y' ? -1 : 1;
                  commit(startValue + (sign * d * (max - min)) / Math.max(1, extent));
                };
                const target = e.currentTarget;
                const up = () => {
                  target.removeEventListener('pointermove', move);
                  target.removeEventListener('pointerup', up);
                };
                target.addEventListener('pointermove', move);
                target.addEventListener('pointerup', up);
              }}
            >
              <span className="es-board__readout">{current.toFixed(2)}</span>
            </div>
          );
        }
        const pressed = Boolean(item.inputs?.[inputKey(h)]);
        if (h.gesture === 'toggle') {
          return (
            <button
              key={o.key}
              type="button"
              className="es-board__hotspot"
              style={pos}
              aria-label={name}
              aria-pressed={pressed}
              disabled={disabled}
              onClick={() => set(item, h, !pressed, `${name} ${!pressed ? 'on' : 'off'}`)}
            />
          );
        }
        // hold: press on down, release on up; Space holds while down; Enter toggles.
        return (
          <button
            key={o.key}
            type="button"
            className="es-board__hotspot"
            style={pos}
            aria-label={name}
            aria-pressed={pressed}
            disabled={disabled}
            onPointerDown={(e) => {
              if (e.button !== 0) return;
              e.preventDefault();
              e.currentTarget.setPointerCapture(e.pointerId);
              set(item, h, true, `${name} pressed`);
            }}
            onPointerUp={() => set(item, h, false, `${name} released`)}
            onPointerCancel={() => set(item, h, false, `${name} released`)}
            onKeyDown={(e) => {
              if (e.key === ' ' && !e.repeat) {
                e.preventDefault();
                set(item, h, true, `${name} pressed`);
              } else if (e.key === 'Enter') {
                e.preventDefault();
                set(item, h, !pressed, `${name} ${!pressed ? 'pressed' : 'released'}`);
              }
            }}
            onKeyUp={(e) => {
              if (e.key === ' ') {
                e.preventDefault();
                set(item, h, false, `${name} released`);
              }
            }}
            onBlur={() => {
              if (pressed) set(item, h, false, `${name} released`);
            }}
          />
        );
      })}
      {stateOverlays.map((s) => (
        <span key={s.key} className="es-board__state" style={{ left: s.rect.left, top: s.rect.top, width: s.rect.width, height: s.rect.height }} role="img" aria-label={s.text} />
      ))}
      <div className="es-sr-only" aria-live="polite">
        {announcement}
      </div>
    </div>
  );
}
