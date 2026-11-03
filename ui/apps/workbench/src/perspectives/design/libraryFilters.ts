// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { CatalogItem } from '@embsim/protocol';
import type { CatalogEntry } from '../../stores/project';
import { TAXONOMY } from '@embsim/protocol';
import type { Language } from '../../stores/settings';

// The library's categories are the catalog's taxonomy (catalog/taxonomy.json,
// after Proteus's device categories): a category, its sub-categories, each
// named in the interface's language. A category from before the taxonomy
// (a library made earlier) shows under the one that replaced it; one the
// taxonomy does not know keeps its own name.

const pretty = (id: string) => id.replace(/[_-]/g, ' ').replace(/\b\w/g, (s) => s.toUpperCase());

/** the item's category in the taxonomy */
export function categoryOf(item: Pick<CatalogItem, 'category'>): string {
  return TAXONOMY.legacy[item.category] ?? item.category;
}

/** the item's sub-category, from its listing or its description */
export function subcategoryOf(item: CatalogItem, catalog: Record<string, CatalogEntry>): string | undefined {
  return item.subcategory ?? catalog[item.id]?.description.subcategory;
}

export function categoryName(category: string, lang: Language = 'en'): string {
  const c = TAXONOMY.categories.find((x) => x.id === (TAXONOMY.legacy[category] ?? category));
  return c ? c.name[lang] : pretty(category);
}

export function subcategoryName(category: string, sub: string, lang: Language = 'en'): string {
  const c = TAXONOMY.categories.find((x) => x.id === (TAXONOMY.legacy[category] ?? category));
  return c?.subcategories.find((s) => s.id === sub)?.name[lang] ?? pretty(sub);
}

export interface CategoryNode {
  id: string;
  name: string;
  count: number;
  subcategories: { id: string; name: string; count: number }[];
}

/** the categories the items fill, alphabetical in `lang`, each with its sub-categories and counts */
export function categoryTree(items: CatalogItem[], catalog: Record<string, CatalogEntry>, lang: Language = 'en'): CategoryNode[] {
  const nodes = new Map<string, CategoryNode>();
  for (const item of items) {
    const cat = categoryOf(item);
    let node = nodes.get(cat);
    if (!node) nodes.set(cat, (node = { id: cat, name: categoryName(cat, lang), count: 0, subcategories: [] }));
    node.count++;
    const sub = subcategoryOf(item, catalog);
    if (!sub) continue;
    const s = node.subcategories.find((x) => x.id === sub);
    if (s) s.count++;
    else node.subcategories.push({ id: sub, name: subcategoryName(cat, sub, lang), count: 1 });
  }
  const order = (a: { name: string }, b: { name: string }) => a.name.localeCompare(b.name, lang);
  return [...nodes.values()].map((n) => ({ ...n, subcategories: n.subcategories.sort(order) })).sort(order);
}

export interface LibraryFilters { query: string; category: string; subcategory?: string; manufacturer: string; pack: string; tier: string; named: boolean }
export function filterLibrary(items: CatalogItem[], catalog: Record<string, CatalogEntry>, filters: LibraryFilters): CatalogItem[] {
  const words = filters.query.toLocaleLowerCase().trim().split(/\s+/).filter(Boolean);
  return items.filter((item) => {
    const d = catalog[item.id]?.description;
    if (filters.category && categoryOf(item) !== filters.category) return false;
    if (filters.subcategory && subcategoryOf(item, catalog) !== filters.subcategory) return false;
    if (filters.manufacturer && d?.part?.manufacturer !== filters.manufacturer) return false;
    if (filters.pack && d?.part?.package !== filters.pack) return false;
    if (filters.tier && !item.fidelity_tiers.some((t) => t === filters.tier)) return false;
    if (filters.named && !d?.part) return false;
    const sub = subcategoryOf(item, catalog);
    const haystack = [item.id, item.name, item.category, categoryName(categoryOf(item)), sub && subcategoryName(categoryOf(item), sub), d?.description, d?.part?.manufacturer, d?.part?.mpn, d?.part?.package, ...(d?.keywords ?? [])].join(' ').toLocaleLowerCase();
    return words.every((word) => haystack.includes(word));
  }).sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true }));
}
