// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// What state the simulation is in, from what the engine last said: the one
// place the header, the status bar and the panels read it from.
import { formatTime, pickUnit, type Time } from '@embsim/protocol';
import { useEffect, useRef, useState } from 'react';
import type { MessageKey } from '../i18n';
import type { ConnectionStatus } from '../stores/session';
import { useSettings, type RateFormat, type TimeUnitPreference } from '../stores/settings';
import { useSession } from '../stores/session';

export type SimState = 'running' | 'paused' | 'stopped' | 'loading' | 'failed' | 'disconnected';

/** the reasons a run ends without anything having gone wrong */
const NORMAL_STOPS = new Set(['until', 'stopped', 'paused', 'pause', 'reset', 'breakpoint', 'user']);

export function simStateOf(status: ConnectionStatus, running: boolean, now: Time, reason: string | null): SimState {
  if (status === 'error') return 'failed';
  if (status === 'closed') return 'disconnected';
  if (status !== 'open') return 'loading';
  if (running) return 'running';
  if (reason && !NORMAL_STOPS.has(reason)) return 'failed';
  return now === 0n ? 'stopped' : 'paused';
}

export function useSimState(): SimState {
  const status = useSession((s) => s.status);
  const running = useSession((s) => s.running);
  const now = useSession((s) => s.now);
  const reason = useSession((s) => s.lastControlReason);
  return simStateOf(status, running, now, reason);
}

export const SIM_STATE_LABEL: Record<SimState, MessageKey> = {
  running: 'sim.state.running',
  paused: 'sim.state.paused',
  stopped: 'sim.state.stopped',
  loading: 'sim.state.loading',
  failed: 'sim.state.failed',
  disconnected: 'sim.state.disconnected',
};

export const SIM_STATE_HINT: Record<SimState, MessageKey> = {
  running: 'sim.state.running.hint',
  paused: 'sim.state.paused.hint',
  stopped: 'sim.state.stopped.hint',
  loading: 'sim.state.loading.hint',
  failed: 'sim.state.failed.hint',
  disconnected: 'sim.state.disconnected.hint',
};

/** Simulated time as the Settings page asks: a fixed unit or the largest that fits. */
export function formatSimTime(t: Time, unit: TimeUnitPreference, decimals: number): string {
  if (unit === 'auto') return formatTime(t, { decimals, unit: pickUnit(t) });
  return formatTime(t, { decimals, unit });
}

export function useSimTimeFormat(): (t: Time) => string {
  const unit = useSettings((s) => s.timeUnit);
  const decimals = useSettings((s) => s.timeDecimals);
  return (t: Time) => formatSimTime(t, unit, decimals);
}

/** The rate of simulated against host time: "0.94×" or "94 %". */
export function formatRate(rate: number, format: RateFormat, lang: string): string {
  const loc = lang === 'fr' ? 'fr-FR' : 'en-US';
  if (format === 'percent') {
    const pct = new Intl.NumberFormat(loc, { maximumFractionDigits: rate * 100 >= 100 ? 0 : 1 }).format(rate * 100);
    return lang === 'fr' ? `${pct} %` : `${pct}%`;
  }
  const digits = rate >= 100 ? 0 : rate >= 10 ? 1 : 2;
  return `${new Intl.NumberFormat(loc, { minimumFractionDigits: digits, maximumFractionDigits: digits }).format(rate)}×`;
}

/** Virtual time against wall time over the last two seconds while running:
 * 1 is real time (the rate a bench's own clock shows). Measured here from
 * the engine's own clock reports, never estimated. */
export function useRealTimeRate(now: bigint, running: boolean): number | null {
  const samples = useRef<{ wall: number; ps: bigint }[]>([]);
  const [rate, setRate] = useState<number | null>(null);
  useEffect(() => {
    if (!running) {
      samples.current = [];
      setRate(null);
      return;
    }
    const wall = performance.now();
    const list = samples.current;
    list.push({ wall, ps: now });
    while (list.length > 2 && wall - list[0]!.wall > 2000) list.shift();
    const first = list[0]!;
    const dt = wall - first.wall;
    if (dt >= 250) setRate(Number(now - first.ps) / 1e9 / dt);
  }, [now, running]);
  return rate;
}
