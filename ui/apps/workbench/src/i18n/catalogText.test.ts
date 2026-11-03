// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { catalogDescription, catalogFidelity } from './catalogText';

const entry = {
  description: 'A 1 kOhm resistor.',
  fidelity: {
    electrical: { claims: 'Ohm\'s law.', omissions: ['self-heating', 'noise'], range: { voltage_v: [-50, 50] as [number, number] } },
    functional: { claims: 'A wire.' },
  },
  i18n: { fr: { description: 'Une résistance de 1 kOhm.', fidelity: { electrical: { claims: 'La loi d’Ohm.', omissions: ['l’échauffement', 'le bruit'] } } } },
};

describe('catalog text', () => {
  it('takes the language’s description, English otherwise', () => {
    expect(catalogDescription(entry, 'fr')).toBe('Une résistance de 1 kOhm.');
    expect(catalogDescription(entry, 'en')).toBe('A 1 kOhm resistor.');
    expect(catalogDescription({ description: 'Only English.', fidelity: {} }, 'fr')).toBe('Only English.');
  });
  it('translates claims and omissions, keeping the rest of the declaration', () => {
    const f = catalogFidelity(entry, 'electrical', 'fr');
    expect(f?.claims).toBe('La loi d’Ohm.');
    expect(f?.omissions).toEqual(['l’échauffement', 'le bruit']);
    expect(f?.range).toEqual({ voltage_v: [-50, 50] });
    expect(catalogFidelity(entry, 'functional', 'fr')?.claims).toBe('A wire.');
    expect(catalogFidelity(entry, 'timed', 'fr')).toBeUndefined();
  });
  it('keeps the English omissions when the translation’s do not line up', () => {
    const stale = { ...entry, i18n: { fr: { fidelity: { electrical: { claims: 'La loi d’Ohm.', omissions: ['l’échauffement'] } } } } };
    expect(catalogFidelity(stale, 'electrical', 'fr')?.omissions).toEqual(['self-heating', 'noise']);
  });
});
