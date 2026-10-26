// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { OutputValue, Time } from '@embsim/protocol';

export interface EmissionFrame { at: Time; totals: Record<string, number>; means: Record<string, OutputValue>; duration: Time }
export const emptyEmission = (): EmissionFrame => ({ at: 0n, totals: {}, means: {}, duration: 0n });

/** Measure native accepted-time integrals between received observations. This
 * is a display exposure measurement, never a replacement for electrical state. */
export function measureEmission(previous: EmissionFrame, now: Time, outputs: Record<string, OutputValue>): EmissionFrame {
  if (now < previous.at) previous = emptyEmission();
  const received = Object.fromEntries(Object.entries(outputs).filter(([key,value]) => key.endsWith('emission_integral') && typeof value === 'number' && Number.isFinite(value))) as Record<string, number>;
  // Net-only or partial notifications are not a fresh integral for every die.
  // Keep the previous complete exposure rather than treating cached totals as zero light.
  if (!Object.keys(received).length || Object.keys(previous.totals).some((key) => received[key] === undefined)) return previous;
  if (Object.entries(received).some(([key,value]) => previous.totals[key] !== undefined && value < previous.totals[key]!)) previous = { ...emptyEmission(), at: now };
  const totals: Record<string, number> = {};
  const means: Record<string, OutputValue> = now === previous.at ? { ...previous.means } : {};
  const dt = Number(now - previous.at) * 1e-12;
  for (const [key, value] of Object.entries(outputs)) {
    if (!key.endsWith('emission_integral') || typeof value !== 'number' || !Number.isFinite(value)) continue;
    totals[key] = value;
    const old = previous.totals[key];
    if (dt > 0 && old !== undefined && value >= old) means[key.replace('emission_integral', 'luminance')] = Math.max(0, Math.min(1, (value - old) / dt));
  }
  const ids = new Set(Object.keys(means).filter((k) => k.endsWith('.red_luminance')).map((k) => k.slice(0, -'.red_luminance'.length)));
  for (const id of ids) {
    const levels = ['red', 'green', 'blue'].map((c) => Number(means[`${id}.${c}_luminance`] ?? 0));
    const peak = Math.max(...levels);
    means[`${id}.luminance`] = peak;
    means[`${id}.color`] = '#' + levels.map((x) => Math.round(peak > 0 ? 255 * x / peak : 0).toString(16).padStart(2, '0')).join('');
  }
  return { at: now, totals, means, duration: now > previous.at ? now - previous.at : previous.duration };
}
