// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Badge, Button, Input } from '@embsim/design';
import type { CheckSeverity, SchematicIssue } from '@embsim/scene';
import { AlertCircle, AlertTriangle, CheckCircle2, ChevronDown, ChevronUp, Info, RefreshCw } from 'lucide-react';
import { useMemo, useState } from 'react';
import { t as tr } from '../../i18n';

export function SchematicChecksPanel({ issues, pending, error, selected, onFocus, onRoute, onRefresh, writable }: {
  issues: SchematicIssue[]; pending: boolean; error?: string; selected: string | null;
  onFocus(issue: SchematicIssue): void; onRoute(net: string): void; onRefresh(): void; writable: boolean;
}) {
  const [severity, setSeverity] = useState<'all' | CheckSeverity>('all');
  const [query, setQuery] = useState('');
  const filtered = useMemo(() => issues.filter((i) => (severity === 'all' || i.severity === severity) && `${i.title} ${i.detail} ${i.code}`.toLowerCase().includes(query.toLowerCase())), [issues, severity, query]);
  const active = issues.find((i) => i.id === selected);
  const navigate = (delta: number) => {
    if (!filtered.length) return;
    const current = filtered.findIndex((i) => i.id === selected);
    onFocus(filtered[current < 0 ? (delta > 0 ? 0 : filtered.length - 1) : (current + delta + filtered.length) % filtered.length]!);
  };
  return <div className="wb-schematic-checks" aria-label={tr('checks.region')}>
    <div className="wb-schematic-checks__toolbar">
      <div className="wb-check-filters" role="group" aria-label={tr('checks.severity')}>
        {(['all', 'error', 'warning', 'info'] as const).map((s) => <button type="button" key={s} aria-pressed={severity === s} onClick={() => setSeverity(s)}>{s === 'all' ? 'All' : s === 'info' ? 'Information' : `${s[0]!.toUpperCase()}${s.slice(1)}s`}<span>{s === 'all' ? issues.length : issues.filter((i) => i.severity === s).length}</span></button>)}
      </div>
      <Input label={tr('checks.filter')} placeholder={tr('checks.filterPlaceholder')} value={query} onChange={(e) => setQuery(e.target.value)} />
      <Button size="sm" variant="ghost" icon={<ChevronUp />} disabled={!filtered.length || pending} onClick={() => navigate(-1)}>{tr('checks.previous')}</Button>
      <Button size="sm" variant="ghost" icon={<ChevronDown />} disabled={!filtered.length || pending} onClick={() => navigate(1)}>{tr('checks.next')}</Button>
      <Button size="sm" variant="ghost" icon={<RefreshCw />} disabled={pending} onClick={onRefresh}>{tr('checks.recheck')}</Button>
    </div>
    <p className="wb-checks-scope">Drawing and connectivity checks include unconnected digital inputs. Live net warnings report observed floating or unknown inputs and contention. Analog accuracy and physical clearances are not checked.</p>
    {pending ? <p role="status" className="wb-checks-empty">{tr('checks.checking')}</p> : error ? <p role="alert" className="wb-checks-empty wb-properties-error">{error}</p> : !filtered.length ? <p role="status" className="wb-checks-empty"><CheckCircle2 size={18} />{issues.length ? tr('checks.noMatch') : tr('checks.none')}</p> : <div className="wb-checks-body">
      <div className="wb-checks-list" role="list" aria-label={tr('checks.results')}>
        {filtered.slice(0, 200).map((issue) => {
          const Icon = issue.severity === 'error' ? AlertCircle : issue.severity === 'warning' ? AlertTriangle : Info;
          return <div key={issue.id} role="listitem"><button type="button" data-check-id={issue.id} aria-pressed={selected === issue.id} className={`wb-check-row wb-check-row--${issue.severity}`} onClick={() => onFocus(issue)}><Icon size={15} /><span><strong>{issue.title}</strong><small>{issue.code}</small></span><Badge>{issue.severity}</Badge></button></div>;
        })}
        {filtered.length > 200 && <p className="wb-checks-scope">Showing the first 200 of {filtered.length}. Refine the filter or use Next/Previous to navigate every result.</p>}
      </div>
      <div className="wb-check-detail" aria-live="polite">
        {active ? <><Badge tone={active.severity === 'error' ? 'danger' : active.severity === 'warning' ? 'warning' : 'default'}>{active.code}</Badge><h3>{active.title}</h3><p>{active.detail}</p><div className="wb-check-detail__actions"><Button size="sm" onClick={() => onFocus(active)}>{tr('checks.locate')}</Button>{active.net && (active.code === 'undrawn-net' || active.code === 'wire-through-symbol' || active.code === 'invalid-wire') && <Button size="sm" variant="ghost" disabled={!writable} onClick={() => onRoute(active.net!)}>{tr('checks.routeNet')}</Button>}</div></> : <p className="es-muted">{tr('checks.choose')}</p>}
      </div>
    </div>}
  </div>;
}
