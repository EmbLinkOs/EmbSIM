// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Badge, Button, Dialog, Input } from '@embsim/design';
import { formatSi, formatTime, parseSi, sweepCsv, sweepValues, verifyManifest, type EngineeringManifestData, type ParameterSweep, type Project, type SweepRequest } from '@embsim/protocol';
import { useRef, useState } from 'react';
import { getClient } from '../../connection';
import { errorMessage } from '../../actions';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';
import { platform } from '../../platform/platform';

export function ParameterSweeps() {
  const project = useProject((s) => s.project), busy = useProject((s) => s.busy);
  const catalog = useProject((s) => s.catalog);
  const running = useSession((s) => s.running), policy = useSession((s) => s.policy), native = useSession((s) => s.capabilities.includes('analysis.sweep'));
  const [open, setOpen] = useState(false), [pending, setPending] = useState(false), [error, setError] = useState('');
  const [inventory, setInventory] = useState<EngineeringManifestData | null>(null), [target, setTarget] = useState(''), [parameter, setParameter] = useState('');
  const [start, setStart] = useState('100k'), [stop, setStop] = useState('10meg'), [count, setCount] = useState(11), [log, setLog] = useState(true);
  const [at, setAt] = useState('1us'), [probes, setProbes] = useState(''), [trace, setTrace] = useState(0);
  const [resultUnit, setResultUnit] = useState('');
  const [result, setResult] = useState<ParameterSweep | null>(null);
  const revision = useRef<Project | null>(null), epoch = useRef(0);
  const stale = Boolean(inventory && revision.current !== project);
  const parts = inventory?.instances.filter((i) => i.kind === 'component' && i.populated !== false && Object.values(catalog[i.reference]?.description.params ?? {}).some((p) => p.type === 'number')) ?? [];
  const part = parts.find((p) => p.path === target);
  const parameters = Object.entries(catalog[part?.reference ?? '']?.description.params ?? {}).filter(([, p]) => p.type === 'number');
  const unit = catalog[part?.reference ?? '']?.description.params?.[parameter]?.unit ?? '';
  const configure = (key: string) => {
    setParameter(key);
    const desc = catalog[part?.reference ?? '']?.description.params?.[key];
    const value = parseSi(part?.params[key] as number | string | undefined);
    if (value === undefined) return;
    const minimum = parseSi(desc?.min), maximum = parseSi(desc?.max);
    const a = Math.max(minimum ?? -Infinity, value > 0 ? value / 10 : value - (Math.abs(value) || 1));
    const b = Math.min(maximum ?? Infinity, value > 0 ? value * 10 : value + (Math.abs(value) || 1));
    if (Number.isFinite(a) && Number.isFinite(b)) {setStart(String(a)); setStop(String(b)); setLog(a > 0 && b > 0);}
  };
  const load = async () => {
    const token = ++epoch.current, source = useProject.getState().project;
    setPending(true); setError(''); setResult(null); setInventory(null);
    try {
      const data = await verifyManifest(await getClient().project.manifest({}));
      if (token !== epoch.current) return;
      if (source !== useProject.getState().project) throw new Error('Project changed while reading analysis inputs. Refresh to continue.');
      revision.current = source; setInventory(data); setTarget(''); setParameter('');
    } catch (e) { if (token === epoch.current) setError(errorMessage(e)); }
    finally { if (token === epoch.current) setPending(false); }
  };
  const run = async () => {
    if (pending || running || busy || stale || !native) return;
    const token = ++epoch.current; setPending(true); setError(''); setResult(null);
    try {
      const request: SweepRequest = {schema_version: 1, kind: 'parameter_sweep_request', target, parameter, values: sweepValues(start, stop, count, log), sample_at: at, probes: probes.split(/[\n,]/).map((p) => p.trim()).filter(Boolean)};
      const data = await getClient().analysis.sweep(request);
      const captured = await verifyManifest(data.source);
      if (captured.project.json !== inventory?.project.json) throw new Error('Native source changed. Refresh circuit inputs before analysis.');
      if (token !== epoch.current) return;
      if (revision.current !== useProject.getState().project) throw new Error('Project changed during analysis. Refresh before running again.');
      setResult(data); setResultUnit(unit); setTrace(0);
    } catch (e) { if (token === epoch.current) setError(errorMessage(e)); }
    finally { if (token === epoch.current) setPending(false); }
  };
  const download = async (kind: 'json' | 'csv') => {
    if (!result) return;
    try { await (await platform()).saveTextFile(`${inventory?.project ? JSON.parse(inventory.project.json).name as string : 'circuit'}-sweep.${kind}`, kind === 'csv' ? sweepCsv(result) : JSON.stringify(result, null, 2), kind === 'csv' ? 'text/csv' : 'application/json'); }
    catch (e) {setError(errorMessage(e));}
  };
  const blocked = pending || busy || running || stale || !native || policy !== 'electrical' || Boolean(inventory?.instances.some((i) => i.kind === 'board'));
  return <>
    <Button size="sm" variant="ghost" disabled={!project || busy} onClick={() => {setOpen(true); void load();}}>Parameter sweep</Button>
    <Dialog open={open} onOpenChange={(v) => {if (!v) {epoch.current++; setOpen(false); setPending(false);}}} title="Parameter sweep" description="Independent native circuit runs with measured electrical loading." className="wb-parameter-sweep" footer={<><Button size="sm" variant="ghost" disabled={pending} onClick={() => void load()}>Refresh circuit inputs</Button><Button size="sm" disabled={blocked || !target || !parameter || !probes.trim()} onClick={() => void run()}>Run isolated sweep</Button></>}>
      {!native && <p role="alert">Parameter sweeps require a native session with analysis.sweep support. Demo results are not fabricated.</p>}
      {policy !== 'electrical' && <p role="alert">Switch to electrical policy before analysis.</p>}
      {running && <p role="status">Pause the running circuit before analysis. Your current clock and controls will be preserved.</p>}
      {stale && <p role="alert">Circuit changed. Refresh inputs before another run. Existing results belong to the captured source.</p>}
      {inventory?.instances.some((i) => i.kind === 'board') && <p role="alert">This analysis supports board-free circuits; firmware sweeps are not implemented.</p>}
      {pending && <p role="status">Preparing independent native runs…</p>}
      {error && <p role="alert">{error}</p>}
      <div className="wb-sweep-config">
        <label>Component<select aria-label="Sweep component" value={target} disabled={blocked} onChange={(e) => {setTarget(e.target.value); setParameter('');}}><option value="">Choose concrete reference</option>{parts.map((p) => <option key={p.path} value={p.path}>{p.path} · {p.reference}</option>)}</select></label>
        <label>Parameter<select aria-label="Sweep parameter" value={parameter} disabled={blocked || !part} onChange={(e) => configure(e.target.value)}><option value="">Choose numeric parameter</option>{parameters.map(([key, p]) => <option key={key} value={key}>{key}{p.unit ? ` (${p.unit})` : ''}</option>)}</select></label>
        <label>Start<Input aria-label="Sweep start" value={start} disabled={blocked} onChange={(e) => setStart(e.target.value)} /></label>
        <label>Stop<Input aria-label="Sweep stop" value={stop} disabled={blocked} onChange={(e) => setStop(e.target.value)} /></label>
        <label>Points<Input aria-label="Sweep points" type="number" min={2} max={64} value={count} disabled={blocked} onChange={(e) => setCount(Number(e.target.value))} /></label>
        <label>Sample after reset<Input aria-label="Sweep sample time" value={at} disabled={blocked} onChange={(e) => setAt(e.target.value)} /></label>
        <label className="wb-sweep-wide">Add a terminal measurement<select aria-label="Add sweep terminal" value="" disabled={blocked || !part} onChange={(e) => {if (e.target.value) setProbes((old) => [...new Set([...old.split(/[\n,]/).map((p) => p.trim()).filter(Boolean), e.target.value])].join(', '));}}><option value="">Choose a terminal on {part?.path ?? 'the selected component'}</option>{(catalog[part?.reference ?? '']?.description.pins ?? []).map((pin) => <option key={pin.name} value={`pin:${part?.path}.${pin.name}`}>{part?.path}.{pin.name}</option>)}</select></label>
        <label className="wb-sweep-wide">Voltage measurements<textarea aria-label="Sweep measurements" rows={2} value={probes} disabled={blocked} onChange={(e) => setProbes(e.target.value)} placeholder="pin:A.R.2, pin:B.R.2 or net:OUT" /></label>
        <label className="wb-sweep-check"><input type="checkbox" checked={log} disabled={blocked} onChange={(e) => setLog(e.target.checked)} />Logarithmic parameter spacing</label>
      </div>
      <p className="es-muted">Each point starts from reset and samples at the stated time, with meter/scope loading included. This is not a converged DC operating point, AC, noise or distortion analysis. Limits: 64 points, eight measurements, 1 ms per point / 10 ms total, 256 compiled components. Runtime stimulus, firmware and host I/O are not replayed.</p>
      {result && <section aria-label="Sweep results"><div className="es-row"><h3>{result.request.target}.{result.request.parameter}</h3><Badge tone={result.ok ? 'accent' : 'danger'}>{result.ok ? `${result.points.length} points sampled` : 'Some points failed'}</Badge></div>
        <p>Captured source at {formatTime(BigInt(inventory?.now_ps ?? '0'))}. The open circuit was not restarted. Empty voltage cells mean unavailable, not zero; logic X/Z is reported separately from analog voltage.</p>
        <label>Plot measurement<select aria-label="Sweep plot measurement" value={trace} onChange={(e) => setTrace(Number(e.target.value))}>{result.request.probes.map((p, i) => <option key={p} value={i}>{p}</option>)}</select></label>
        <SweepPlot result={result} trace={trace} unit={resultUnit} />
        <div className="wb-sweep-table"><table aria-label="Native sweep samples"><thead><tr><th>Point</th><th>Resolved value</th><th>At</th>{result.request.probes.map((p) => <th key={p}>{p}</th>)}<th>Diagnostics</th></tr></thead><tbody>{result.points.map((p, i) => <tr key={i}><td>{i + 1} · {p.ok ? 'sampled' : 'failed'}</td><td>{p.resolved === null ? '—' : formatSi(p.resolved, resultUnit)}</td><td>{formatTime(BigInt(p.at_ps))}</td>{p.measurements.map((m) => <td key={m.target}>{m.voltage === null ? 'Unavailable' : formatSi(m.voltage, 'V', 6)} <small>Logic {m.logic}</small></td>)}<td>{p.diagnostics.map((d) => `${d.code}: ${d.message}`).join('; ') || '—'}</td></tr>)}</tbody></table></div>
        <div className="es-row"><Button size="sm" variant="ghost" onClick={() => void download('csv')}>Export sweep CSV</Button><Button size="sm" variant="ghost" onClick={() => void download('json')}>Export sweep evidence</Button></div>
      </section>}
    </Dialog>
  </>;
}

export function SweepPlot({result, trace, unit}: {result: ParameterSweep; trace: number; unit: string}) {
  const values = result.points.map((p) => ({x: p.resolved, y: p.ok ? p.measurements[trace]?.voltage ?? null : null}));
  const valid = values.filter((p): p is {x: number; y: number} => p.x !== null && p.y !== null && Number.isFinite(p.x) && Number.isFinite(p.y));
  if (!valid.length) return <p>No numeric analog measurements are available to plot.</p>;
  let xmin = Math.min(...valid.map((p) => p.x)), xmax = Math.max(...valid.map((p) => p.x));
  let ymin = Math.min(...valid.map((p) => p.y)), ymax = Math.max(...valid.map((p) => p.y));
  if (xmin === xmax) {if (xmin > 0) xmin *= .95; else if (xmax < 0) xmax *= .95; else {xmin = -1; xmax = 1;}}
  if (ymin === ymax) {if (ymin > 0) ymin *= .95; else if (ymax < 0) ymax *= .95; else {ymin = -1; ymax = 1;}}
  // Normalize before subtraction: finite opposite-sign endpoints can otherwise
  // overflow the plotting range even when all simulated samples are finite.
  const ratio = (v: number, min: number, max: number) => {const scale = Math.max(Math.abs(min), Math.abs(max)) || 1; return (v/scale - min/scale)/(max/scale - min/scale);};
  const x = (v: number) => 75 + 650 * ratio(v, xmin, xmax), y = (v: number) => 220 - 185 * ratio(v, ymin, ymax);
  let gap = true;
  const path = values.map((p) => {if (p.x === null || p.y === null) {gap = true; return '';} const command = `${gap ? 'M' : 'L'}${x(p.x)},${y(p.y)}`; gap = false; return command;}).join(' ');
  return <svg className="wb-sweep-plot" viewBox="0 0 800 275" role="img" aria-label={`Measured voltage at ${result.request.probes[trace]} versus ${result.request.parameter}; linear axes`}>
    <title>Native reset-and-sample response. Exact samples and diagnostics are in the table below.</title>
    {[0, .25, .5, .75, 1].map((t) => <g key={t}><line x1={75} x2={725} y1={35 + 185*t} y2={35 + 185*t} className="wb-sweep-grid" /><text x={65} y={40 + 185*t} textAnchor="end">{formatSi(ymax*(1-t) + ymin*t, 'V')}</text><text x={75 + 650*t} y={243} textAnchor="middle">{formatSi(xmin*(1-t) + xmax*t, unit)}</text></g>)}
    <path d={path} fill="none" className="wb-sweep-line" />{valid.map((p, i) => <circle key={i} cx={x(p.x)} cy={y(p.y)} r={4} className="wb-sweep-point"><title>{formatSi(p.x, unit, 8)} → {formatSi(p.y, 'V', 8)}</title></circle>)}
    <text x={400} y={269} textAnchor="middle">{result.request.parameter} · linear axis</text>
  </svg>;
}
