// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button, Input, Select, useToast } from '@embsim/design';
import { useEffect, useMemo, useState } from 'react';
import { errorMessage } from '../../actions';
import { useProject } from '../../stores/project';
import { useSelection } from '../../stores/selection';
import { commonParameters, parameterValue } from './parameterEditing';

/** Exact placement and batch editing share the normal transactional history. */
export function SelectionProperties() {
  const project = useProject((s) => s.project);
  const catalog = useProject((s) => s.catalog);
  const sceneCatalog = useProject((s) => s.sceneCatalog);
  const store = useProject();
  const ids = useSelection((s) => s.selection.components);
  const { toast } = useToast();
  const [x, setX] = useState('0'), [y, setY] = useState('0');
  const [rotation, setRotation] = useState('0');
  const [mirror, setMirror] = useState(false);
  const [enabled, setEnabled] = useState<Record<string, boolean>>({});
  const [values, setValues] = useState<Record<string, string>>({});
  const part = ids.length === 1 ? [...(project?.components ?? []), ...(project?.boards ?? [])].find((c) => c.id === ids[0]) : undefined;
  const shared = useMemo(() => project ? commonParameters(project, catalog, ids) : [], [project, catalog, ids]);
  useEffect(() => {
    setX(String(part?.layout?.x ?? 0)); setY(String(part?.layout?.y ?? 0));
    setRotation(String(part?.layout?.rotation ?? 0)); setMirror(!!part?.layout?.mirror);
    setEnabled({}); setValues(Object.fromEntries(shared.map((p) => [p.name, p.mixed ? '' : String(p.value ?? '')])));
  }, [part, shared]);
  if (!project || !ids.length) return null;
  const disabled = !store.editable || store.busy || store.commands.isBusy;
  const locked = [...project.components, ...(project.boards ?? [])].some((c) => ids.includes(c.id) && c.layout?.locked);
  const placementDisabled = disabled || locked;
  const validPosition = x.trim() !== '' && y.trim() !== '' && [Number(x), Number(y)].every(Number.isFinite);
  const batchErrors = shared.filter((p) => enabled[p.name]).map((p) => parameterValue(p.param, values[p.name] ?? '').error).filter(Boolean);
  const fail = (e: unknown) => toast({ kind: 'error', title: 'Properties not applied', description: errorMessage(e) });
  const ref = part && ('component' in part ? part.component : part.board);
  const symbol = ref ? sceneCatalog[ref]?.symbol : undefined;
  return (
    <div className="wb-selection-properties">
      <section className="wb-inspector__section" aria-label="Placement">
        <h3>{ids.length === 1 ? 'Placement' : `Move ${ids.length} selected items`}</h3>
        <p className="es-muted">{ids.length === 1 ? 'Sheet coordinates · snapped to 10 units' : 'Relative offset for the entire selection'}</p>
        <Button size="sm" disabled={disabled} onClick={() => void store.setPlacementLocked(ids, !locked).catch(fail)}>{locked ? 'Unlock placement' : 'Lock placement'}</Button>
        {locked && <p className="es-muted">Unlock the selection to change placement. Parameters remain editable.</p>}
        <form onSubmit={(e) => {
          e.preventDefault(); if (placementDisabled || !validPosition) return;
          const operation = part ? store.setPartLayout(part.id, { ...part.layout, x: Number(x), y: Number(y), rotation: Number(rotation), mirror }, project) : store.moveComponents(ids, Number(x), Number(y), true);
          void operation.then(() => toast({ kind: 'success', title: 'Placement applied', duration: 1500 })).catch(fail);
        }}>
          <div className="wb-properties-pair">
            <Input label={part ? 'Position X' : 'Offset X'} mono value={x} onChange={(e) => setX(e.target.value)} disabled={placementDisabled} />
            <Input label={part ? 'Position Y' : 'Offset Y'} mono value={y} onChange={(e) => setY(e.target.value)} disabled={placementDisabled} />
          </div>
          {part && <>
            <Select label="Rotation" value={rotation} options={[0, 90, 180, 270].map((n) => ({ value: String(n), label: `${n}°` }))} onValueChange={setRotation} disabled={placementDisabled} />
            <label className="wb-property-toggle"><input type="checkbox" checked={mirror} onChange={(e) => setMirror(e.target.checked)} disabled={placementDisabled} />Mirror symbol</label>
            {symbol && <p className="es-muted">Symbol {symbol.width} × {symbol.height} sheet units</p>}
          </>}
          {!validPosition && <p role="alert" className="wb-properties-error">Enter finite X and Y coordinates.</p>}
          <Button type="submit" size="sm" disabled={placementDisabled || !validPosition}>Apply placement</Button>
        </form>
      </section>
      {ids.length > 1 && <section className="wb-inspector__section" aria-label="Shared parameters">
        <h3>Shared parameters</h3>
        <p className="es-muted">{shared.length ? 'Choose the properties to apply to every selected part in one edit.' : 'No compatible parameters across this selection.'}</p>
        <form onSubmit={(e) => {
          e.preventDefault(); if (disabled || batchErrors.length) return;
          const changes = Object.fromEntries(shared.filter((p) => enabled[p.name]).map((p) => [p.name, parameterValue(p.param, values[p.name] ?? '').value!]));
          if (!Object.keys(changes).length) return;
          void store.setBatchParams(ids, changes, project).then(() => toast({ kind: 'success', title: 'Shared properties applied', duration: 1500 })).catch(fail);
        }}>
          {shared.map(({ name, param, mixed }) => {
            const error = enabled[name] ? parameterValue(param, values[name] ?? '').error : undefined;
            return <div key={name} className="wb-batch-property">
              <label className="wb-property-toggle"><input type="checkbox" checked={!!enabled[name]} disabled={disabled} onChange={(e) => setEnabled({ ...enabled, [name]: e.target.checked })} />Set {name}{mixed && <span className="es-muted" aria-hidden="true"> · mixed</span>}</label>
              {enabled[name] && (param.type === 'boolean' || param.type === 'enum' ? <Select label={`Shared ${name}`} value={values[name] ?? ''} placeholder="Choose a value" options={(param.type === 'boolean' ? ['true', 'false'] : param.enum ?? []).map((v) => ({ value: v, label: v }))} onValueChange={(v) => setValues({ ...values, [name]: v })} disabled={disabled} /> : <Input label={`Shared ${name}`} value={values[name] ?? ''} placeholder={mixed ? 'Mixed values' : undefined} unit={param.unit} onChange={(e) => setValues({ ...values, [name]: e.target.value })} error={error} disabled={disabled} />)}
              {error && (param.type === 'enum' || param.type === 'boolean') && <p role="alert" className="wb-properties-error">{error}</p>}
            </div>;
          })}
          {!!shared.length && <Button type="submit" size="sm" disabled={disabled || !!batchErrors.length || !Object.values(enabled).some(Boolean)}>Apply shared properties</Button>}
        </form>
      </section>}
    </div>
  );
}
