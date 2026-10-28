// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Badge, Button, Dialog, Input } from '@embsim/design';
import { assemblyCsv, assemblyLines, hasUnsafeInteger, verifyManifest, type AssemblyVariant, type EngineeringManifestData, type Project, type VariantPreview } from '@embsim/protocol';
import { useRef, useState } from 'react';
import { errorMessage } from '../../actions';
import { getClient, primeStores } from '../../connection';
import { platform } from '../../platform/platform';
import { useProject } from '../../stores/project';
import { VariantOverrideEditor } from './VariantOverrideEditor';

export function AssemblyVariants() {
  const project = useProject((s) => s.project), busy = useProject((s) => s.busy), catalog = useProject((s) => s.catalog);
  const [open, setOpen] = useState(false), [pending, setPending] = useState(false), [error, setError] = useState('');
  const [base, setBase] = useState<Project | null>(null), [inventory, setInventory] = useState<EngineeringManifestData | null>(null);
  const [selected, setSelected] = useState(''), [newName, setNewName] = useState(''), [target, setTarget] = useState('');
  const [preview, setPreview] = useState<{result: VariantPreview; data?: EngineeringManifestData; name: string} | null>(null);
  const revision = useRef<Project | null>(null), request = useRef(0);
  const stale = Boolean(base && project !== revision.current);
  const editable = Boolean(base && !project?.variant_origin && !hasUnsafeInteger(base.variants) && !stale);
  const disabled = pending || busy || !editable;
  const load = async (preferred?: string) => {
    const token = ++request.current, source = useProject.getState().project;
    setPending(true); setError(''); setPreview(null);
    try {
      const [sourceBase, checked] = await Promise.all([
        getClient().project.variant({op: 'base'}) as Promise<Project>,
        getClient().project.variant({op: 'preview'}) as Promise<VariantPreview>,
      ]);
      if (!checked.valid || !checked.manifest) throw new Error(checked.diagnostics.map((d) => d.message).join('; ') || 'Base project does not currently validate.');
      const data = await verifyManifest(checked.manifest);
      if (token !== request.current) return;
      if (source !== useProject.getState().project) throw new Error('Project changed while reading variants. Refresh to continue.');
      revision.current = source; setBase(sourceBase); setInventory(data);
      setSelected(preferred && sourceBase.variants?.[preferred] ? preferred : selected && sourceBase.variants?.[selected] ? selected : Object.keys(sourceBase.variants ?? {})[0] ?? '');
    } catch (e) { if (token === request.current) setError(errorMessage(e)); }
    finally { if (token === request.current) setPending(false); }
  };
  const mutate = async (label: string, fn: (variants: Record<string, AssemblyVariant>) => void, preferred = selected) => {
    if (disabled) return;
    await useProject.getState().mutate(label, (draft) => {const variants = structuredClone(draft.variants ?? {}); fn(variants); draft.variants = variants; return draft;});
    await load(preferred);
  };
  const create = async () => {
    setError('');
    try {
      const name = newName.trim();
      if (!/^[A-Za-z_][A-Za-z0-9_]{0,62}$/.test(name)) throw new Error('Use a variant identifier of up to 63 letters, digits and underscores.');
      await mutate(`Create assembly variant ${name}`, (variants) => {if (Object.hasOwn(variants, name)) throw new Error('Variant already exists.'); Object.defineProperty(variants, name, {value: {overrides: {}}, enumerable: true, configurable: true, writable: true});}, name);
      setNewName('');
    } catch (e) { setError(errorMessage(e)); }
  };
  const review = async (name = selected) => {
    if (pending || busy || stale) return;
    setPending(true); setError('');
    try {
      const result = await getClient().project.variant({op: 'preview', name: name || undefined}) as VariantPreview;
      const data = result.manifest ? await verifyManifest(result.manifest) : undefined;
      setPreview({result, data, name});
    } catch (e) { setError(errorMessage(e)); }
    finally { setPending(false); }
  };
  const activate = async () => {
    if (!preview?.result.valid || pending || stale) return;
    setPending(true); setError('');
    try {
      await getClient().project.variant({op: 'activate', name: preview.name || undefined, expected_sha256: preview.result.base_sha256});
      useProject.getState().commands.clear();
      await primeStores(getClient());
      setOpen(false); setBase(null); setPreview(null);
    } catch (e) { setError(errorMessage(e)); }
    finally { setPending(false); }
  };
  const exportPreview = async (kind: 'project' | 'bom') => {
    if (!preview?.result.valid || !preview.data || stale || pending) return;
    setError('');
    try {
      const fresh = await getClient().project.variant({op: 'preview', name: preview.name || undefined}) as VariantPreview;
      if (!fresh.valid || fresh.base_sha256 !== preview.result.base_sha256) throw new Error('Base project changed. Preview again before exporting.');
      await (await platform()).saveTextFile(`${base?.name ?? 'project'}-${preview.name || 'base'}${kind === 'bom' ? '-bom.csv' : '.json'}`, kind === 'bom' ? assemblyCsv(assemblyLines(preview.data!), false, true) : preview.data.project.json, kind === 'bom' ? 'text/csv' : 'application/json');
    } catch (e) { setError(errorMessage(e)); }
  };
  const close = () => {request.current++; setOpen(false); setPending(false); setBase(null); setPreview(null);};
  const variant = base?.variants?.[selected];
  const parts = inventory?.instances.filter((i) => i.kind === 'component' && !catalog[i.reference]?.description.bench?.wiring_aid) ?? [];
  const part = parts.find((i) => i.path === target);
  const lines = preview?.data ? assemblyLines(preview.data).filter((l) => !l.simulationFixture) : [];
  const fitted = lines.filter((l) => l.populated).reduce((n, l) => n + l.paths.length, 0), dnp = lines.filter((l) => !l.populated).reduce((n, l) => n + l.paths.length, 0);
  return <>
    <Button size="sm" variant="ghost" disabled={!project || busy} onClick={() => {setOpen(true); void load();}}>Assembly variants{project?.variant_origin ? ` · ${project.variant_origin.name}` : ''}</Button>
    {project?.variant_origin && <Badge tone="warning">Derived circuit · return to base to edit</Badge>}
    <Dialog open={open} onOpenChange={(value) => value ? setOpen(true) : close()} title="Assembly variants" description="Named population, value and catalog alternatives, validated against the actual native circuit." className="wb-assembly-variants" footer={<>
      <Button size="sm" variant="ghost" disabled={pending} onClick={() => void load()}>Refresh variants</Button>
      <Button size="sm" disabled={pending || !base || stale} onClick={() => void review()}>Preview selected variant</Button>
      {project?.variant_origin && <Button size="sm" variant="ghost" disabled={pending || !base || stale} onClick={() => void review('')}>Preview base circuit</Button>}
    </>}>
      {pending && <p role="status">Validating native assembly data…</p>}
      {error && <p role="alert">{error}</p>}
      {stale && <p role="status">Project changed. Refresh variants before editing or activating.</p>}
      {project?.variant_origin && <p>You are running <strong>{project.variant_origin.name}</strong>. This derived circuit is read-only; preview and activate the base circuit to edit its definitions.</p>}
      {base && <>
        {hasUnsafeInteger(base.variants) && <p role="alert">Large integer variant fields require editing the exact source JSON. Native preview and activation remain available.</p>}
        <div className="wb-variant-picker"><label>Selected variant<select aria-label="Selected assembly variant" value={selected} disabled={pending} onChange={(e) => {setSelected(e.target.value); setPreview(null); setTarget('');}}><option value="">Base circuit</option>{Object.keys(base.variants ?? {}).map((name) => <option key={name} value={name}>{name}</option>)}</select></label>
        {editable && <><label>New variant identifier<Input aria-label="New variant identifier" value={newName} disabled={disabled} onChange={(e) => setNewName(e.target.value)} /></label><Button size="sm" disabled={disabled || !newName.trim()} onClick={() => void create()}>Create variant</Button></>}</div>
        {variant && <>
          <p>{variant.description || 'No description'} · {Object.keys(variant.overrides).length} component alternatives. Definitions do not change the running base until activation.</p>
          <details><summary>Edit description or remove variant</summary><form onSubmit={(e) => {e.preventDefault(); const data = new FormData(e.currentTarget); void mutate('Describe assembly variant', (variants) => {variants[selected] = {...variants[selected]!, description: String(data.get('description') ?? '')};}).catch((err: unknown) => setError(errorMessage(err)));}}><Input key={`${selected}-${variant.description}`} name="description" aria-label="Variant description" defaultValue={variant.description ?? ''} disabled={disabled} /><Button size="sm" type="submit" disabled={disabled}>Save description</Button></form><Button size="sm" variant="ghost" disabled={disabled} onClick={() => void mutate('Remove assembly variant', (variants) => {delete variants[selected];}, '').catch((e: unknown) => setError(errorMessage(e)))}>Remove variant</Button></details>
          <div className="wb-variant-overrides"><section aria-label="Component alternatives"><table><thead><tr><th>Reference</th><th>Population / changes</th><th /></tr></thead><tbody>{Object.entries(variant.overrides).map(([path, change]) => <tr key={path}><td><Button size="sm" variant="ghost" onClick={() => setTarget(path)}>{path}</Button></td><td>{change.populated === false ? 'DNP' : change.populated === true ? 'Fitted' : 'Base population'}<small>{change.component} {change.version} {JSON.stringify(change.params ?? {})}</small></td><td><Button size="sm" variant="ghost" disabled={disabled} onClick={() => void mutate('Remove component alternative', (variants) => {delete variants[selected]!.overrides[path];}).catch((e: unknown) => setError(errorMessage(e)))}>Remove {path}</Button></td></tr>)}</tbody></table>
          <label>Component to configure<select aria-label="Variant component target" value={target} disabled={disabled} onChange={(e) => setTarget(e.target.value)}><option value="">Choose concrete component</option>{parts.map((i) => <option key={i.path} value={i.path}>{i.path} · {i.reference}</option>)}</select></label></section>
          {part && <VariantOverrideEditor key={`${selected}-${part.path}-${JSON.stringify(variant.overrides[part.path])}`} item={part} initial={variant.overrides[part.path] ?? {}} disabled={disabled} onSave={async (change) => {await mutate(`Configure ${part.path} in ${selected}`, (variants) => {if (Object.keys(change).length) variants[selected]!.overrides = {...variants[selected]!.overrides, [part.path]: change}; else delete variants[selected]!.overrides[part.path];});}} />}</div>
        </>}
        {preview && <section aria-label="Native variant preview"><h3>{preview.name || 'Base circuit'} <Badge tone={preview.result.valid ? 'accent' : 'danger'}>{preview.result.valid ? 'Native validation passed' : 'Native validation failed'}</Badge></h3>
          {preview.result.diagnostics.map((d, i) => <p key={i}>{d.code}: {d.message}</p>)}
          {preview.data && <><p>{fitted} fitted {fitted === 1 ? 'part' : 'parts'} · {dnp} DNP {dnp === 1 ? 'footprint' : 'footprints'}. DNP pads and wires remain; device behavior and load are absent.</p><table aria-label="Variant assembly preview"><thead><tr><th>Population</th><th>References</th><th>Part / resolved values</th></tr></thead><tbody>{lines.slice(0,200).map((l, i) => <tr key={i}><td>{l.populated ? 'Fitted' : 'DNP'}</td><td>{l.paths.join(', ')}</td><td>{l.reference}<small>{l.unsafeIntegerPreview ? 'Exact values in native manifest' : JSON.stringify(l.params)}</small></td></tr>)}</tbody></table></>}
          <p>Activation restarts simulation, clears acquisition/history and opens a read-only derived circuit. Save the derived circuit under a new file name; the saved base stays intact. Export beside the base project when using relative library or firmware paths.</p>
          <div className="wb-variant-actions"><Button size="sm" disabled={pending || stale || !preview.result.valid} onClick={() => void activate()}>Activate {preview.name || 'base circuit'}</Button><Button size="sm" variant="ghost" disabled={pending || stale || !preview.result.valid} onClick={() => void exportPreview('project')}>Export derived project</Button><Button size="sm" variant="ghost" disabled={pending || stale || !preview.result.valid || !preview.data} onClick={() => void exportPreview('bom')}>Export variant BOM</Button></div>
        </section>}
      </>}
      <p className="es-muted">Scope: project-owned components and packages across sheets. Board internals, bench wiring aids, firmware builds, PCB assembly output and empty-footprint parasitics are not included. Population visuals use the schematic; 3D assembly variants come later.</p>
    </Dialog>
  </>;
}
