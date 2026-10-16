// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button, Input, cx } from '@embsim/design';
import type { CatalogItem, FidelityTier } from '@embsim/protocol';
import { SymbolThumbnail } from '@embsim/scene';
import { Plus, Search } from 'lucide-react';
import { useMemo, useState } from 'react';
import { useProject } from '../../stores/project';

const TIERS: { tier: FidelityTier; letter: string; title: string }[] = [
  { tier: 'functional', letter: 'F', title: 'functional' },
  { tier: 'timed', letter: 'T', title: 'timed' },
  { tier: 'electrical', letter: 'E', title: 'electrical' },
];

export function FidelityBadges({ tiers }: { tiers: FidelityTier[] }) {
  return (
    <span className="wb-fidelity" role="img" aria-label={`Fidelity tiers: ${tiers.join(', ') || 'none'}`} title={`Fidelity tiers: ${tiers.join(', ') || 'none'}`}>
      {TIERS.map((t) => (
        <span key={t.tier} className={cx('wb-fidelity__tier', tiers.includes(t.tier) && 'wb-fidelity__tier--on')} aria-hidden="true">
          {t.letter}
        </span>
      ))}
    </span>
  );
}

export interface LibraryPanelProps {
  /** Places a component at the centre of the current view (keyboard path). */
  onPlace: (componentId: string) => void;
}

export function LibraryPanel({ onPlace }: LibraryPanelProps) {
  const items = useProject((s) => s.items);
  const catalog = useProject((s) => s.catalog);
  const [query, setQuery] = useState('');
  const [category, setCategory] = useState<string | null>(null);

  const categories = useMemo(() => [...new Set(items.map((i) => i.category))].sort(), [items]);
  const visible = useMemo(() => {
    const q = query.trim().toLowerCase();
    return items.filter((i) => {
      if (category && i.category !== category) return false;
      if (!q) return true;
      const d = catalog[i.id]?.description;
      return [i.id, i.name, i.category, d?.description ?? '', d?.part?.manufacturer ?? '', d?.part?.mpn ?? '', d?.part?.package ?? '', ...(d?.keywords ?? [])].some((t) => t.toLowerCase().includes(q));
    });
  }, [items, catalog, query, category]);

  return (
    <div className="wb-library">
      <div className="wb-library__filters">
        <Input aria-label="Search components" placeholder="Search components…" icon={<Search />} value={query} onChange={(e) => setQuery(e.target.value)} />
        <div className="wb-library__chips" role="group" aria-label="Category">
          <Button size="sm" variant={category === null ? 'default' : 'ghost'} pressed={category === null} onClick={() => setCategory(null)}>
            All
          </Button>
          {categories.map((c) => (
            <Button key={c} size="sm" variant={category === c ? 'default' : 'ghost'} pressed={category === c} onClick={() => setCategory(category === c ? null : c)}>
              {c}
            </Button>
          ))}
        </div>
      </div>
      <ul className="wb-library__list" aria-label="Components">
        {visible.length === 0 && <li className="es-muted" style={{ padding: 'var(--space-3)' }}>No component matches</li>}
        {visible.map((item) => (
          <LibraryItem key={item.id} item={item} onPlace={onPlace} />
        ))}
      </ul>
    </div>
  );
}

function LibraryItem({ item, onPlace }: { item: CatalogItem; onPlace: (id: string) => void }) {
  const entry = useProject((s) => s.catalog[item.id]);
  return (
    <li
      className="wb-library__item"
      draggable
      onDragStart={(e) => {
        e.dataTransfer.setData('text/x-embsim-component', item.id);
        e.dataTransfer.effectAllowed = 'copy';
      }}
      title={`${entry?.description.description ?? item.name} — drag onto the schematic or press Place`}
    >
      <span className="wb-library__thumb">{entry ? <SymbolThumbnail svg={entry.files.symbol ?? ''} symbol={entry.description.symbol} size={40} /> : null}</span>
      <span>
        <div className="wb-library__name">{item.name}</div>
        {entry?.description.part && <div className="es-muted" style={{ fontSize: 'var(--text-xs)' }}>{entry.description.part.manufacturer} · {entry.description.part.package}</div>}
        <div className="wb-library__meta">
          <span className="es-mono">{item.id}</span>
          <FidelityBadges tiers={item.fidelity_tiers} />
          {item.broken && <span className="es-badge es-badge--danger">broken</span>}
        </div>
      </span>
      <Button size="sm" variant="ghost" icon={<Plus />} aria-label={`Place ${item.name}`} onClick={() => onPlace(item.id)}>
        Place
      </Button>
    </li>
  );
}
