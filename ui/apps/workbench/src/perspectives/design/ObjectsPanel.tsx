// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button, Input, useToast } from '@embsim/design';
import { useState } from 'react';
import { errorMessage } from '../../actions';
import { platform } from '../../platform/platform';
import { useProject } from '../../stores/project';
import { useSelection } from '../../stores/selection';
import { assignGroup, partsListCsv } from './circuitOrganization';
import { t as tr } from '../../i18n';

export function ObjectsPanel({ onFocus, writable = true }: { onFocus(ids: string[]): void; writable?: boolean }) {
  const store = useProject();
  const selection = useSelection((s) => s.selection);
  const setSelection = useSelection((s) => s.setSelection);
  const [query, setQuery] = useState('');
  const [name, setName] = useState('');
  const { toast } = useToast();
  const p = store.project;
  if (!p) return null;
  const disabled = !writable || !store.editable || store.busy || store.commands.isBusy;
  const fail = (e: unknown) => toast({ kind: 'error', title: tr('objects.failed'), description: errorMessage(e) });
  const focus = (ids: string[]) => { setSelection({ components: ids, wires: [] }); onFocus(ids); };
  const parts = [...p.components, ...(p.boards ?? [])];
  const matches = (s: string) => s.toLowerCase().includes(query.trim().toLowerCase());
  const filtered = parts.filter((c) => matches(`${c.id} ${'component' in c ? c.component : c.board} ${p.layout?.groups?.find((g) => g.members.includes(c.id))?.name ?? ''}`));
  return <section className="wb-objects" aria-label={tr('objects.region')}>
    <div className="wb-objects__toolbar">
      <Input label={tr('objects.search')} value={query} onChange={(e) => setQuery(e.target.value)} placeholder={tr('objects.searchPlaceholder')} />
      <Input label={tr('objects.groupName')} value={name} onChange={(e) => setName(e.target.value)} placeholder={tr('objects.groupPlaceholder')} disabled={disabled} />
      <Button size="sm" disabled={disabled || !selection.components.length || !name.trim()} onClick={() => void store.mutate('Group selected parts', (project) => { if (useProject.getState().project !== p) throw new Error('The project changed. Select the parts again.'); return assignGroup(project, selection.components, name); }).then(() => setName('')).catch(fail)}>{tr('objects.groupSelection')}</Button>
      <Button size="sm" onClick={() => void platform().then((host) => host.saveTextFile('parts-list.csv', partsListCsv(p), 'text/csv')).catch(fail)}>{tr('objects.export')}</Button>
    </div>
    <p className="es-muted">{tr('objects.counts', { parts: parts.length, groups: p.layout?.groups?.length ?? 0, locked: parts.filter((c) => c.layout?.locked).length })} Groups organize the drawing; placement locks protect position and orientation.</p>
    <div className="wb-objects__groups">
      {(p.layout?.groups ?? []).filter((g) => matches(`${g.name} ${g.members.join(' ')}`)).map((g) => <div className="wb-objects__group" key={g.id}>
        <Button size="sm" onClick={() => focus(g.members)}>{g.name} · {g.members.length} parts</Button>
        <Button size="sm" disabled={disabled || !name.trim()} onClick={() => void store.mutate(`Rename ${g.name}`, (project) => { const group = project.layout?.groups?.find((v) => v.id === g.id); if (group) group.name = name.trim(); return project; }).then(() => setName('')).catch(fail)}>{tr('design.rename')}</Button>
        <Button size="sm" disabled={disabled} onClick={() => void store.setPlacementLocked(g.members, !parts.some((c) => g.members.includes(c.id) && c.layout?.locked)).catch(fail)}>{parts.some((c) => g.members.includes(c.id) && c.layout?.locked) ? 'Unlock group' : 'Lock group'}</Button>
        <Button size="sm" disabled={disabled} onClick={() => void store.mutate(`Ungroup ${g.name}`, (project) => { project.layout = { ...project.layout, groups: (project.layout?.groups ?? []).filter((v) => v.id !== g.id) }; return project; }).catch(fail)}>{tr('objects.ungroup')}</Button>
      </div>)}
    </div>
    <table className="wb-objects__table"><thead><tr><th>{tr('objects.reference')}</th><th>{tr('objects.model')}</th><th>{tr('objects.group')}</th><th>{tr('objects.placement')}</th></tr></thead><tbody>
      {filtered.map((c) => <tr key={c.id} aria-selected={selection.components.includes(c.id)}>
        <td><button type="button" onClick={(e) => { if (e.shiftKey) setSelection({ components: selection.components.includes(c.id) ? selection.components.filter((id) => id !== c.id) : [...selection.components, c.id], wires: [] }); else focus([c.id]); }}>{c.id}</button></td>
        <td>{'component' in c ? c.component : c.board}</td><td>{p.layout?.groups?.find((g) => g.members.includes(c.id))?.name ?? '—'}</td>
        <td><Button size="sm" disabled={disabled} onClick={() => void store.setPlacementLocked([c.id], !c.layout?.locked).catch(fail)} aria-label={`${c.layout?.locked ? 'Unlock' : 'Lock'} ${c.id}`}>{c.layout?.locked ? 'Locked' : 'Free'}</Button></td>
      </tr>)}
    </tbody></table>
    {!filtered.length && <p className="es-muted">{tr('objects.noMatch')}</p>}
    <p className="es-muted">{tr('objects.help')}</p>
  </section>;
}
