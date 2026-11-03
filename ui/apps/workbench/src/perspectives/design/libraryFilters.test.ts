// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import type { CatalogItem, ComponentDescription } from '@embsim/protocol';
import { categoryName, categoryTree, filterLibrary, subcategoryName, type LibraryFilters } from './libraryFilters';
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
    // the legacy categories show under the taxonomy's; an unknown one keeps its name
    const tree = categoryTree(items, catalog);
    expect(tree.map((c) => c.id).sort()).toEqual(['new-category', 'simulator_primitives', 'transducers']);
    expect(tree.find((c) => c.id === 'new-category')?.name).toBe('New Category');
  });
  it('names categories and sub-categories in the interface language and filters by both', () => {
    const parts: CatalogItem[] = [
      { id: 'acme/1n4148', name: '1N4148', version: '1', category: 'diodes', subcategory: 'switching', fidelity_tiers: ['electrical'] },
      { id: 'acme/1n4007', name: '1N4007', version: '1', category: 'diodes', subcategory: 'rectifiers', fidelity_tiers: ['electrical'] },
      { id: 'acme/bc547', name: 'BC547', version: '1', category: 'transistors', subcategory: 'bipolar_npn', fidelity_tiers: ['electrical'] },
    ];
    expect(categoryName('diodes', 'fr')).toBe('Diodes');
    expect(categoryName('transistors', 'es')).toBe('Transistores');
    expect(subcategoryName('transistors', 'bipolar_npn', 'fr')).toBe('Bipolaires NPN');
    const tree = categoryTree(parts, {}, 'en');
    expect(tree[0]).toMatchObject({ id: 'diodes', count: 2, subcategories: [{ id: 'rectifiers', count: 1 }, { id: 'switching', count: 1 }] });
    expect(filterLibrary(parts, {}, { ...empty, category: 'diodes', subcategory: 'switching' }).map((i) => i.id)).toEqual(['acme/1n4148']);
    expect(filterLibrary(parts, {}, { ...empty, query: 'bipolar npn' }).map((i) => i.id)).toEqual(['acme/bc547']); // the sub-category's name is searched
  });
});
