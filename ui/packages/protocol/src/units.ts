// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// SI-suffixed numbers as accepted by descriptions and projects (SPEC-005 §4):
// "4.7k", "100n", "2.2u", "1meg", "1G". Parsing is for display and range
// checks only; the loader normalises values authoritatively.

const SUFFIX: Record<string, number> = {
  f: 1e-15,
  p: 1e-12,
  n: 1e-9,
  u: 1e-6,
  µ: 1e-6,
  μ: 1e-6,
  m: 1e-3,
  k: 1e3,
  K: 1e3,
  meg: 1e6,
  M: 1e6,
  G: 1e9,
  T: 1e12,
};

const SI = /^\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?)\s*(meg|[fpnuµμmkKMGT])?\s*$/;

/** Parses "4.7k" → 4700; returns undefined when the text is not a number. */
export function parseSi(value: number | string | undefined | null): number | undefined {
  if (value === undefined || value === null) return undefined;
  if (typeof value === 'number') return Number.isFinite(value) ? value : undefined;
  const m = SI.exec(value);
  if (!m) return undefined;
  const n = Number(m[1]);
  if (!Number.isFinite(n)) return undefined;
  return m[2] ? n * (SUFFIX[m[2]] ?? 1) : n;
}

/** Formats a number with an SI prefix and optional unit: 4700 → "4.7k", with unit "Ω" → "4.7 kΩ". */
export function formatSi(value: number, unit = '', digits = 4): string {
  if (!Number.isFinite(value)) return '—';
  const a = Math.abs(value);
  const table: [number, string][] = [
    [1e12, 'T'],
    [1e9, 'G'],
    [1e6, 'M'],
    [1e3, 'k'],
    [1, ''],
    [1e-3, 'm'],
    [1e-6, 'µ'],
    [1e-9, 'n'],
    [1e-12, 'p'],
  ];
  let scale = 1;
  let prefix = '';
  if (a !== 0) {
    for (const [s, p] of table) {
      if (a >= s) {
        scale = s;
        prefix = p;
        break;
      }
    }
  }
  const text = Number((value / scale).toPrecision(digits)).toString();
  return unit ? `${text} ${prefix}${unit}` : `${text}${prefix}`;
}
