// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { Hotspot, InputEntry } from '@embsim/protocol';
import { useCallback, useEffect, useRef, useState, type RefObject } from 'react';

interface Box { x: number; y: number; width: number; height: number }
export interface SymbolControlsProps {
  root: RefObject<SVGGElement | null>;
  id: string;
  hotspots: Hotspot[];
  inputs: InputEntry[];
  enabled: boolean;
  onInput?: (target: string, input: string, value: unknown) => void;
}

export function SymbolControls({ root, id, hotspots, inputs, enabled, onInput }: SymbolControlsProps) {
  const [boxes, setBoxes] = useState<(Box | null)[]>([]);
  useEffect(() => {
    setBoxes(hotspots.map((h) => {
      try { const el = root.current?.querySelectorAll<SVGGraphicsElement>(h.selector)[0]; const b = el?.getBBox(); return b && b.width + b.height > 0 ? { x: b.x, y: b.y, width: b.width, height: b.height } : null; }
      catch { return null; }
    }));
  }, [root, hotspots]);
  return <>{hotspots.map((h, i) => {
    const box = boxes[i];
    if (!box) return null;
    const target = h.target ? `${id}.${h.target}` : id;
    const entry = inputs.find((e) => e.target === target && e.input === h.input);
    // The runtime decides which controls exist and their effective limits.
    return entry ? <Control key={`${target}.${h.input}`} box={box} hotspot={h} target={target} entry={entry} enabled={enabled} onInput={onInput} /> : null;
  })}</>;
}

function Control({ box, hotspot: h, target, entry, enabled, onInput }: { box: Box; hotspot: Hotspot; target: string; entry: InputEntry; enabled: boolean; onInput?: SymbolControlsProps['onInput'] }) {
  const sendRef = useRef(onInput); sendRef.current = onInput;
  const held = useRef(false);
  const drag = useRef<{ x: number; y: number; value: number; angle: number; turns: number } | null>(null);
  const [preview, setPreview] = useState<number | null>(null);
  const min = entry.min ?? h.min ?? h.range?.[0] ?? 0;
  const max = entry.max ?? h.max ?? h.range?.[1] ?? 1;
  const step = h.step ?? (max - min) / 100;
  const value = preview ?? Number(entry.value ?? min);
  useEffect(() => { if (!drag.current) setPreview(null); }, [entry.value]);
  const label = entry.label ?? `${target} ${h.label ?? h.input}`;
  const release = useCallback(() => {
    if (held.current) { held.current = false; sendRef.current?.(target, entry.input, false); }
    drag.current = null; setPreview(null);
  }, [target, entry.input]);
  useEffect(() => {
    const visibility = () => { if (document.hidden) release(); };
    window.addEventListener('blur', release); document.addEventListener('visibilitychange', visibility);
    return () => { window.removeEventListener('blur', release); document.removeEventListener('visibilitychange', visibility); release(); };
  }, [release]);
  useEffect(() => { if (!enabled) release(); }, [enabled, release]);
  const localPoint = (el: SVGGElement, x: number, y: number) => {
    const matrix = el.getScreenCTM();
    if (!matrix) return { x, y };
    const inverse = matrix.inverse();
    return { x: inverse.a * x + inverse.c * y + inverse.e, y: inverse.b * x + inverse.d * y + inverse.f };
  };
  const commit = (v: number) => { const bounded = Math.max(min, Math.min(max, v)); setPreview(bounded); sendRef.current?.(target, entry.input, bounded); };
  return <g data-live-control={`${target}.${entry.input}`} className={`es-symbol-control${h.gesture === 'drag' ? ' es-symbol-control--drag' : ''}`} role={h.gesture === 'drag' ? 'slider' : 'button'} tabIndex={enabled ? 0 : -1} aria-label={label} aria-disabled={!enabled} aria-pressed={h.gesture !== 'drag' ? Boolean(entry.value) : undefined} aria-valuemin={h.gesture === 'drag' ? min : undefined} aria-valuemax={h.gesture === 'drag' ? max : undefined} aria-valuenow={h.gesture === 'drag' ? value : undefined}
    onPointerDown={(e) => {
      e.stopPropagation(); if (!enabled || e.button !== 0) return; e.preventDefault(); e.currentTarget.focus(); e.currentTarget.setPointerCapture(e.pointerId);
      if (h.gesture === 'hold') { held.current = true; sendRef.current?.(target, entry.input, true); }
      else if (h.gesture === 'toggle') sendRef.current?.(target, entry.input, !entry.value);
      else { const point = localPoint(e.currentTarget, e.clientX, e.clientY); drag.current = { ...point, value: Number(entry.value ?? min), angle: Math.atan2(point.y - box.y - box.height / 2, point.x - box.x - box.width / 2), turns: 0 }; }
    }}
    onPointerMove={(e) => {
      if (!drag.current || !enabled) return; e.stopPropagation();
      const point = localPoint(e.currentTarget, e.clientX, e.clientY);
      if (h.axis === 'angle') {
        const angle = Math.atan2(point.y - box.y - box.height / 2, point.x - box.x - box.width / 2);
        const change = Math.atan2(Math.sin(angle - drag.current.angle), Math.cos(angle - drag.current.angle));
        drag.current.turns += change; drag.current.angle = angle;
        commit(drag.current.value + drag.current.turns / ((h.travel ?? 270) * Math.PI / 180) * (max - min));
      } else {
        const delta = h.axis === 'y' ? point.y - drag.current.y : point.x - drag.current.x;
        commit(drag.current.value + delta / (h.travel ?? Math.max(20, h.axis === 'y' ? box.height : box.width)) * (max - min));
      }
    }} onPointerUp={(e) => { e.stopPropagation(); release(); if (e.currentTarget.hasPointerCapture(e.pointerId)) e.currentTarget.releasePointerCapture(e.pointerId); }} onPointerCancel={release} onLostPointerCapture={release} onBlur={release}
    onKeyDown={(e) => {
      if (!enabled) return;
      if (h.gesture === 'drag') {
        if (['ArrowLeft', 'ArrowDown', 'ArrowRight', 'ArrowUp', 'Home', 'End'].includes(e.key)) {
          e.stopPropagation(); e.preventDefault(); commit(e.key === 'Home' ? min : e.key === 'End' ? max : value + (['ArrowLeft', 'ArrowDown'].includes(e.key) ? -step : step) * (e.shiftKey ? 10 : 1));
        }
      } else if ((e.key === ' ' || e.key === 'Enter') && !e.repeat) {
        e.stopPropagation(); e.preventDefault(); if (h.gesture === 'hold') { held.current = true; sendRef.current?.(target, entry.input, true); } else sendRef.current?.(target, entry.input, !entry.value);
      }
      if (e.key === 'Escape') { e.stopPropagation(); e.preventDefault(); release(); }
    }} onKeyUp={(e) => { e.stopPropagation(); if (h.gesture === 'hold' && (e.key === ' ' || e.key === 'Enter')) { e.preventDefault(); release(); } }}>
    <rect x={box.x - 2} y={box.y - 2} width={Math.max(8, box.width + 4)} height={Math.max(8, box.height + 4)} rx={3} />
    <title>{h.gesture === 'drag' ? `Drag or use arrow keys · ${value.toFixed(2)} ${entry.unit ?? ''}` : h.gesture === 'hold' ? 'Hold with pointer, Space or Enter' : 'Click, Space or Enter to toggle'}</title>
  </g>;
}
