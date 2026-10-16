// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import type { CatalogItem, ComponentDescription } from '@embsim/protocol';
import { categoryGroups, filterLibrary, type LibraryFilters } from './libraryFilters';
const items: CatalogItem[] = [
  { id: 'acme/sensor', name: 'NTC 10k', version: '1', category: 'sensors', fidelity_tiers: ['electrical'] },
  { id: 'acme/gate', name: 'Gate', version: '1', category: 'logic', fidelity_tiers: ['functional', 'timed'] },
  { id: 'acme/custom', name: 'Custom', version: '1', category: 'new-category', fidelity_tiers: [] },
];
const catalog = { 'acme/sensor': { description: { description: 'Temperature sensor', keywords: ['thermistor'], part: { manufacturer: 'Vishay', mpn: 'NTCLE100', package: 'Radial' } } as ComponentDescription, files: {} } };
const empty: LibraryFilters = { query: '', category: '', manufacturer: '', pack: '', tier: '', named: false };
describe('library discovery', () => {
  it('combines words across manufacturer, function and package', () => {
    expect(filterLibrary(items, catalog, { ...empty, query: 'radial Vishay thermistor' }).map((i) => i.id)).toEqual(['acme/sensor']);
    expect(filterLibrary(items, catalog, { ...empty, query: 'Vishay gate' })).toEqual([]);
  });
  it('intersects named identity, package and fidelity filters', () => {
    expect(filterLibrary(items, catalog, { ...empty, named: true, manufacturer: 'Vishay', pack: 'Radial', tier: 'electrical' })).toHaveLength(1);
    expect(filterLibrary(items, catalog, { ...empty, named: true, tier: 'timed' })).toEqual([]);
    expect(filterLibrary(items, catalog, { ...empty, tier: 'timed' }).map((i) => i.id)).toEqual(['acme/gate']);
  });
  it('keeps custom libraries discoverable before descriptions load', () => {
    expect(filterLibrary(items, catalog, { ...empty, query: 'custom' })).toHaveLength(1);
    const groups = categoryGroups(items.map((i) => i.category));
    expect(groups.find((g) => g.name === 'Other')?.categories).toEqual(['new-category']);
    expect(groups.flatMap((g) => g.categories).sort()).toEqual(['logic', 'new-category', 'sensors']);
  });
});
