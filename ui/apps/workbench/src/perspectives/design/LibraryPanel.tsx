// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button, Input, cx } from '@embsim/design';
import type { CatalogItem, FidelityTier } from '@embsim/protocol';
import { SymbolThumbnail } from '@embsim/scene';
import { ArrowUpRight, BookOpen, Check, Layers, Plus, Search, SlidersHorizontal, X } from 'lucide-react';
import { useMemo, useState } from 'react';
import { useProject, type CatalogEntry } from '../../stores/project';
import { IndicatorEvidence } from './IndicatorEvidence';
import { categoryGroups, categoryName, filterLibrary } from './libraryFilters';

const TIERS: { tier: FidelityTier; letter: string; title: string }[] = [
  { tier: 'functional', letter: 'F', title: 'functional' },
  { tier: 'timed', letter: 'T', title: 'timed' },
  { tier: 'electrical', letter: 'E', title: 'electrical' },
];

export function FidelityBadges({ tiers }: { tiers: FidelityTier[] }) {
  return (
    <span className="wb-fidelity" role="img" aria-label={`Fidelity tiers: ${tiers.join(', ') || 'none'}`} title={`Fidelity tiers: ${tiers.join(', ') || 'none'}`}>
      {TIERS.map((t) => (
        <span key={t.tier} className={cx('wb-fidelity__tier', tiers.includes(t.tier) && 'wb-fidelity__tier--on')} aria-hidden="true">{t.letter}</span>
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
  const policy = useProject((s) => s.project?.policy ?? 'electrical');
  const [query, setQuery] = useState('');
  const [category, setCategory] = useState('');
  const [manufacturer, setManufacturer] = useState('');
  const [pack, setPack] = useState('');
  const [tier, setTier] = useState('');
  const [named, setNamed] = useState(false);
  const [selected, setSelected] = useState<string | null>(null);
  const [advanced, setAdvanced] = useState(false);
  const reset = () => { setQuery(''); setCategory(''); setManufacturer(''); setPack(''); setTier(''); setNamed(false); };
  const options = useMemo(() => {
    const descriptions = items.map((i) => catalog[i.id]?.description);
    return {
      manufacturers: [...new Set(descriptions.flatMap((d) => d?.part ? [d.part.manufacturer] : []))].sort(),
      packages: [...new Set(descriptions.flatMap((d) => d?.part ? [d.part.package] : []))].sort(),
      categories: [...new Set(items.map((i) => i.category))].sort(),
    };
  }, [catalog, items]);
  const visible = useMemo(() => filterLibrary(items, catalog, { query, category, manufacturer, pack, tier, named }), [items, catalog, query, category, manufacturer, pack, tier, named]);
  const active = Boolean(query || category || manufacturer || pack || tier || named);
  const preview = selected && visible.find((i) => i.id === selected);
  return (
    <div className="wb-library">
      <div className="wb-library__filters">
        <div className="wb-library__eyebrow"><Layers size={13} /> COMPONENT LIBRARY <span>{items.length}</span></div>
        <Input aria-label="Search components" placeholder="Part, function, manufacturer…" icon={<Search />} value={query} onChange={(e) => setQuery(e.target.value)} />
        <div className="wb-library__filterbar">
          <details className="wb-library__browse">
            <summary>{category ? categoryName(category) : 'Browse categories'}</summary>
            <nav aria-label="Component categories" className="wb-library__categories">
              <button type="button" aria-pressed={!category} onClick={() => setCategory('')}>All components <span>{items.length}</span></button>
              {categoryGroups(options.categories).map((group) => (
                <div key={group.name}>
                  <h4>{group.name}</h4>
                  {group.categories.map((c) => <button type="button" key={c} aria-pressed={category === c} onClick={() => setCategory(c)}>{categoryName(c)} <span>{items.filter((i) => i.category === c).length}</span></button>)}
                </div>
              ))}
            </nav>
          </details>
          <Button size="sm" variant="ghost" icon={<SlidersHorizontal />} aria-label="Filter components" aria-expanded={advanced} pressed={advanced} onClick={() => setAdvanced(!advanced)} />
        </div>
        {advanced && <div className="wb-library__advanced">
          <label>Manufacturer<select aria-label="Manufacturer" value={manufacturer} onChange={(e) => setManufacturer(e.target.value)}><option value="">All manufacturers</option>{options.manufacturers.map((m) => <option key={m}>{m}</option>)}</select></label>
          <label>Package<select aria-label="Package" value={pack} onChange={(e) => setPack(e.target.value)}><option value="">All packages</option>{options.packages.map((p) => <option key={p}>{p}</option>)}</select></label>
          <label>Model support<select aria-label="Model support" value={tier} onChange={(e) => setTier(e.target.value)}><option value="">Any model</option>{TIERS.map((t) => <option key={t.tier} value={t.tier}>{t.title}</option>)}</select></label>
          <label className="wb-library__check"><input type="checkbox" checked={named} onChange={(e) => setNamed(e.target.checked)} />Named manufacturer parts</label>
        </div>}
        <div className="wb-library__results"><span role="status">{visible.length} {visible.length === 1 ? 'component' : 'components'}</span>{active && <button type="button" onClick={reset}>Clear filters</button>}</div>
      </div>
      <ul className="wb-library__list" aria-label="Components">
        {visible.length === 0 && <li className="wb-library__empty"><Search size={24} /><strong>No matching components</strong><span>Try a broader search or clear a filter.</span><Button size="sm" onClick={reset}>Clear filters</Button></li>}
        {visible.map((item) => <LibraryItem key={item.id} item={item} entry={catalog[item.id]} policy={policy} selected={selected === item.id} onPreview={() => setSelected(item.id)} onPlace={onPlace} />)}
      </ul>
      {preview && catalog[preview.id] && <LibraryPreview item={preview} entry={catalog[preview.id]!} policy={policy} onClose={() => setSelected(null)} onPlace={() => onPlace(preview.id)} />}
    </div>
  );
}

function LibraryItem({ item, entry, policy, selected, onPreview, onPlace }: { item: CatalogItem; entry?: CatalogEntry; policy: FidelityTier; selected: boolean; onPreview: () => void; onPlace: (id: string) => void }) {
  const unavailable = useProject((state) => !state.editable || state.busy || state.commands.isBusy);
  return (
    <li className={cx('wb-library__item', selected && 'wb-library__item--selected')} draggable={!item.broken && item.fidelity_tiers.includes(policy)} onDragStart={(e) => { e.dataTransfer.setData('text/x-embsim-component', item.id); e.dataTransfer.effectAllowed = 'copy'; }}>
      <button type="button" className="wb-library__part" aria-label={`Preview ${item.name}`} aria-pressed={selected} onClick={onPreview} title={entry?.description.description ?? item.name}>
        <span className="wb-library__thumb">{entry && <SymbolThumbnail svg={entry.files.symbol ?? ''} symbol={entry.description.symbol} size={40} />}</span>
        <span className="wb-library__identity"><strong className="wb-library__name">{item.name}</strong><span className="wb-library__subtitle">{entry?.description.part ? `${entry.description.part.manufacturer} · ${entry.description.part.package}` : categoryName(item.category)}</span><span className="wb-library__meta"><FidelityBadges tiers={item.fidelity_tiers} />{item.broken ? <span className="es-badge es-badge--danger">broken</span> : entry?.description.inputs?.length ? <span className="wb-library__interactive">Interactive</span> : null}</span></span>
      </button>
      <Button size="sm" variant="ghost" icon={<Plus />} aria-label={`Place ${item.name}`} disabled={unavailable || item.broken || !item.fidelity_tiers.includes(policy)} title={item.fidelity_tiers.includes(policy) ? `Place in ${policy} circuit` : `No ${policy} model; preview for supported policies`} onClick={() => onPlace(item.id)}>Place</Button>
    </li>
  );
}

function LibraryPreview({ item, entry, policy, onClose, onPlace }: { item: CatalogItem; entry: CatalogEntry; policy: FidelityTier; onClose: () => void; onPlace: () => void }) {
  const unavailable = useProject((state) => !state.editable || state.busy || state.commands.isBusy);
  const d = entry.description;
  const [tab, setTab] = useState<'overview' | 'pins' | 'models'>('overview');
  const reviewed = d.provenance.sources?.some((s) => s.hash && s.url === d.part?.datasheet);
  return <section className="wb-library__preview" aria-label="Component preview">
    <header><span className="wb-library__eyebrow">PART DETAILS</span><Button size="sm" variant="ghost" icon={<X />} aria-label="Close component preview" onClick={onClose} /></header>
    <div className="wb-library__hero"><SymbolThumbnail svg={entry.files.symbol ?? ''} symbol={d.symbol} size={76} /><div><h3>{d.name}</h3><span>{d.part?.manufacturer ?? 'Generic building block'}</span><small>{d.part?.package ?? categoryName(item.category)} · {d.pins.length} terminals</small></div></div>
    <div className="wb-library__tabs" role="group" aria-label="Preview sections">{(['overview', 'pins', 'models'] as const).map((t) => <button type="button" key={t} aria-pressed={tab === t} onClick={() => setTab(t)}>{t}</button>)}</div>
    <div className="wb-library__detailbody">
      {tab === 'overview' && <><p>{d.description}</p>{d.part && <><span className="wb-library__source"><Check size={12} />{reviewed ? 'Source reviewed · behavioral model' : 'Source reference · behavioral model'}</span><a className="wb-library__datasheet" href={d.part.datasheet} target="_blank" rel="noopener noreferrer"><BookOpen size={13} /> Manufacturer datasheet <ArrowUpRight size={12} /></a></>}{Boolean(d.inputs?.length) && <p className="wb-library__hint">Live controls: {d.inputs?.map((i) => `${i.name}${i.unit ? ` (${i.unit})` : ''}`).join(', ')}. Adjust them in Run while the circuit is running.</p>}<code>{item.id}</code></>}
      {tab === 'pins' && <table className="es-table"><thead><tr><th scope="col">No.</th><th scope="col">Terminal</th><th scope="col">Function</th></tr></thead><tbody>{[...d.pins].sort((a, b) => (a.number ?? 0) - (b.number ?? 0)).map((p) => <tr key={p.name}><td>{p.number ?? '—'}</td><td>{p.name}</td><td>{p.doc ?? p.model?.kind ?? '—'}</td></tr>)}</tbody></table>}
      {tab === 'models' && d.qualification && <IndicatorEvidence evidence={d.qualification} />}
      {tab === 'models' && TIERS.map(({ tier }) => <div className="wb-library__model" key={tier}><strong>{tier}<span>{d.fidelity[tier] ? 'Provided' : 'Not provided'}</span></strong>{d.fidelity[tier] && <><p>{d.fidelity[tier]?.claims}</p>{Boolean(d.fidelity[tier]?.omissions?.length) && <p className="wb-library__hint">Limits: {d.fidelity[tier]?.omissions?.join('; ')}</p>}</>}</div>)}
    </div>
    {!item.fidelity_tiers.includes(policy) && <p className="wb-library__hint">No {policy} model. Choose a supported project policy before placing this part.</p>}
    <Button size="sm" icon={<Plus />} disabled={unavailable || item.broken || !item.fidelity_tiers.includes(policy)} onClick={onPlace}>Place {d.name}</Button>
  </section>;
}
