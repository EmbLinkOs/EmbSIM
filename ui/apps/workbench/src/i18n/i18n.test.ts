// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { en } from './en';
import { fr } from './fr';
import { translate, translatePlural } from './index';

const placeholders = (s: string) => [...s.matchAll(/\{(\w+)\}/g)].map((m) => m[1]).sort();

describe('catalogs', () => {
  it('French has every English key and no other', () => {
    expect(Object.keys(fr).sort()).toEqual(Object.keys(en).sort());
  });
  it('every translation keeps its message\'s parameters', () => {
    for (const k of Object.keys(en) as (keyof typeof en)[]) {
      expect(placeholders(fr[k]), k).toEqual(placeholders(en[k]));
    }
  });
  it('no translation is empty', () => {
    for (const [k, v] of Object.entries(fr)) expect(v.trim().length, k).toBeGreaterThan(0);
  });
  it('plural pairs come in twos', () => {
    const keys = Object.keys(en);
    for (const k of keys.filter((x) => x.endsWith('.one'))) expect(keys).toContain(k.replace(/\.one$/, '.other'));
  });
});

describe('translate', () => {
  it('fills parameters and falls back to English', () => {
    expect(translate('fr', 'transport.advanceBy', { amount: '10ms' })).toBe('Avancer de 10ms');
    expect(translate('en', 'transport.advanceBy', { amount: '10ms' })).toBe('Advance by 10ms');
  });
  it('counts by each language\'s rule', () => {
    expect(translatePlural('en', 'status.errors', 1)).toBe('1 error');
    expect(translatePlural('en', 'status.errors', 0)).toBe('0 errors');
    // French counts zero and one as singular
    expect(translatePlural('fr', 'status.errors', 0)).toBe('0 erreur');
    expect(translatePlural('fr', 'status.errors', 2)).toBe('2 erreurs');
  });
});
