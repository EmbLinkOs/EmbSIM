// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { CatalogItem } from '@embsim/protocol';
import type { CatalogEntry } from '../../stores/project';
import { tOptional } from '../../i18n';

const GROUPS: Record<string, string[]> = {
  'Circuit essentials': ['passives', 'sources', 'connectors', 'inputs', 'switches', 'mechanical'],
  'Analog & power': ['diodes', 'transistors', 'analog', 'power', 'amplifiers', 'comparators', 'timers', 'optoelectronics'],
  'Digital & embedded': ['logic', 'memory', 'interfaces', 'mcu', 'boards'],
  'Sense & interact': ['sensors', 'displays', 'actuators', 'indicators', 'instruments'],
};
export function categoryName(category: string): string {
  return tOptional(`category.${category}`) ?? category.replace(/[_-]/g, ' ').replace(/\b\w/g, (s) => s.toUpperCase());
}
export function categoryGroups(categories: string[]) {
  const used = new Set(Object.values(GROUPS).flat());
  return [...Object.entries(GROUPS).map(([name, members]) => ({ name, categories: members.filter((c) => categories.includes(c)) })), { name: 'Other', categories: categories.filter((c) => !used.has(c)) }].filter((g) => g.categories.length);
}
export interface LibraryFilters { query: string; category: string; manufacturer: string; pack: string; tier: string; named: boolean }
export function filterLibrary(items: CatalogItem[], catalog: Record<string, CatalogEntry>, filters: LibraryFilters): CatalogItem[] {
  const words = filters.query.toLocaleLowerCase().trim().split(/\s+/).filter(Boolean);
  return items.filter((item) => {
    const d = catalog[item.id]?.description;
    if (filters.category && item.category !== filters.category) return false;
    if (filters.manufacturer && d?.part?.manufacturer !== filters.manufacturer) return false;
    if (filters.pack && d?.part?.package !== filters.pack) return false;
    if (filters.tier && !item.fidelity_tiers.some((t) => t === filters.tier)) return false;
    if (filters.named && !d?.part) return false;
    const haystack = [item.id, item.name, item.category, d?.description, d?.part?.manufacturer, d?.part?.mpn, d?.part?.package, ...(d?.keywords ?? [])].join(' ').toLocaleLowerCase();
    return words.every((word) => haystack.includes(word));
  }).sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true }));
}
