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

export function formatMeterValue(value: MeterProps['value'], unit = '', digits = 4): string {
  if (value === null || value === undefined) return '—';
  if (typeof value === 'boolean') return value ? 'ON' : 'OFF';
  if (typeof value === 'string') return value;
  if (!Number.isFinite(value)) return '—';
  const a = Math.abs(value);
  let scaled = value;
  let prefix = '';
  if (a !== 0 && a < 1e-6) {
    scaled = value * 1e9;
    prefix = 'n';
  } else if (a !== 0 && a < 1e-3) {
    scaled = value * 1e6;
    prefix = 'µ';
  } else if (a !== 0 && a < 1) {
    scaled = value * 1e3;
    prefix = 'm';
  } else if (a >= 1e6) {
    scaled = value / 1e6;
    prefix = 'M';
  } else if (a >= 1e3) {
    scaled = value / 1e3;
    prefix = 'k';
  }
  const text = Number(scaled.toPrecision(digits)).toString();
  return unit ? `${text} ${prefix}${unit}` : prefix ? `${text} ${prefix}` : text;
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
