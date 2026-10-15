// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Virtual time. The protocol carries every time as a decimal string of
// picoseconds (SPEC-008 §4); the client keeps it as a bigint so values past
// 2^53 ps (about 2.5 hours) never lose precision (UI-007).

/** A virtual time or duration in picoseconds. */
export type Time = bigint;

export const PS: Time = 1n;
export const NS: Time = 1_000n;
export const US: Time = 1_000_000n;
export const MS: Time = 1_000_000_000n;
export const S: Time = 1_000_000_000_000n;

export type TimeUnit = 'ps' | 'ns' | 'us' | 'ms' | 's';

const UNIT_SCALE: Record<TimeUnit, Time> = { ps: PS, ns: NS, us: US, ms: MS, s: S };
const UNIT_ORDER: TimeUnit[] = ['s', 'ms', 'us', 'ns', 'ps'];
const UNIT_LABEL: Record<TimeUnit, string> = { ps: 'ps', ns: 'ns', us: 'µs', ms: 'ms', s: 's' };

const PS_STRING = /^-?\d+$/;
const DURATION = /^\s*(-?)(\d+)(?:\.(\d+))?\s*(ps|ns|us|µs|μs|ms|s)?\s*$/;

/** Parses a protocol time: a decimal string of picoseconds. Throws on anything else. */
export function parsePs(value: string | number | bigint): Time {
  if (typeof value === 'bigint') return value;
  if (typeof value === 'number') {
    if (!Number.isSafeInteger(value)) throw new RangeError(`time ${value} is not a safe integer`);
    return BigInt(value);
  }
  if (!PS_STRING.test(value)) throw new RangeError(`"${value}" is not a picosecond string`);
  return BigInt(value);
}

/**
 * Parses a duration with an optional unit as in SPEC-007 §2 ("10ms", "1.5us",
 * "250"). A bare number is picoseconds. Sub-picosecond fractions are refused.
 */
export function parseDuration(text: string): Time {
  const m = DURATION.exec(text);
  if (!m) throw new RangeError(`"${text}" is not a duration`);
  const sign = m[1] === '-' ? -1n : 1n;
  const whole = m[2] ?? '0';
  const frac = m[3] ?? '';
  const unitText = m[4] ?? 'ps';
  const unit: TimeUnit = unitText === 'µs' || unitText === 'μs' ? 'us' : (unitText as TimeUnit);
  const scale = UNIT_SCALE[unit];
  const digits = scale.toString().length - 1; // decimal places available at this unit
  if (frac.length > digits && /[1-9]/.test(frac.slice(digits))) {
    throw new RangeError(`"${text}" has a sub-picosecond fraction`);
  }
  const fracPadded = (frac + '0'.repeat(digits)).slice(0, digits);
  const value = BigInt(whole) * scale + (digits > 0 ? BigInt(fracPadded || '0') : 0n);
  return sign * value;
}

/** Serialises a time for the wire. */
export function toPs(t: Time): string {
  return t.toString();
}

export interface FormatOptions {
  /** Significant decimals shown after the unit split; default 3. */
  decimals?: number;
  /** Force a unit instead of choosing the largest one that fits. */
  unit?: TimeUnit;
  /** Keep trailing zeros (default true: "12.000 ms" keeps the column stable). */
  padZeros?: boolean;
}

/** Chooses the largest unit whose magnitude is at least one in `t`. */
export function pickUnit(t: Time): TimeUnit {
  const abs = t < 0n ? -t : t;
  for (const u of UNIT_ORDER) {
    if (abs >= UNIT_SCALE[u]) return u;
  }
  return 'ps';
}

/** Splits a time into an integer part and a decimal string at the given unit. */
export function splitTime(t: Time, unit: TimeUnit, decimals = 3): { sign: string; whole: string; frac: string } {
  const scale = UNIT_SCALE[unit];
  const neg = t < 0n;
  const abs = neg ? -t : t;
  const whole = abs / scale;
  const rem = abs % scale;
  const scaleDigits = scale.toString().length - 1;
  let frac = scaleDigits === 0 ? '' : rem.toString().padStart(scaleDigits, '0');
  if (decimals < scaleDigits) {
    // Truncate (never round) so the display never claims a future instant.
    frac = frac.slice(0, decimals);
  } else {
    frac = frac + '0'.repeat(decimals - scaleDigits);
  }
  return { sign: neg ? '-' : '', whole: whole.toString(), frac };
}

/** Formats a time with units, e.g. 12345000000n → "12.345 ms". */
export function formatTime(t: Time, options: FormatOptions = {}): string {
  const unit = options.unit ?? pickUnit(t);
  const decimals = options.decimals ?? 3;
  const { sign, whole, frac } = splitTime(t, unit, decimals);
  let f = frac;
  if (options.padZeros === false) f = f.replace(/0+$/, '');
  const body = f.length > 0 ? `${whole}.${f}` : whole;
  return `${sign}${body} ${UNIT_LABEL[unit]}`;
}

/** Formats a raw picosecond count with thousands separators, for tooltips: "12 345 000 000 ps". */
export function formatPsRaw(t: Time): string {
  const neg = t < 0n;
  const digits = (neg ? -t : t).toString();
  const groups: string[] = [];
  for (let i = digits.length; i > 0; i -= 3) groups.unshift(digits.slice(Math.max(0, i - 3), i));
  return `${neg ? '-' : ''}${groups.join(' ')} ps`;
}

/** Formats a frequency in Hz with an SI prefix. */
export function formatFrequency(hz: number, decimals = 3): string {
  if (!Number.isFinite(hz)) return '—';
  const abs = Math.abs(hz);
  if (abs >= 1e9) return `${(hz / 1e9).toFixed(decimals)} GHz`;
  if (abs >= 1e6) return `${(hz / 1e6).toFixed(decimals)} MHz`;
  if (abs >= 1e3) return `${(hz / 1e3).toFixed(decimals)} kHz`;
  return `${hz.toFixed(decimals)} Hz`;
}

/** Converts a time to a float number of seconds for drawing only (never for protocol values). */
export function toSeconds(t: Time): number {
  return Number(t) / 1e12;
}

/** Frequency of a period expressed as a bigint duration, as a float. */
export function frequencyOf(period: Time): number {
  if (period <= 0n) return NaN;
  return 1e12 / Number(period);
}

export function clampTime(t: Time, lo: Time, hi: Time): Time {
  return t < lo ? lo : t > hi ? hi : t;
}

export function minTime(a: Time, b: Time): Time {
  return a < b ? a : b;
}

export function maxTime(a: Time, b: Time): Time {
  return a > b ? a : b;
}

/** Scales a time by a float factor (for zoom) rounding to integer ps. */
export function scaleTime(t: Time, factor: number): Time {
  if (!Number.isFinite(factor)) return t;
  // Keep precision for huge values by scaling the integer part with a 2^20 fixed point.
  const fixed = BigInt(Math.round(factor * 1_048_576));
  return (t * fixed) / 1_048_576n;
}
