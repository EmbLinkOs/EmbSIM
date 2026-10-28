// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { useCallback, useEffect, useRef, useState } from 'react';
import type { Point } from './geometry';

/** A movable observation lead. A canceled/invalid drop leaves the old net intact. */
export function MeasurementProbe({ side, net, at, onDrop }: { side: 'a' | 'b'; net: string; at: Point; onDrop?: (side: 'a' | 'b', client: Point) => void }) {
  const drag = useRef<{ x: number; y: number; element: SVGGElement; pointerId: number } | null>(null);
  const [delta, setDelta] = useState<Point>({ x: 0, y: 0 });
  const cancel = useCallback(() => {
    const capture = drag.current; drag.current = null;
    if (capture?.element.hasPointerCapture(capture.pointerId)) capture.element.releasePointerCapture(capture.pointerId);
    setDelta({ x: 0, y: 0 });
  }, []);
  useEffect(() => {
    const key = (e: KeyboardEvent) => { if (e.key === 'Escape' && drag.current) { e.preventDefault(); e.stopPropagation(); cancel(); } };
    window.addEventListener('keydown',key,true); window.addEventListener('blur',cancel);
    return () => { window.removeEventListener('keydown',key,true); window.removeEventListener('blur',cancel); };
  }, [cancel]);
  return <g className={`es-comparison-probe es-comparison-probe--${side}`} role="img" aria-label={`Voltage probe ${side.toUpperCase()} on ${net}`} transform={`translate(${at.x + delta.x} ${at.y + delta.y})`}
    onPointerDown={(e) => { if (!onDrop || e.button !== 0) return; e.stopPropagation(); e.preventDefault(); e.currentTarget.setPointerCapture(e.pointerId); drag.current = { x: e.clientX, y: e.clientY, element: e.currentTarget, pointerId: e.pointerId }; }}
    onPointerMove={(e) => { if (!drag.current) return; e.stopPropagation(); const inverse = e.currentTarget.getScreenCTM()?.inverse(); const x = e.clientX - drag.current.x, y = e.clientY - drag.current.y; setDelta(inverse ? { x: inverse.a * x + inverse.c * y, y: inverse.b * x + inverse.d * y } : { x, y }); }}
    onPointerUp={(e) => { if (!drag.current) return; e.stopPropagation(); cancel(); if (e.currentTarget.hasPointerCapture(e.pointerId)) e.currentTarget.releasePointerCapture(e.pointerId); onDrop?.(side, { x: e.clientX, y: e.clientY }); }}
    onPointerCancel={cancel} onLostPointerCapture={cancel}>
    <circle r="8"/><path d={side === 'a' ? 'M0 0L-12 -20H-36V-7H-15Z' : 'M0 0L12 -20H36V-7H15Z'}/><text x={side === 'a' ? -27 : 19} y="-10">{side.toUpperCase()}</text><title>Drag onto another wire or pin. Select nets by keyboard in Voltage comparison.</title>
  </g>;
}
