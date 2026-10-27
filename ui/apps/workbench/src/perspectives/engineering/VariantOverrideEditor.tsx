// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button, Input } from '@embsim/design';
import { hasUnsafeInteger, type AssemblyVariantOverride, type ComponentDescription, type ManifestInstance } from '@embsim/protocol';
import { useState } from 'react';
import { errorMessage } from '../../actions';
import { useProject } from '../../stores/project';

export function VariantOverrideEditor({item, initial, disabled, onSave}: {item: ManifestInstance; initial: AssemblyVariantOverride; disabled: boolean; onSave(value: AssemblyVariantOverride): Promise<void>}) {
  const catalog = useProject((s) => s.catalog), items = useProject((s) => s.items);
  const [population, setPopulation] = useState(initial.populated === undefined ? 'base' : initial.populated ? 'fitted' : 'dnp');
  const [replacement, setReplacement] = useState(initial.component ?? '');
  const [versionOverride, setVersionOverride] = useState(initial.version !== undefined), [version, setVersion] = useState(initial.version ?? '');
  const [params, setParams] = useState(initial.params ?? {}), [param, setParam] = useState(''), [value, setValue] = useState('');
  const [error, setError] = useState(''), [saving, setSaving] = useState(false);
  const desc = catalog[replacement || item.reference]?.description as ComponentDescription | undefined;
  const parameter = desc?.params?.[param];
  const add = () => {
    try {
      const next = parameter?.type === 'boolean' ? value === 'true' : parameter?.type === 'table' ? JSON.parse(value) as unknown : value;
      if (hasUnsafeInteger(next)) throw new Error('Large integer table values require editing the exact source JSON.');
      JSON.stringify(next, (_key, item: unknown) => {if (typeof item === 'number' && !Number.isFinite(item)) throw new Error('Table values must be finite.'); return item;});
      setParams({...params, [param]: next}); setError('');
    } catch (e) { setError(errorMessage(e)); }
  };
  const submit = async () => {
    setError(''); setSaving(true);
    const change: AssemblyVariantOverride = {};
    if (population !== 'base') change.populated = population === 'fitted';
    if (replacement) change.component = replacement;
    if (versionOverride) change.version = version.trim() || null;
    if (Object.keys(params).length) change.params = params;
    try { await onSave(change); } catch (e) { setError(errorMessage(e)); }
    finally { setSaving(false); }
  };
  return <form className="wb-variant-override" aria-label={`Variant override for ${item.path}`} onSubmit={(e) => {e.preventDefault(); void submit();}}>
    <strong>{item.path} · {item.reference}</strong>
    <label>Population<select aria-label="Override population" value={population} disabled={disabled} onChange={(e) => setPopulation(e.target.value)}>
      <option value="base">Keep base population</option><option value="fitted">Fitted</option><option value="dnp">DNP · footprint only</option>
    </select></label>
    <label>Catalog replacement<select aria-label="Catalog replacement" value={replacement} disabled={disabled} onChange={(e) => {setReplacement(e.target.value); setParam('');}}>
      <option value="">Keep {item.reference}</option>{items.filter((i) => i.kind === 'component' && !i.id.startsWith('block/')).map((i) => <option key={i.id} value={i.id}>{i.name} · {i.id}</option>)}
    </select></label>
    <small>Replacements require the same visible terminal names, positions and symbol dimensions. They start from the new part’s defaults plus these explicit overrides; native preview checks the circuit.</small>
    <label className="wb-variant-check"><input type="checkbox" checked={versionOverride} disabled={disabled} onChange={(e) => setVersionOverride(e.target.checked)} />Override version request</label>
    {versionOverride && <Input aria-label="Variant version request" value={version} disabled={disabled} onChange={(e) => setVersion(e.target.value)} placeholder="Empty explicitly removes the base request" />}
    <fieldset disabled={disabled}><legend>Value overrides</legend>
      <label>Parameter<select aria-label="Variant parameter" value={param} onChange={(e) => {const key=e.target.value; setParam(key); const p=desc?.params?.[key]; setValue(p?.type === 'boolean' ? 'false' : p?.type === 'enum' ? p.enum?.[0] ?? '' : '');}}>
        <option value="">Choose parameter</option>{Object.entries(desc?.params ?? {}).map(([key, p]) => <option key={key} value={key}>{key}{p.unit ? ` · ${p.unit}` : ''}</option>)}
      </select></label>
      {param && <>
        {parameter?.type === 'boolean' || parameter?.type === 'enum' ? <select aria-label="Variant parameter value" value={value} onChange={(e) => setValue(e.target.value)}>{(parameter.type === 'boolean' ? ['false','true'] : parameter.enum ?? []).map((v) => <option key={v} value={v}>{v}</option>)}</select> : <Input aria-label="Variant parameter value" value={value} onChange={(e) => setValue(e.target.value)} placeholder={parameter?.type === 'table' ? 'JSON table' : 'Value · SI suffixes supported'} />}
        <small>{parameter?.doc}</small><Button type="button" size="sm" variant="ghost" disabled={!value} onClick={add}>Set value override</Button>
      </>}
      {Object.entries(params).map(([key, v]) => <div className="wb-variant-param" key={key}><code>{key} = {typeof v === 'string' ? v : JSON.stringify(v)}</code><Button type="button" size="sm" variant="ghost" onClick={() => setParams(Object.fromEntries(Object.entries(params).filter(([k]) => k !== key)))}>Keep base {key}</Button></div>)}
    </fieldset>
    <p>Saving edits the named definition and preserves simulation time. Preview validates the complete derived circuit before activation.</p>
    {error && <p role="alert">{error}</p>}
    <Button size="sm" type="submit" disabled={disabled || saving}>Save component alternative</Button>
  </form>;
}
