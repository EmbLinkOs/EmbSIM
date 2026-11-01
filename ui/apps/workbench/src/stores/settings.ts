// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The workbench's preferences (the Settings page): kept in localStorage under
// `embsim.settings`, applied to <html> at once (theme, text size, density,
// language). Only preferences that act on something are here.
import { applyTheme, type ThemePreference } from '@embsim/design';
import type { TimeUnit } from '@embsim/protocol';
import { create } from 'zustand';

export type Language = 'en' | 'fr';
export const LANGUAGES: Language[] = ['en', 'fr'];
export type TextSize = 'small' | 'medium' | 'large';
export type Density = 'comfortable' | 'compact';
/** how the clock, the status bar and the Debug perspective show virtual time */
export type TimeUnitPreference = 'auto' | TimeUnit;
/** how the rate of virtual time against host time is shown */
export type RateFormat = 'factor' | 'percent';

export interface Settings {
  language: Language;
  theme: ThemePreference;
  textSize: TextSize;
  density: Density;
  /** reopen each perspective with the panel arrangement it had last time */
  restoreLayout: boolean;
  timeUnit: TimeUnitPreference;
  /** decimals shown after the unit (0 to 6) */
  timeDecimals: number;
  rateFormat: RateFormat;
}

const KEY = 'embsim.settings';
const LAYOUT_PREFIXES = ['embsim.layout.', 'embsim.floating', 'embsim.perspective', 'embsim.designView'];

function browserLanguage(): Language {
  try {
    const l = (navigator.languages?.[0] ?? navigator.language ?? 'en').toLowerCase();
    return l.startsWith('fr') ? 'fr' : 'en';
  } catch {
    return 'en';
  }
}

export function defaultSettings(): Settings {
  let theme: ThemePreference = 'system';
  try {
    const t = localStorage.getItem('embsim.theme');
    if (t === 'dark' || t === 'light') theme = t;
  } catch {
    /* none */
  }
  return { language: browserLanguage(), theme, textSize: 'medium', density: 'comfortable', restoreLayout: true, timeUnit: 'auto', timeDecimals: 3, rateFormat: 'factor' };
}

/** Keeps only known values: a stored object from an older or newer build is read safely. */
export function normalizeSettings(raw: unknown, base: Settings = defaultSettings()): Settings {
  const r = (raw && typeof raw === 'object' ? raw : {}) as Record<string, unknown>;
  const pick = <T,>(v: unknown, allowed: readonly T[], d: T): T => (allowed.includes(v as T) ? (v as T) : d);
  const decimals = typeof r['timeDecimals'] === 'number' && Number.isInteger(r['timeDecimals']) ? Math.min(6, Math.max(0, r['timeDecimals'])) : base.timeDecimals;
  return {
    language: pick(r['language'], LANGUAGES, base.language),
    theme: pick(r['theme'], ['system', 'light', 'dark'] as const, base.theme),
    textSize: pick(r['textSize'], ['small', 'medium', 'large'] as const, base.textSize),
    density: pick(r['density'], ['comfortable', 'compact'] as const, base.density),
    restoreLayout: typeof r['restoreLayout'] === 'boolean' ? r['restoreLayout'] : base.restoreLayout,
    timeUnit: pick(r['timeUnit'], ['auto', 's', 'ms', 'us', 'ns', 'ps'] as const, base.timeUnit),
    timeDecimals: decimals,
    rateFormat: pick(r['rateFormat'], ['factor', 'percent'] as const, base.rateFormat),
  };
}

function read(): Settings {
  try {
    return normalizeSettings(JSON.parse(localStorage.getItem(KEY) ?? 'null'));
  } catch {
    return defaultSettings();
  }
}

/** Puts the preferences that live on <html> in place. */
export function applySettings(s: Settings): void {
  const root = document.documentElement;
  root.lang = s.language;
  root.dataset['textSize'] = s.textSize;
  root.dataset['density'] = s.density;
  applyTheme(s.theme);
}

/** Removes every saved panel arrangement (dock sizes, order, floating windows). */
export function clearSavedLayouts(): number {
  let n = 0;
  try {
    for (let i = localStorage.length - 1; i >= 0; i--) {
      const k = localStorage.key(i);
      if (k && LAYOUT_PREFIXES.some((p) => k.startsWith(p))) {
        localStorage.removeItem(k);
        n++;
      }
    }
  } catch {
    /* storage unavailable: nothing was saved either */
  }
  return n;
}

export interface SettingsState extends Settings {
  /** bumped by a layout reset: perspectives remount with their default arrangement */
  layoutEpoch: number;
  settingsOpen: boolean;
  set<K extends keyof Settings>(key: K, value: Settings[K]): void;
  resetAll(): void;
  resetLayout(): void;
  setSettingsOpen(open: boolean): void;
}

function persist(s: Settings): void {
  try {
    localStorage.setItem(KEY, JSON.stringify(s));
  } catch {
    /* in-memory preferences still apply */
  }
}

function settingsOf(s: SettingsState): Settings {
  const { language, theme, textSize, density, restoreLayout, timeUnit, timeDecimals, rateFormat } = s;
  return { language, theme, textSize, density, restoreLayout, timeUnit, timeDecimals, rateFormat };
}

const initial = read();
// A workbench told not to restore its arrangement starts from the defaults.
if (!initial.restoreLayout) clearSavedLayouts();

export const useSettings = create<SettingsState>((set, get) => ({
  ...initial,
  layoutEpoch: 0,
  settingsOpen: false,
  set: (key, value) => {
    set({ [key]: value } as Partial<SettingsState>);
    const s = settingsOf(get());
    persist(s);
    applySettings(s);
  },
  resetAll: () => {
    const d = { ...defaultSettings(), theme: 'system' as const };
    set(d);
    persist(d);
    applySettings(d);
  },
  resetLayout: () => {
    clearSavedLayouts();
    set({ layoutEpoch: get().layoutEpoch + 1 });
  },
  setSettingsOpen: (settingsOpen) => set({ settingsOpen }),
}));

if (typeof document !== 'undefined') applySettings(initial);
