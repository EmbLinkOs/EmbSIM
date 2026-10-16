// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { Layout, SymbolDescription } from '@embsim/protocol';
import { memo, useMemo } from 'react';
import { symbolTransform } from './geometry';
import { parseSvg } from './svg';

export interface ComponentSymbolProps {
  /** The catalog symbol.svg source. */
  svg: string;
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
export const ComponentSymbol = memo(function ComponentSymbol({ svg, symbol, layout, showPins, selected, fallbackLabel }: ComponentSymbolProps) {
  const parsed = useMemo(() => parseSvg(svg), [svg]);
  const transform = symbolTransform(layout, symbol);
  const broken = !parsed.inner;
  return (
    <g transform={transform} className={selected ? 'es-symbol es-symbol--selected' : 'es-symbol'}>
      {broken ? (
        <g className="es-symbol__fallback">
          <rect x={0} y={0} width={symbol.width} height={symbol.height} rx={2} />
          <text x={symbol.width / 2} y={symbol.height / 2 + 3} textAnchor="middle">
            {fallbackLabel ?? '?'}
          </text>
        </g>
      ) : (
        <g dangerouslySetInnerHTML={{ __html: parsed.inner }} />
      )}
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
      <g dangerouslySetInnerHTML={{ __html: parsed.inner }} />
    </svg>
  );
}
