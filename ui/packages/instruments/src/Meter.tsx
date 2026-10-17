// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { useEffect, useRef } from 'react';

export interface MeterProps {
  label: string;
  value: number | boolean | string | null | undefined;
  unit?: string;
  /** Significant digits for numbers. */
  digits?: number;
  /** Visual tone for boolean values. */
  tone?: 'default' | 'accent';
  /** Keeps and shows a min/max envelope of numeric values. */
  track?: boolean;
}

const PREFIXES: [number, string][] = [
  [1e9, 'G'],
  [1e6, 'M'],
  [1e3, 'k'],
  [1, ''],
  [1e-3, 'm'],
  [1e-6, 'µ'],
  [1e-9, 'n'],
  [1e-12, 'p'],
  [1e-15, 'f'],
];

/** the SI prefix that puts |v| in [1, 1000) (the extremes clamp) */
export function siPrefix(v: number): [number, string] {
  const a = Math.abs(v);
  for (const p of PREFIXES) if (a >= p[0]) return p;
  return PREFIXES[PREFIXES.length - 1]!;
}

/** below a femto-unit a reading is numerical residue (a leakage current
 * through an idle pull-up): it shows as about zero */
const FLOOR = 1e-15;

export function formatMeterValue(value: MeterProps['value'], unit = '', digits = 4): string {
  if (value === null || value === undefined) return '—';
  if (typeof value === 'boolean') return value ? 'ON' : 'OFF';
  if (typeof value === 'string') return value;
  if (!Number.isFinite(value)) return '—';
  if (value === 0) return unit ? `0 ${unit}` : '0';
  if (Math.abs(value) < FLOOR) return unit ? `≈ 0 ${unit}` : '≈ 0';
  let [scale, prefix] = siPrefix(value);
  let text = Number((value / scale).toPrecision(digits)).toString();
  // rounding can carry into the next prefix: 999.96 m -> 1000 m -> 1
  if (Math.abs(Number(text)) >= 1000 && scale < 1e9) {
    [scale, prefix] = siPrefix(value * 1.001);
    text = Number((value / scale).toPrecision(digits)).toString();
  }
  return unit ? `${text} ${prefix}${unit}` : prefix ? `${text} ${prefix}` : text;
}

/** axis tick labels: one prefix for the whole axis (from its largest
 * magnitude), and as many decimals as the step needs */
export function axisLabels(values: number[], step: number, unit: string): string[] {
  const big = values.reduce((m, v) => Math.max(m, Math.abs(v)), 0);
  const [scale, prefix] = big > 0 ? siPrefix(big) : [1, ''];
  // the fewest decimals that write the step exactly (0.2, 2.5, 5)
  const unitStep = step / scale;
  let decimals = 0;
  while (decimals < 6 && Math.abs(Math.round(unitStep * 10 ** decimals) - unitStep * 10 ** decimals) > 1e-6) decimals++;
  return values.map((v) => {
    const n = v / scale;
    const t = (Math.abs(n) < 0.5 * 10 ** -decimals ? 0 : n).toFixed(decimals);
    return `${t} ${prefix}${unit}`.trimEnd();
  });
}

/** A numeric readout tile with a monospaced value, unit and optional min/max envelope. */
export function Meter({ label, value, unit = '', digits = 4, tone = 'default', track }: MeterProps) {
  const envelope = useRef<{ min: number; max: number } | null>(null);
  useEffect(() => {
    if (!track || typeof value !== 'number' || !Number.isFinite(value)) return;
    const e = envelope.current;
    envelope.current = e ? { min: Math.min(e.min, value), max: Math.max(e.max, value) } : { min: value, max: value };
  }, [value, track]);
  const text = formatMeterValue(value, unit, digits);
  const on = typeof value === 'boolean' ? value : undefined;
  return (
    <div className={`es-meter${on !== undefined ? (on ? ' es-meter--on' : ' es-meter--off') : ''}${tone === 'accent' ? ' es-meter--accent' : ''}`} role="group" aria-label={`${label}: ${text}`}>
      <div className="es-meter__label">{label}</div>
      <div className="es-meter__value" aria-hidden="true">
        {text}
      </div>
      {track && envelope.current && (
        <div className="es-meter__envelope" aria-hidden="true">
          {formatMeterValue(envelope.current.min, unit, 3)} … {formatMeterValue(envelope.current.max, unit, 3)}
        </div>
      )}
    </div>
  );
}
