// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { en } from './en';
import { fr } from './fr';
import { es } from './es';
import { translate, translatePlural } from './index';

const placeholders = (s: string) => [...s.matchAll(/\{(\w+)\}/g)].map((m) => m[1]).sort();

describe.each([
  ['French', fr],
  ['Spanish', es],
])('the %s catalog', (_name, catalog) => {
  it('has every English key and no other', () => {
    expect(Object.keys(catalog).sort()).toEqual(Object.keys(en).sort());
  });
  it('keeps each message\'s parameters', () => {
    for (const k of Object.keys(en) as (keyof typeof en)[]) {
      expect(placeholders(catalog[k]), k).toEqual(placeholders(en[k]));
    }
  });
  it('has no empty translation', () => {
    for (const [k, v] of Object.entries(catalog)) expect(v.trim().length, k).toBeGreaterThan(0);
  });
});

describe('catalogs', () => {
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
    // Spanish counts zero as plural
    expect(translatePlural('es', 'status.errors', 1)).toBe('1 error');
    expect(translatePlural('es', 'status.errors', 0)).toBe('0 errores');
    expect(translate('es', 'transport.advanceBy', { amount: '10ms' })).toBe('Avanzar 10ms');
  });
});
