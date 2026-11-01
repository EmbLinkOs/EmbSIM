// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The workbench's translations. English (en.ts) is the source of every
// message and its key; each other language is a complete catalog of the same
// keys (the type system and i18n.test.ts hold them to it), and a key a
// catalog lacks at run time falls back to English. Messages take {name}
// parameters; a count picks `key.one` or `key.other` by the language's own
// plural rule. Technical identifiers (register and peripheral names, units,
// protocol and part names, code) are never translated: they are passed in as
// parameters or left out of the catalogs.
import { useCallback } from 'react';
import { useSettings, type Language } from '../stores/settings';
import { en, type MessageKey } from './en';
import { fr } from './fr';

export type { MessageKey } from './en';

const CATALOGS: Record<Language, Partial<Record<MessageKey, string>>> = { en, fr };

export const LANGUAGE_NAMES: Record<Language, string> = { en: 'English', fr: 'Français' };

export type Params = Record<string, string | number>;

function interpolate(text: string, params?: Params): string {
  if (!params) return text;
  return text.replace(/\{(\w+)\}/g, (m, name: string) => (name in params ? String(params[name]) : m));
}

/** The message `key` in `lang`, English when the catalog lacks it. */
export function translate(lang: Language, key: MessageKey, params?: Params): string {
  const text = CATALOGS[lang][key] ?? en[key] ?? key;
  return interpolate(text, params);
}

/** `key.one` or `key.other` for `count`, as the language counts. */
export function translatePlural(lang: Language, key: string, count: number, params?: Params): string {
  const rule = new Intl.PluralRules(lang).select(count);
  const k = (rule === 'one' ? `${key}.one` : `${key}.other`) as MessageKey;
  return translate(lang, k, { count: formatNumber(lang, count), ...params });
}

export function formatNumber(lang: Language, n: number, options?: Intl.NumberFormatOptions): string {
  return new Intl.NumberFormat(lang === 'fr' ? 'fr-FR' : 'en-US', options).format(n);
}

/** Outside React (actions, toasts raised from stores): the current language. */
export function t(key: MessageKey, params?: Params): string {
  return translate(useSettings.getState().language, key, params);
}

export function tp(key: string, count: number, params?: Params): string {
  return translatePlural(useSettings.getState().language, key, count, params);
}

export interface Translator {
  (key: MessageKey, params?: Params): string;
  plural(key: string, count: number, params?: Params): string;
  number(n: number, options?: Intl.NumberFormatOptions): string;
  lang: Language;
}

/** In a component: re-renders when the language changes. */
export function useT(): Translator {
  const lang = useSettings((s) => s.language);
  const fn = useCallback((key: MessageKey, params?: Params) => translate(lang, key, params), [lang]) as Translator;
  fn.plural = (key, count, params) => translatePlural(lang, key, count, params);
  fn.number = (n, options) => formatNumber(lang, n, options);
  fn.lang = lang;
  return fn;
}
