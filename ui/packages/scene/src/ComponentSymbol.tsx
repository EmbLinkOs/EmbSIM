// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { InputEntry, Layout, OutputValue, SymbolDescription } from '@embsim/protocol';
import { memo, useEffect, useId, useMemo, useRef } from 'react';
import { symbolTransform } from './geometry';
import { parseSvg, scopeArtwork } from './svg';
import { applyState } from './artworkState';
import { SymbolControls } from './SymbolControls';

export interface ComponentSymbolProps {
  /** The catalog symbol.svg source. */
  svg: string;
  id?: string;
  emitterColor?: string;
  outputs?: Record<string, OutputValue>;
  inputs?: InputEntry[];
  interactive?: boolean;
  onInput?: (target: string, input: string, value: unknown) => void;
  symbol: SymbolDescription;
  layout: Layout;
  /** Show pin anchor dots. */
  showPins?: boolean;
  selected?: boolean;
  /** Fallback when the SVG does not parse: a labelled rectangle (SPEC-005 §8). */
  fallbackLabel?: string;
}

/**
 * Renders a catalog symbol at its layout as a scene-graph group. The symbol
 * is inlined markup; colours come from --symbol-stroke / --symbol-fill.
 */
export const ComponentSymbol = memo(function ComponentSymbol({ svg, symbol, layout, showPins, selected, fallbackLabel, id, outputs, inputs = [], interactive = false, onInput, emitterColor }: ComponentSymbolProps) {
  const parsed = useMemo(() => parseSvg(svg), [svg]);
  const transform = symbolTransform(layout, symbol);
  const broken = !parsed.inner;
  const root = useRef<SVGGElement>(null);
  const identity = id ?? fallbackLabel ?? 'symbol';
  const markup = useMemo(() => scopeArtwork(parsed.inner, `symbol-${identity}`), [parsed.inner, identity]);
  const originals = useMemo(() => {
    const doc = new DOMParser().parseFromString(`<svg xmlns="http://www.w3.org/2000/svg">${markup}</svg>`, 'image/svg+xml');
    return (symbol.states ?? []).map((state) => {
      try { const base = doc.querySelector(state.selector); return { state, attr: state.attr ? base?.getAttribute(state.attr) : null, text: base?.textContent ?? '', cssClass: base?.getAttribute('class') ?? '' }; }
      catch { return { state, attr: null, text: '', cssClass: '' }; }
    });
  }, [markup, symbol.states]);
  useEffect(() => {
    const el = root.current;
    if (!el) return;
    for (const { state, attr, text, cssClass } of originals) {
      let target: Element | null = null;
      try { target = el.querySelectorAll(state.selector)[0] ?? null; } catch { continue; }
      if (!target) continue;
      if (state.class) target.setAttribute('class', cssClass);
      else if (state.attr === 'text') target.textContent = text;
      else if (state.attr) { if (attr === null || attr === undefined) target.removeAttribute(state.attr); else target.setAttribute(state.attr, attr); (target as SVGElement).style.removeProperty(state.attr); }
      if (outputs && outputs[state.output] !== undefined) applyState(el, state, outputs[state.output]);
    }
  }, [originals, outputs]);
  return (
    <g transform={transform} style={{ '--led-color': emitterColor ?? '#ff453a' } as React.CSSProperties} className={selected ? 'es-symbol es-symbol--selected' : 'es-symbol'}>
      {broken ? (
        <g className="es-symbol__fallback">
          <rect x={0} y={0} width={symbol.width} height={symbol.height} rx={2} />
          <text x={symbol.width / 2} y={symbol.height / 2 + 3} textAnchor="middle">
            {fallbackLabel ?? '?'}
          </text>
        </g>
      ) : (
        <g ref={root} data-board-item={`symbol-${identity}`} dangerouslySetInnerHTML={{ __html: markup }} />
      )}
      {interactive && id && symbol.hotspots?.length ? <SymbolControls root={root} id={id} hotspots={symbol.hotspots} inputs={inputs} enabled={interactive} onInput={onInput} /> : null}
      {showPins &&
        Object.entries(symbol.pins).map(([name, pin]) => (
          <circle key={name} className="es-symbol__pin" cx={pin.x} cy={pin.y} r={1.8} />
        ))}
    </g>
  );
});

/** A standalone thumbnail of a symbol for lists and the library. */
export function SymbolThumbnail({ svg, symbol, size = 48, className }: { svg: string; symbol: SymbolDescription; size?: number; className?: string }) {
  const parsed = useMemo(() => parseSvg(svg), [svg]);
  const unique = useId();
  const markup = useMemo(() => scopeArtwork(parsed.inner, `thumbnail-${unique}`), [parsed.inner, unique]);
  const pad = 6;
  const vb = parsed.viewBox ?? { x: 0, y: 0, width: symbol.width, height: symbol.height };
  return (
    <svg
      className={className}
      width={size}
      height={size}
      viewBox={`${vb.x - pad} ${vb.y - pad} ${vb.width + 2 * pad} ${vb.height + 2 * pad}`}
      aria-hidden="true"
      focusable="false"
      preserveAspectRatio="xMidYMid meet"
    >
      <g data-board-item={`thumbnail-${unique}`} dangerouslySetInnerHTML={{ __html: markup }} />
    </svg>
  );
}
