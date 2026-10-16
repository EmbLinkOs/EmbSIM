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
import { parseSvg } from './svg';

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
}

export interface BoardViewProps {
  items: BoardItem[];
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

/** CSS.escape with a fallback for environments without it (jsdom). */
function cssEscape(s: string): string {
  if (typeof CSS !== 'undefined' && typeof CSS.escape === 'function') return CSS.escape(s);
  return s.replace(/[^a-zA-Z0-9_-]/g, (ch) => `\\${ch}`);
}

function applyState(root: Element, state: ArtworkState, value: OutputValue | undefined): void {
  const el = root.querySelector(state.selector);
  if (!el) return;
  if (state.class) {
    el.classList.toggle(state.class, Boolean(value));
    return;
  }
  const attr = state.attr ?? 'opacity';
  if (typeof value === 'number' && state.scale) {
    const [lo, hi] = state.scale;
    const t = hi === lo ? 0 : Math.max(0, Math.min(1, (value - lo) / (hi - lo)));
    el.setAttribute(attr, attr === 'opacity' ? t.toFixed(3) : String(t));
    return;
  }
  if (typeof value === 'number' && attr === 'opacity') {
    el.setAttribute(attr, Math.max(0, Math.min(1, value)).toFixed(3));
    return;
  }
  const text = value ? state.true ?? '1' : state.false ?? '';
  if (text === '') el.removeAttribute(attr);
  else el.setAttribute(attr, text);
}

function describeOutput(name: string, value: OutputValue | undefined): string {
  if (typeof value === 'boolean') return `${name} ${value ? 'on' : 'off'}`;
  if (typeof value === 'number') return `${name} ${Math.round(value * 100)}%`;
  return `${name} ${String(value ?? '')}`;
}

export function BoardView({ items, width, height, onInput, disabled, label = 'Board', className }: BoardViewProps) {
  const svgRef = useRef<SVGSVGElement>(null);
  const wrapRef = useRef<HTMLDivElement>(null);
  const [overlays, setOverlays] = useState<Overlay[]>([]);
  const [stateOverlays, setStateOverlays] = useState<StateOverlay[]>([]);
  const [dragValue, setDragValue] = useState<Record<string, number>>({});
  const [announcement, setAnnouncement] = useState('');

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
      for (const s of item.states ?? []) applyState(root, s, item.outputs?.[s.output]);
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
        next.push({ key: `${item.id}:${h.input}`, item, hotspot: h, rect: { left: r.left - wrapRect.left, top: r.top - wrapRect.top, width: r.width, height: r.height } });
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
      onInput?.(item.id, hotspot.input, value);
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
            <g key={item.id} data-board-item={item.id} transform={`translate(${item.x} ${item.y}) scale(${sx} ${sy}) translate(${-vb.x} ${-vb.y})`}>
              <g dangerouslySetInnerHTML={{ __html: p.inner }} />
            </g>
          );
        })}
        {items.map((item) => (
          <text key={`${item.id}-label`} className="es-board__label" x={item.x} y={item.y + item.height + 12}>
            {item.id}
          </text>
        ))}
      </svg>
      {overlays.map((o) => {
        const { item, hotspot: h } = o;
        const pos: CSSProperties = { left: o.rect.left, top: o.rect.top, width: o.rect.width, height: o.rect.height };
        const name = `${item.label} ${h.label ?? h.input}`;
        if (h.gesture === 'drag') {
          const min = h.min ?? 0;
          const max = h.max ?? 1;
          const step = h.step ?? (max - min) / 100;
          const current = dragValue[o.key] ?? Number(item.inputs?.[h.input] ?? min);
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
        const pressed = Boolean(item.inputs?.[h.input]);
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
